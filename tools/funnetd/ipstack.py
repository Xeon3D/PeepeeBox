"""A small IPv4 stack that lives on one PPP link.

The cabinet's world is exactly one point-to-point link, and everything it wants
to reach is on the far end of it.  That makes two simplifications not just
convenient but correct:

  * **Every destination is ours.**  \\FN_SYS\\DFU\\WATTCP.CFG carries hard-coded
    nameserver addresses -- 194.158.160.10 and friends on the MASTERS machine --
    and on some images a hard-coded `my_ip` as well.  Those addresses are long
    dead, but the packets still arrive here, because here is the only place
    they can go.  So the stack answers for any destination address rather than
    only its own, and replies from whatever address was asked for.  A cabinet
    then needs no reconfiguration to talk to us.

  * **No ARP, no routing, no fragmentation.**  One link, one MTU, one peer.

The TCP is deliberately plain: a fixed window, no out-of-order buffering, and a
single retransmission timer.  A serial link at 57600 baud does not reorder, and
the transfers are tens of kilobytes.
"""

import struct
import time

# --------------------------------------------------------------- checksums


def checksum(data):
    if len(data) & 1:
        data += b"\x00"
    s = 0
    for i in range(0, len(data), 2):
        s += (data[i] << 8) | data[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def _pseudo(src, dst, proto, length):
    return src + dst + struct.pack(">BBH", 0, proto, length)


def ip2s(b):
    return ".".join(str(x) for x in b)


def s2ip(s):
    return bytes(int(x) for x in s.split("."))


# ------------------------------------------------------- sequence arithmetic

M32 = 0xFFFFFFFF


def _lt(a, b):
    return ((a - b) & M32) > 0x7FFFFFFF


def _le(a, b):
    return a == b or _lt(a, b)


def _gt(a, b):
    return _lt(b, a)


def _ge(a, b):
    return a == b or _gt(a, b)


# ---------------------------------------------------------------------- TCP

FIN, SYN, RST, PSH, ACK, URG = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20


class TcpSocket:
    """One connection.

    The handler is any object with `on_open`, `on_data` and `on_close`; all
    three are optional and are called with the socket as first argument.
    """

    RTO = 1.0
    MAX_RETRY = 8
    WINDOW = 8192

    def __init__(self, stack, laddr, lport, raddr, rport, handler):
        self.stack = stack
        self.laddr, self.lport = laddr, lport
        self.raddr, self.rport = raddr, rport
        self.handler = handler

        self.state = "CLOSED"
        self.iss = int(time.time() * 1000) & 0x7FFFFFFF
        self.snd_una = self.iss
        self.snd_nxt = self.iss
        self.snd_wnd = 4096
        self.rcv_nxt = 0
        self.mss = 536

        self.sndbuf = bytearray()  # unacked; sndbuf[0] is snd_una
        self.closing = False  # app asked to close; FIN once drained
        self.fin_sent = False
        self.fin_seq = None

        self.rto_at = 0.0
        self.retries = 0
        self.dead_at = None

    # -- helpers ----------------------------------------------------------

    @property
    def key(self):
        return (self.laddr, self.lport, self.raddr, self.rport)

    def _cb(self, name, *a):
        fn = getattr(self.handler, name, None)
        if fn is not None:
            fn(self, *a)

    def _out(self, flags, seq=None, payload=b"", opts=b""):
        seq = self.snd_nxt if seq is None else seq
        off = (5 + len(opts) // 4) << 4
        hdr = struct.pack(">HHIIBBHHH", self.lport, self.rport, seq,
                          self.rcv_nxt if (flags & ACK) else 0,
                          off, flags, self.WINDOW, 0, 0)
        seg = hdr + opts + payload
        ck = checksum(_pseudo(self.laddr, self.raddr, 6, len(seg)) + seg)
        seg = seg[:16] + struct.pack(">H", ck) + seg[18:]
        self.stack.send_ip(self.laddr, self.raddr, 6, seg)

    def _arm(self):
        if self.sndbuf or self.fin_seq is not None:
            if self.rto_at == 0.0:
                self.rto_at = time.time() + self.RTO
        else:
            self.rto_at = 0.0
            self.retries = 0

    # -- application face -------------------------------------------------

    def send(self, data):
        if self.state not in ("ESTABLISHED", "CLOSE_WAIT"):
            return
        self.sndbuf += data
        self._pump()

    def close(self):
        self.closing = True
        self._pump()

    def abort(self):
        self._out(RST | ACK)
        self._done()

    def _done(self):
        if self.state != "CLOSED":
            self.state = "CLOSED"
            self._cb("on_close")
        self.dead_at = time.time() + 2.0  # soak up stray retransmissions

    def _pump(self):
        window = min(self.snd_wnd, 4 * self.mss)
        while True:
            inflight = (self.snd_nxt - self.snd_una) & M32
            avail = len(self.sndbuf) - inflight
            if avail <= 0 or inflight >= window:
                break
            n = min(avail, self.mss, window - inflight)
            chunk = bytes(self.sndbuf[inflight:inflight + n])
            self._out(ACK | PSH, self.snd_nxt, chunk)
            self.snd_nxt = (self.snd_nxt + n) & M32

        if (self.closing and not self.fin_sent and not self.sndbuf
                and self.state in ("ESTABLISHED", "CLOSE_WAIT")):
            self.fin_sent = True
            self.fin_seq = self.snd_nxt
            self._out(FIN | ACK, self.snd_nxt)
            self.snd_nxt = (self.snd_nxt + 1) & M32
            self.state = "FIN_WAIT_1" if self.state == "ESTABLISHED" else "LAST_ACK"
        self._arm()

    # -- active open ------------------------------------------------------

    def connect(self):
        self.state = "SYN_SENT"
        self._out(SYN, self.iss, opts=struct.pack(">BBH", 2, 4, 1460))
        self.snd_nxt = (self.iss + 1) & M32
        self.rto_at = time.time() + self.RTO

    # -- input ------------------------------------------------------------

    def recv(self, seq, ack, flags, wnd, payload, opts):
        if flags & RST:
            self._done()
            return

        if flags & SYN:
            for t, v in opts:
                if t == 2 and len(v) == 2:
                    self.mss = min(struct.unpack(">H", v)[0], self.stack.mtu - 40)

        if self.state == "SYN_SENT":
            if (flags & SYN) and (flags & ACK):
                if ack != ((self.iss + 1) & M32):
                    self._out(RST)
                    self._done()
                    return
                self.rcv_nxt = (seq + 1) & M32
                self.snd_una = ack
                self.snd_wnd = wnd
                self.state = "ESTABLISHED"
                self.rto_at = 0.0
                self._out(ACK)
                self._cb("on_open")
                self._pump()
            return

        if self.state == "SYN_RCVD":
            if flags & ACK:
                self.snd_una = ack
                self.snd_wnd = wnd
                self.state = "ESTABLISHED"
                self.rto_at = 0.0
                self._cb("on_open")
            else:
                return

        if flags & ACK:
            if _gt(ack, self.snd_una) and _le(ack, self.snd_nxt):
                acked = (ack - self.snd_una) & M32
                consumed = acked
                if self.fin_seq is not None and _gt(ack, self.fin_seq):
                    consumed -= 1  # the FIN takes a sequence number, not a byte
                if consumed > 0:
                    del self.sndbuf[:consumed]
                self.snd_una = ack
                self.retries = 0
                self.rto_at = 0.0
                if self.fin_seq is not None and _ge(ack, (self.fin_seq + 1) & M32):
                    if self.state == "FIN_WAIT_1":
                        self.state = "FIN_WAIT_2"
                    elif self.state == "LAST_ACK":
                        self._done()
                        return
            self.snd_wnd = wnd

        if payload:
            if seq == self.rcv_nxt:
                self.rcv_nxt = (self.rcv_nxt + len(payload)) & M32
                self._out(ACK)
                self._cb("on_data", payload)
            else:
                self._out(ACK)  # duplicate ack; make it retransmit

        if flags & FIN:
            if seq == self.rcv_nxt or _le(seq, self.rcv_nxt):
                self.rcv_nxt = (self.rcv_nxt + 1) & M32
                self._out(ACK)
                if self.state == "ESTABLISHED":
                    self.state = "CLOSE_WAIT"
                    self._cb("on_data", b"")  # EOF marker for the handler
                    self.close()
                elif self.state in ("FIN_WAIT_1", "FIN_WAIT_2"):
                    self._done()
                    return

        self._pump()

    # -- timer ------------------------------------------------------------

    def tick(self, now):
        if self.rto_at and now >= self.rto_at:
            self.retries += 1
            if self.retries > self.MAX_RETRY:
                self.stack.log("tcp: %s:%d gave up after %d retries"
                               % (ip2s(self.raddr), self.rport, self.retries))
                self.abort()
                return
            if self.state == "SYN_SENT":
                self._out(SYN, self.iss, opts=struct.pack(">BBH", 2, 4, 1460))
            elif self.state == "SYN_RCVD":
                self._out(SYN | ACK, self.iss, opts=struct.pack(">BBH", 2, 4, 1460))
            else:
                self.snd_nxt = self.snd_una  # resend from the top of the window
                if self.fin_sent:
                    self.fin_sent = False
                    self.fin_seq = None
                self._pump()
            self.rto_at = now + self.RTO * (2 ** min(self.retries, 4))


# -------------------------------------------------------------------- stack

class IpStack:
    """IPv4 over one PPP link: ICMP echo, UDP dispatch, and TCP."""

    def __init__(self, send_frame, log, mtu=1500):
        self._send_frame = send_frame
        self.log = log
        self.mtu = mtu

        self.tcp_listeners = {}  # port -> factory()
        self.udp_listeners = {}  # port -> fn(stack, laddr, lport, raddr, rport, data)
        self.conns = {}  # key -> TcpSocket
        self._ipid = 1

    # -- transmit ---------------------------------------------------------

    def send_ip(self, src, dst, proto, payload):
        self._ipid = (self._ipid + 1) & 0xFFFF
        hdr = struct.pack(">BBHHHBBH", 0x45, 0, 20 + len(payload), self._ipid,
                          0x4000, 64, proto, 0) + src + dst
        ck = checksum(hdr)
        hdr = hdr[:10] + struct.pack(">H", ck) + hdr[12:]
        self._send_frame(hdr + payload)

    def send_udp(self, src, sport, dst, dport, data):
        seg = struct.pack(">HHHH", sport, dport, len(data) + 8, 0) + data
        ck = checksum(_pseudo(src, dst, 17, len(seg)) + seg)
        seg = seg[:6] + struct.pack(">H", ck or 0xFFFF) + seg[8:]
        self.send_ip(src, dst, 17, seg)

    # -- registration -----------------------------------------------------

    def tcp_listen(self, port, factory):
        self.tcp_listeners[port] = factory

    def udp_listen(self, port, fn):
        self.udp_listeners[port] = fn

    def tcp_connect(self, laddr, lport, raddr, rport, handler):
        sock = TcpSocket(self, laddr, lport, raddr, rport, handler)
        self.conns[sock.key] = sock
        sock.connect()
        return sock

    # -- receive ----------------------------------------------------------

    def on_packet(self, pkt):
        if len(pkt) < 20 or (pkt[0] >> 4) != 4:
            return
        ihl = (pkt[0] & 0x0F) * 4
        if ihl < 20 or len(pkt) < ihl:
            return
        total = struct.unpack(">H", pkt[2:4])[0]
        if 20 <= total <= len(pkt):
            pkt = pkt[:total]
        frag = struct.unpack(">H", pkt[6:8])[0]
        if frag & 0x1FFF:
            return  # a fragment; nothing here should produce one
        proto = pkt[9]
        src, dst = pkt[12:16], pkt[16:20]
        body = pkt[ihl:]

        if proto == 1:
            self._icmp(src, dst, body)
        elif proto == 17:
            self._udp(src, dst, body)
        elif proto == 6:
            self._tcp(src, dst, body)

    def _icmp(self, src, dst, body):
        if len(body) < 8 or body[0] != 8:
            return
        rep = bytearray(body)
        rep[0] = 0
        rep[2:4] = b"\x00\x00"
        ck = checksum(bytes(rep))
        rep[2:4] = struct.pack(">H", ck)
        self.send_ip(dst, src, 1, bytes(rep))

    def _udp(self, src, dst, body):
        if len(body) < 8:
            return
        sport, dport, ln = struct.unpack(">HHH", body[:6])
        data = body[8:ln] if 8 <= ln <= len(body) else body[8:]
        fn = self.udp_listeners.get(dport)
        if fn is not None:
            fn(self, dst, dport, src, sport, data)

    def _tcp(self, src, dst, body):
        if len(body) < 20:
            return
        (sport, dport, seq, ack, off,
         flags, wnd) = struct.unpack(">HHIIBBH", body[:16])
        hlen = (off >> 4) * 4
        if hlen < 20 or hlen > len(body):
            return
        opts = self._tcp_opts(body[20:hlen])
        payload = body[hlen:]

        key = (dst, dport, src, sport)
        sock = self.conns.get(key)

        if sock is None:
            if flags & SYN and not (flags & ACK):
                factory = self.tcp_listeners.get(dport)
                if factory is None:
                    self._reset(dst, dport, src, sport, seq, len(payload), flags)
                    return
                sock = TcpSocket(self, dst, dport, src, sport, factory())
                sock.state = "SYN_RCVD"
                sock.rcv_nxt = (seq + 1) & M32
                sock.snd_wnd = wnd
                for t, v in opts:
                    if t == 2 and len(v) == 2:
                        sock.mss = min(struct.unpack(">H", v)[0], self.mtu - 40)
                self.conns[key] = sock
                sock._out(SYN | ACK, sock.iss, opts=struct.pack(">BBH", 2, 4, 1460))
                sock.snd_nxt = (sock.iss + 1) & M32
                sock.rto_at = time.time() + sock.RTO
            elif not (flags & RST):
                self._reset(dst, dport, src, sport, seq, len(payload), flags)
            return

        if sock.state == "CLOSED":
            return
        sock.recv(seq, ack, flags, wnd, payload, opts)

    @staticmethod
    def _tcp_opts(raw):
        out = []
        i = 0
        while i < len(raw):
            t = raw[i]
            if t == 0:
                break
            if t == 1:
                i += 1
                continue
            if i + 1 >= len(raw):
                break
            ln = raw[i + 1]
            if ln < 2 or i + ln > len(raw):
                break
            out.append((t, raw[i + 2:i + ln]))
            i += ln
        return out

    def _reset(self, laddr, lport, raddr, rport, seq, plen, flags):
        if flags & RST:
            return
        ack = (seq + plen + (1 if flags & SYN else 0)) & M32
        hdr = struct.pack(">HHIIBBHHH", lport, rport, 0, ack, 5 << 4,
                          RST | ACK, 0, 0, 0)
        ck = checksum(_pseudo(laddr, raddr, 6, len(hdr)) + hdr)
        hdr = hdr[:16] + struct.pack(">H", ck) + hdr[18:]
        self.send_ip(laddr, raddr, 6, hdr)

    # -- timers -----------------------------------------------------------

    def tick(self):
        now = time.time()
        for key, sock in list(self.conns.items()):
            if sock.dead_at is not None:
                if now >= sock.dead_at:
                    del self.conns[key]
                continue
            sock.tick(now)
