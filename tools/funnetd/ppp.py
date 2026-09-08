"""PPP for the fun.net stand-in: HDLC framing, LCP, PAP, CHAP and IPCP.

The cabinet's link stack is Novell ODI -- LSL.COM, a PPP ODI driver configured
by \\FN_SYS\\DFU\\NET.CFG, then WATTCP on top.  Three things in that file decide
the shape of this module:

    OPEN PASSIVE        the cabinet waits to be spoken to.  *We* send the first
                        LCP Configure-Request, and keep resending it until it
                        is answered, or nothing ever happens.

    PCOMP ON            it asks to compress the address/control and protocol
    ACCOMP ON           fields on the frames it sends.  We agree, so the
                        receive path copes with both forms.  We never ask for
                        it ourselves, so our own frames stay full width.

    USER "funworld" "<obscured>"
                        it has credentials and will use them if challenged.  We
                        do not challenge by default -- there is nobody to
                        authenticate against -- but PAP and CHAP are both
                        answered if the cabinet insists, and whatever it sends
                        is logged, because the NET.CFG password is obscured on
                        disk and PAP puts it on the wire in the clear.

Nothing here is 86Box-specific: it speaks to a byte pipe, which is what
char_modem.c's TCP line gives us once the guest dials.
"""

import os
import random
import struct
import time

# ------------------------------------------------------------------ framing

FLAG = 0x7E
ESC = 0x7D

GOOD_FCS = 0xF0B8


def _fcstab():
    tab = []
    for b in range(256):
        v = b
        for _ in range(8):
            v = ((v >> 1) ^ 0x8408) if (v & 1) else (v >> 1)
        tab.append(v)
    return tab


FCSTAB = _fcstab()


def fcs16(data, fcs=0xFFFF):
    """RFC 1662 FCS-16, the reversed CRC-CCITT."""
    for b in data:
        fcs = (fcs >> 8) ^ FCSTAB[(fcs ^ b) & 0xFF]
    return fcs


class Hdlc:
    """Async HDLC-like framing: flag delimited, byte stuffed, FCS-16.

    `tx_accm` is the map the *peer* asked us to honour -- the control
    characters it cannot receive raw.  Until it says otherwise the default is
    every one of them, which is what RFC 1662 requires.
    """

    def __init__(self):
        self.rx = bytearray()
        self.tx_accm = 0xFFFFFFFF
        self.bad = 0
        self.short = 0

    def feed(self, data):
        """Push received bytes, yield complete de-stuffed frames, FCS stripped."""
        for b in data:
            if b == FLAG:
                frame = bytes(self.rx)
                self.rx = bytearray()
                if not frame:
                    continue  # back-to-back flags, or a leading one
                if len(frame) < 4:
                    self.short += 1
                    continue
                out = bytearray()
                esc = False
                for c in frame:
                    if esc:
                        out.append(c ^ 0x20)
                        esc = False
                    elif c == ESC:
                        esc = True
                    else:
                        out.append(c)
                if esc or len(out) < 4:
                    self.short += 1
                    continue
                if fcs16(out) != GOOD_FCS:
                    self.bad += 1
                    continue
                yield bytes(out[:-2])
            else:
                self.rx.append(b)
                if len(self.rx) > 4096:  # a lost flag; resynchronise
                    self.rx = bytearray()

    def frame(self, payload):
        """Wrap one assembled PPP frame (address, control, protocol, data)."""
        body = bytearray(payload)
        f = fcs16(bytes(body)) ^ 0xFFFF
        body += bytes((f & 0xFF, (f >> 8) & 0xFF))

        out = bytearray((FLAG,))
        for b in body:
            if b == FLAG or b == ESC or (b < 0x20 and (self.tx_accm >> b) & 1):
                out.append(ESC)
                out.append(b ^ 0x20)
            else:
                out.append(b)
        out.append(FLAG)
        return bytes(out)


# ---------------------------------------------------------------- protocols

P_IP = 0x0021
P_IPCP = 0x8021
P_LCP = 0xC021
P_PAP = 0xC023
P_CHAP = 0xC223

CONF_REQ, CONF_ACK, CONF_NAK, CONF_REJ = 1, 2, 3, 4
TERM_REQ, TERM_ACK, CODE_REJ, PROTO_REJ = 5, 6, 7, 8
ECHO_REQ, ECHO_REP, DISCARD = 9, 10, 11

LCP_MRU, LCP_ACCM, LCP_AUTH, LCP_MAGIC, LCP_PFC, LCP_ACFC = 1, 2, 3, 5, 7, 8

IPCP_COMP, IPCP_ADDR = 2, 3
IPCP_DNS1, IPCP_NBNS1, IPCP_DNS2, IPCP_NBNS2 = 129, 130, 131, 132


def parse_opts(data):
    """[(type, value), ...]; stops cleanly on a malformed tail."""
    out = []
    i = 0
    while i + 2 <= len(data):
        t, ln = data[i], data[i + 1]
        if ln < 2 or i + ln > len(data):
            break
        out.append((t, data[i + 2:i + ln]))
        i += ln
    return out


def build_opts(opts):
    out = bytearray()
    for t, v in opts:
        out.append(t)
        out.append(len(v) + 2)
        out += v
    return bytes(out)


def ip2s(b):
    return ".".join(str(x) for x in b)


def s2ip(s):
    return bytes(int(x) for x in s.split("."))


# ----------------------------------------------------------------------- PPP

class Ppp:
    """One PPP session over one byte pipe.

    Feed it received bytes; it calls `on_ip(packet)` for every IPv4 datagram
    and `on_up(local_ip, peer_ip)` once IPCP has settled.  `send_ip()` goes the
    other way.  Everything it transmits is handed to `write(bytes)`.
    """

    RESTART = 3.0  # seconds between Configure-Request retries
    MAX_TRY = 12

    def __init__(self, write, log, local_ip, peer_ip, dns_ip,
                 accm=0x00000000, mru=1500, auth=None):
        self.write = write
        self.log = log
        self.hdlc = Hdlc()

        self.local_ip = s2ip(local_ip)
        self.peer_ip = s2ip(peer_ip)
        self.dns_ip = s2ip(dns_ip)
        self.want_accm = accm
        self.mru = mru
        self.auth = auth  # None, "pap" or "chap" -- what we demand

        self.magic = random.getrandbits(32)
        self.peer_pfc = False
        self.peer_acfc = False

        self.lcp_local = False  # our Configure-Request has been Acked
        self.lcp_peer = False  # we have Acked theirs
        self.lcp_up = False
        self.authed = (auth is None)
        self.ipcp_local = False
        self.ipcp_peer = False
        self.up = False
        self.dead = False

        self._id = 0
        self._lcp_id = 0
        self._ipcp_id = 0
        self._lcp_try = 0
        self._ipcp_try = 0
        self._lcp_at = 0.0
        self._ipcp_at = 0.0
        self._chap_id = 0
        self._chap_ch = b""

        self.on_ip = lambda pkt: None
        self.on_up = lambda a, b: None

        self.peer_user = None
        self.peer_pass = None

    # -- plumbing ---------------------------------------------------------

    def _next_id(self):
        self._id = (self._id + 1) & 0xFF
        return self._id

    def _send(self, proto, payload):
        self.write(self.hdlc.frame(b"\xff\x03" + struct.pack(">H", proto) + payload))

    def _cp(self, proto, code, ident, data=b""):
        self._send(proto, struct.pack(">BBH", code, ident, len(data) + 4) + data)

    # -- receive ----------------------------------------------------------

    def feed(self, data):
        for frame in self.hdlc.feed(data):
            self._frame(frame)

    def _frame(self, f):
        i = 0
        if len(f) >= 2 and f[0] == 0xFF and f[1] == 0x03:
            i = 2  # uncompressed address/control
        if i >= len(f):
            return
        if f[i] & 1:  # compressed protocol field
            proto = f[i]
            i += 1
        else:
            if i + 2 > len(f):
                return
            proto = (f[i] << 8) | f[i + 1]
            i += 2
        body = f[i:]

        if proto == P_IP:
            self.on_ip(body)
        elif proto == P_LCP:
            self._lcp(body)
        elif proto == P_IPCP:
            self._ipcp(body)
        elif proto == P_PAP:
            self._pap(body)
        elif proto == P_CHAP:
            self._chap(body)
        else:
            self.log("ppp: rejecting protocol 0x%04x" % proto)
            self._cp(P_LCP, PROTO_REJ, self._next_id(),
                     struct.pack(">H", proto) + body[:64])

    # -- LCP --------------------------------------------------------------

    def start(self):
        self._send_lcp_req()

    def _send_lcp_req(self):
        opts = [(LCP_MRU, struct.pack(">H", self.mru)),
                (LCP_ACCM, struct.pack(">I", self.want_accm)),
                (LCP_MAGIC, struct.pack(">I", self.magic))]
        if self.auth == "pap":
            opts.append((LCP_AUTH, struct.pack(">H", P_PAP)))
        elif self.auth == "chap":
            opts.append((LCP_AUTH, struct.pack(">H", P_CHAP) + b"\x05"))
        self._lcp_id = self._next_id()
        self._lcp_at = time.time()
        self._lcp_try += 1
        self._cp(P_LCP, CONF_REQ, self._lcp_id, build_opts(opts))
        self.log("lcp: Configure-Request #%d (MRU %d, ACCM 0x%08x)"
                 % (self._lcp_try, self.mru, self.want_accm))

    def _lcp(self, body):
        if len(body) < 4:
            return
        code, ident, ln = struct.unpack(">BBH", body[:4])
        data = body[4:ln] if ln >= 4 else b""

        if code == CONF_REQ:
            self._lcp_conf_req(ident, data)
        elif code == CONF_ACK:
            if not self.lcp_local:
                self.lcp_local = True
                self.log("lcp: our request acked")
                self._maybe_lcp_up()
        elif code in (CONF_NAK, CONF_REJ):
            # The only things we ask for are harmless; drop whatever it
            # dislikes and try once more rather than arguing.
            self.log("lcp: peer %s our options -- retrying without them"
                     % ("naked" if code == CONF_NAK else "rejected"))
            objected = {t for t, _ in parse_opts(data)}
            if LCP_ACCM in objected:
                self.want_accm = 0xFFFFFFFF
            if LCP_AUTH in objected:
                self.auth, self.authed = None, True
            self._send_lcp_req()
        elif code == TERM_REQ:
            self.log("lcp: peer hung up")
            self._cp(P_LCP, TERM_ACK, ident, data)
            self.dead = True
        elif code == TERM_ACK:
            self.dead = True
        elif code == ECHO_REQ:
            self._cp(P_LCP, ECHO_REP, ident, struct.pack(">I", self.magic) + data[4:])
        elif code == CODE_REJ:
            self.log("lcp: peer rejected one of our codes")

    def _lcp_conf_req(self, ident, data):
        opts = parse_opts(data)
        rej = []
        pfc = acfc = False
        accm = None

        for t, v in opts:
            if t == LCP_MRU:
                pass  # any MRU is fine
            elif t == LCP_ACCM and len(v) == 4:
                accm = struct.unpack(">I", v)[0]
            elif t == LCP_MAGIC:
                pass
            elif t == LCP_PFC:
                pfc = True
            elif t == LCP_ACFC:
                acfc = True
            else:
                # LCP_AUTH lands here too: it would be asking *us* to
                # authenticate, and there is no account to use.
                rej.append((t, v))

        if rej:
            self._cp(P_LCP, CONF_REJ, ident, build_opts(rej))
            self.log("lcp: rejected %d option(s) from peer" % len(rej))
            return

        self._cp(P_LCP, CONF_ACK, ident, data)
        if accm is not None:
            self.hdlc.tx_accm = accm
        self.peer_pfc, self.peer_acfc = pfc, acfc
        if not self.lcp_peer:
            self.lcp_peer = True
            self.log("lcp: acked peer request (ACCM 0x%08x%s%s)"
                     % (self.hdlc.tx_accm,
                        ", PFC" if pfc else "", ", ACFC" if acfc else ""))
            self._maybe_lcp_up()

    def _maybe_lcp_up(self):
        if self.lcp_up or not (self.lcp_local and self.lcp_peer):
            return
        self.lcp_up = True
        self.log("lcp: up")
        if self.auth == "chap":
            self._chap_challenge()
        elif self.authed:
            self._start_ipcp()

    # -- authentication ---------------------------------------------------

    def _pap(self, body):
        if len(body) < 4:
            return
        code, ident, ln = struct.unpack(">BBH", body[:4])
        if code != 1:
            return
        data = body[4:ln]
        try:
            ul = data[0]
            user = data[1:1 + ul].decode("latin-1")
            pl = data[1 + ul]
            pw = data[2 + ul:2 + ul + pl].decode("latin-1")
        except IndexError:
            user = pw = "?"
        self.peer_user, self.peer_pass = user, pw
        self.log("pap: cabinet authenticated as %r / %r -- accepted" % (user, pw))
        msg = b"fun.net"
        self._cp(P_PAP, 2, ident, bytes((len(msg),)) + msg)
        self.authed = True
        self._start_ipcp()

    def _chap_challenge(self):
        self._chap_id = self._next_id()
        self._chap_ch = os.urandom(16)
        payload = bytes((len(self._chap_ch),)) + self._chap_ch + b"fun.net"
        self._cp(P_CHAP, 1, self._chap_id, payload)
        self.log("chap: challenge sent")

    def _chap(self, body):
        if len(body) < 4:
            return
        code, ident, ln = struct.unpack(">BBH", body[:4])
        data = body[4:ln]
        if code == 2:  # Response
            try:
                vl = data[0]
                name = data[1 + vl:].decode("latin-1")
            except IndexError:
                name = "?"
            self.peer_user = name
            self.log("chap: cabinet answered as %r -- accepted" % name)
            self._cp(P_CHAP, 3, ident, b"ok")
            self.authed = True
            self._start_ipcp()
        elif code == 1:  # it challenged us
            self.log("chap: peer challenged us; answering with a null hash")
            self._cp(P_CHAP, 2, ident, b"\x10" + b"\x00" * 16 + b"funnet")

    # -- IPCP -------------------------------------------------------------

    def _start_ipcp(self):
        if self._ipcp_try == 0:
            self._send_ipcp_req()

    def _send_ipcp_req(self):
        self._ipcp_id = self._next_id()
        self._ipcp_at = time.time()
        self._ipcp_try += 1
        self._cp(P_IPCP, CONF_REQ, self._ipcp_id,
                 build_opts([(IPCP_ADDR, self.local_ip)]))
        self.log("ipcp: Configure-Request #%d (our address %s)"
                 % (self._ipcp_try, ip2s(self.local_ip)))

    def _ipcp(self, body):
        if len(body) < 4:
            return
        code, ident, ln = struct.unpack(">BBH", body[:4])
        data = body[4:ln] if ln >= 4 else b""

        if code == CONF_REQ:
            self._ipcp_conf_req(ident, data)
        elif code == CONF_ACK:
            if not self.ipcp_local:
                self.ipcp_local = True
                self.log("ipcp: our address acked")
                self._maybe_up()
        elif code == CONF_NAK:
            for t, v in parse_opts(data):
                if t == IPCP_ADDR and len(v) == 4:
                    self.log("ipcp: peer wants us at %s -- taking it" % ip2s(v))
                    self.local_ip = v
            self._send_ipcp_req()
        elif code == CONF_REJ:
            self.log("ipcp: peer rejected our address option")
            self.ipcp_local = True
            self._maybe_up()
        elif code == TERM_REQ:
            self._cp(P_IPCP, TERM_ACK, ident, data)

    def _ipcp_conf_req(self, ident, data):
        opts = parse_opts(data)
        nak, rej = [], []

        for t, v in opts:
            if t == IPCP_ADDR and len(v) == 4:
                if v != self.peer_ip:
                    # 0.0.0.0 means "tell me"; anything else that is not what
                    # we hand out gets corrected the same way.
                    nak.append((t, self.peer_ip))
            elif t == IPCP_COMP:
                # NET.CFG says TCPIPCOMP 16, so it asks for Van Jacobson.  We
                # do not implement it, and a Configure-Reject is the documented
                # way to say so; the ODI driver falls back to plain IP.
                rej.append((t, v))
            elif t in (IPCP_DNS1, IPCP_DNS2):
                if len(v) != 4 or v != self.dns_ip:
                    nak.append((t, self.dns_ip))
            else:
                rej.append((t, v))

        if rej:
            self._cp(P_IPCP, CONF_REJ, ident, build_opts(rej))
            self.log("ipcp: rejected %s"
                     % ", ".join("option %d" % t for t, _ in rej))
            return
        if nak:
            self._cp(P_IPCP, CONF_NAK, ident, build_opts(nak))
            self.log("ipcp: naked -- offering %s" % ip2s(self.peer_ip))
            return

        self._cp(P_IPCP, CONF_ACK, ident, data)
        if not self.ipcp_peer:
            self.ipcp_peer = True
            self.log("ipcp: acked peer, it is %s" % ip2s(self.peer_ip))
            self._maybe_up()

    def _maybe_up(self):
        if self.up or not (self.ipcp_local and self.ipcp_peer):
            return
        self.up = True
        self.log("ipcp: up -- %s <-> %s" % (ip2s(self.local_ip), ip2s(self.peer_ip)))
        self.on_up(ip2s(self.local_ip), ip2s(self.peer_ip))

    # -- transmit ---------------------------------------------------------

    def send_ip(self, packet):
        if self.up:
            self._send(P_IP, packet)

    # -- timers -----------------------------------------------------------

    def tick(self):
        now = time.time()
        if not self.lcp_local and self._lcp_try and (now - self._lcp_at) > self.RESTART:
            if self._lcp_try >= self.MAX_TRY:
                self.log("lcp: no answer after %d requests -- giving up" % self._lcp_try)
                self.dead = True
            else:
                self._send_lcp_req()
        if (self.lcp_up and self.authed and not self.ipcp_local
                and self._ipcp_try and (now - self._ipcp_at) > self.RESTART):
            if self._ipcp_try >= self.MAX_TRY:
                self.log("ipcp: no answer after %d requests -- giving up" % self._ipcp_try)
                self.dead = True
            else:
                self._send_ipcp_req()

    def terminate(self):
        if not self.dead:
            self._cp(P_LCP, TERM_REQ, self._next_id(), b"funnetd closing")
            self.dead = True
