"""The two small services the cabinet needs before it can do anything: a name
server and a clock.

`CLIENT.EXE` resolves two names -- whatever `ftpcli_ftpdomain` and
`ftpcli_ntpdomain` hold in \\FN_SYS\\DATABASE\\USER\\SETTINGS.TAB.  On the
MASTERS machine those are `ftp.nl.funsys.com` and `time.nl.funsys.com`; other
territories differ only in the country label, and there is a hard-coded backup
of `ftp.backup.funnet.cc` in the binary.  None of them resolve any more, so the
name server here answers *every* name with the one address there is.

The clock is asked for first -- "connecting Time-Server" comes before
"connecting fun.net server" in the client's own log strings -- and what it
answers matters more than it looks.  A net game carries a validity window
(NETGAME.TAB on the MASTERS machine holds NETQUIZ1, 2002-05-27 to 2002-06-30),
so a cabinet whose clock says 2026 will fetch a game and then refuse to show
it.  `--date` exists for that.

Which time protocol the client speaks is not settled: the strings name a
"Time-Server" and an `ftpcli_ntpoffset`, but no port number is legible.  All
three plausible answers are served -- RFC 868 on UDP/37 and TCP/37, and SNTP on
UDP/123 -- which costs a few dozen lines and removes the question.
"""

import struct
import time

# 1900-01-01 to 1970-01-01, the epoch both RFC 868 and NTP count from.
EPOCH_1900 = 2208988800


class Clock:
    """Wall clock, optionally displaced to some other date.

    It advances in real time from wherever it was set, so a session that takes
    ten minutes sees ten minutes pass.
    """

    def __init__(self, at=None):
        self.skew = 0.0 if at is None else (at - time.time())

    def now(self):
        return time.time() + self.skew

    def stamp(self):
        return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(self.now()))


# ----------------------------------------------------------------------- DNS

def _name(msg, off):
    """Read a (possibly compressed) name; return (labels, next_offset)."""
    labels = []
    jumped = False
    end = off
    hops = 0
    while off < len(msg):
        ln = msg[off]
        if ln == 0:
            off += 1
            if not jumped:
                end = off
            break
        if (ln & 0xC0) == 0xC0:
            if off + 1 >= len(msg):
                break
            ptr = ((ln & 0x3F) << 8) | msg[off + 1]
            if not jumped:
                end = off + 2
            off = ptr
            jumped = True
            hops += 1
            if hops > 16:
                break
            continue
        off += 1
        labels.append(msg[off:off + ln].decode("latin-1"))
        off += ln
    return ".".join(labels), end


class Dns:
    """Answers A queries for anything with one address."""

    def __init__(self, addr, log):
        self.addr = addr  # bytes, 4
        self.log = log
        self.seen = set()

    def __call__(self, stack, laddr, lport, raddr, rport, data):
        if len(data) < 12:
            return
        ident, flags, qd = struct.unpack(">HHH", data[:6])
        if flags & 0x8000 or qd < 1:
            return

        name, off = _name(data, 12)
        if off + 4 > len(data):
            return
        qtype, qclass = struct.unpack(">HH", data[off:off + 4])
        off += 4
        question = data[12:off]

        if name not in self.seen:
            self.seen.add(name)
            self.log("dns: %s -> %s" % (name, ".".join(str(b) for b in self.addr)))

        if qtype == 1 and qclass == 1:
            answer = (b"\xc0\x0c" + struct.pack(">HHIH", 1, 1, 60, 4) + self.addr)
            an = 1
        else:
            # Not an address question.  Say so politely rather than lying: the
            # client only ever asks for A records.
            answer = b""
            an = 0

        head = struct.pack(">HHHHHH", ident, 0x8180, 1, an, 0, 0)
        stack.send_udp(laddr, lport, raddr, rport, head + question + answer)


# --------------------------------------------------------------------- time

class TimeUdp:
    """RFC 868 over UDP: any datagram gets four bytes back."""

    def __init__(self, clock, log):
        self.clock = clock
        self.log = log

    def __call__(self, stack, laddr, lport, raddr, rport, data):
        secs = int(self.clock.now()) + EPOCH_1900
        self.log("time: RFC 868/udp -> %s" % self.clock.stamp())
        stack.send_udp(laddr, lport, raddr, rport, struct.pack(">I", secs & 0xFFFFFFFF))


class TimeTcp:
    """RFC 868 over TCP: four bytes on connect, then close."""

    def __init__(self, clock, log):
        self.clock = clock
        self.log = log

    def on_open(self, sock):
        secs = int(self.clock.now()) + EPOCH_1900
        self.log("time: RFC 868/tcp -> %s" % self.clock.stamp())
        sock.send(struct.pack(">I", secs & 0xFFFFFFFF))
        sock.close()

    def on_data(self, sock, data):
        pass

    def on_close(self, sock):
        pass


class Sntp:
    """SNTP/NTP over UDP/123, server mode, stratum 1."""

    def __init__(self, clock, log):
        self.clock = clock
        self.log = log

    @staticmethod
    def _ts(t):
        secs = int(t) + EPOCH_1900
        frac = int((t - int(t)) * (1 << 32))
        return struct.pack(">II", secs & 0xFFFFFFFF, frac & 0xFFFFFFFF)

    def __call__(self, stack, laddr, lport, raddr, rport, data):
        if len(data) < 48:
            return
        now = self.clock.now()
        self.log("time: SNTP -> %s" % self.clock.stamp())
        vn = (data[0] >> 3) & 7 or 3
        pkt = bytearray(48)
        pkt[0] = (0 << 6) | (vn << 3) | 4  # no leap, same version, server
        pkt[1] = 1  # stratum 1
        pkt[2] = data[2] or 6  # keep the client's poll
        pkt[3] = 0xEC  # precision, ~2^-20 s
        pkt[12:16] = b"FUNN"  # reference identifier
        pkt[16:24] = self._ts(now)  # reference
        pkt[24:32] = data[40:48]  # originate = client transmit
        pkt[32:40] = self._ts(now)  # receive
        pkt[40:48] = self._ts(now)  # transmit
        stack.send_udp(laddr, lport, raddr, rport, bytes(pkt))
