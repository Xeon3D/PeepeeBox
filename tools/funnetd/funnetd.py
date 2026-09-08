r"""funnetd -- a stand-in for fun.net, for emulated Photo Play cabinets to dial.

    python funnetd.py

Then point the emulated modem at it.  In the machine's 86Box ini:

    [Photo Play]
    modem = pp_modem

    [Modem]
    line = 1
    host = 127.0.0.1
    host_port = 23

or the same thing through Tools -> Modem... in the PeepeeBox UI.  Any number
dials here; the cabinet's own NET.CFG number is ignored, because there is only
one place to go.

What happens when a cabinet dials
---------------------------------

`\FN_SYS\DFU\TRANSMIT.BAT` is the whole session, and it is short:

    LSL / PPP / IPSTUB          load the ODI stack
    PPPMENU /CONNECT            dial, and negotiate PPP
    PPPSTATE WAIT=30 LCP        wait for the link
    PPPSTATE WAIT=30 IP         wait for an address
    PPPWAT                      hand the address to WATTCP
    CLIENT                      do the actual work over FTP

So this program is four things stacked up: a PPP peer (ppp.py), an IP stack
that answers for every address on the link (ipstack.py), a name server and a
clock (services.py), and an FTP server (ftpd.py).  The cabinet needs all four
before it will say a word.

What it can and cannot do
-------------------------

Everything up to and including the FTP dialogue is complete: the cabinet
authenticates, gets an address, resolves `ftp.<cc>.funsys.com`, sets its clock,
logs in, and reads and writes files.  Uploads are kept.

What is *not* here is the content of the scripts fun.net used to send back --
`/master/outgoing/machine/...`, which the client saves as SCRIPT.DL and parses
for records named MACHLIC, DATETIME, CONFIRM, UPDATE, MSTMAIN, NEWSPAGE and
forty more.  That grammar is not published and cannot be guessed from the
binary's string table alone.  The way to learn it is the upload: the cabinet
builds SCRIPT.UL out of the same vocabulary and STORs it, so one completed
session leaves a real sample in the capture directory.  Until then every RETR
that has no file behind it is answered 550, which the client treats as "nothing
for me today" and moves on.

The closing report of every session lists each path the cabinet asked for and
did not get.  That list is the specification for whatever goes in the tree
next.
"""

import argparse
import errno
import os
import select
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import ftpd  # noqa: E402
import ipstack  # noqa: E402
import ppp  # noqa: E402
import services  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class Log:
    def __init__(self, path=None, quiet=False):
        self.fh = open(path, "a", encoding="utf-8") if path else None
        self.quiet = quiet
        self.tag = ""

    def __call__(self, msg):
        line = "%s %s%s" % (time.strftime("%H:%M:%S"), self.tag, msg)
        if not self.quiet:
            print(line, flush=True)
        if self.fh:
            self.fh.write(line + "\n")
            self.fh.flush()


class Session:
    """One dialled-in cabinet: a socket, a PPP peer, and a stack above it."""

    def __init__(self, sock, addr, cfg, log, n):
        self.sock = sock
        self.addr = addr
        self.cfg = cfg
        self.log = log
        self.n = n
        self.started = time.time()
        self.closed = False
        self.outbuf = bytearray()
        self.raw = None

        if cfg.raw:
            os.makedirs(cfg.raw, exist_ok=True)
            self.raw = open(os.path.join(
                cfg.raw, time.strftime("session-%Y%m%d-%H%M%S.bin")), "wb")

        sock.setblocking(False)

        self.ppp = ppp.Ppp(self._write, self._log,
                           local_ip=cfg.server_ip, peer_ip=cfg.client_ip,
                           dns_ip=cfg.dns_ip, accm=cfg.accm, mru=cfg.mru,
                           auth=None if cfg.auth == "none" else cfg.auth)

        self.stack = ipstack.IpStack(self._send_ip, self._log, mtu=cfg.mru)
        self.ppp.on_ip = self.stack.on_packet
        self.ppp.on_up = self._up

        clock = services.Clock(cfg.at)
        addr_b = ipstack.s2ip(cfg.dns_ip)
        self.stack.udp_listen(53, services.Dns(addr_b, self._log))
        self.stack.udp_listen(37, services.TimeUdp(clock, self._log))
        self.stack.udp_listen(123, services.Sntp(clock, self._log))
        self.stack.tcp_listen(37, lambda: services.TimeTcp(clock, self._log))

        self.ftp = ftpd.FtpServer(self.stack, cfg.root, cfg.captures, self._log)
        self.stack.tcp_listen(21, self.ftp.factory)

        self._log("carrier up from %s:%d" % addr)
        self.ppp.start()

    # -- logging ----------------------------------------------------------

    def _log(self, msg):
        self.log("[%d] %s" % (self.n, msg))

    # -- byte pipe --------------------------------------------------------

    def _write(self, data):
        self.outbuf += data
        self._flush()

    def _flush(self):
        while self.outbuf:
            try:
                n = self.sock.send(bytes(self.outbuf[:4096]))
            except socket.error as e:
                if e.errno in (errno.EWOULDBLOCK, errno.EAGAIN):
                    return
                self.close()
                return
            if n <= 0:
                return
            if self.raw:
                self.raw.write(b">" + bytes(self.outbuf[:n]))
            del self.outbuf[:n]

    def _send_ip(self, packet):
        self.ppp.send_ip(packet)

    def _up(self, local, peer):
        self._log("link ready: cabinet is %s, we are %s (and every other "
                  "address on the link)" % (peer, local))

    # -- pump -------------------------------------------------------------

    def readable(self):
        try:
            data = self.sock.recv(4096)
        except socket.error as e:
            if e.errno in (errno.EWOULDBLOCK, errno.EAGAIN):
                return
            data = b""
        if not data:
            self.close()
            return
        if self.raw:
            self.raw.write(b"<" + data)
        self.ppp.feed(data)

    def tick(self):
        self.ppp.tick()
        self.stack.tick()
        self._flush()
        if self.ppp.dead:
            self.close()

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.close()
        except socket.error:
            pass
        if self.raw:
            self.raw.close()
        self._log("carrier lost after %d s" % (time.time() - self.started))
        self.report()

    def report(self):
        f = self.ftp
        if f.logins:
            self._log("summary: FTP login %s"
                      % ", ".join("%s / %s" % lp for lp in f.logins))
        if self.ppp.peer_user is not None:
            self._log("summary: PPP account %r password %r"
                      % (self.ppp.peer_user, self.ppp.peer_pass))
        if f.misses:
            self._log("summary: %d path(s) the cabinet wanted and did not get:"
                      % len(f.misses))
            for m in f.misses:
                self._log("summary:     %s" % m)
        else:
            self._log("summary: every file the cabinet asked for was served")
        if self.ppp.hdlc.bad or self.ppp.hdlc.short:
            self._log("summary: %d bad FCS, %d runt frame(s)"
                      % (self.ppp.hdlc.bad, self.ppp.hdlc.short))


def parse_date(s):
    if not s:
        return None
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d"):
        try:
            return time.mktime(time.strptime(s, fmt))
        except ValueError:
            continue
    raise argparse.ArgumentTypeError("not a date: %r" % s)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="A fun.net stand-in for emulated Photo Play cabinets.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Point the emulated modem's host/port at this program and dial "
               "any number.")
    ap.add_argument("--bind", default="127.0.0.1",
                    help="address to listen on (default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=23,
                    help="port the modem dials (default 23, the modem "
                         "device's own default)")
    ap.add_argument("--root", default=os.path.join(HERE, "site"),
                    help="the served tree; /master/... lives inside it")
    ap.add_argument("--captures", default=os.path.join(HERE, "captures"),
                    help="where a dated copy of every upload is kept")
    ap.add_argument("--raw", default=None,
                    help="also write the raw serial bytes of each session here")
    ap.add_argument("--server-ip", default="195.170.72.254",
                    help="our address on the link (default: the gateway the "
                         "IGO 6 cabinet's own WATTCP.CFG names)")
    ap.add_argument("--client-ip", default="195.170.72.135",
                    help="the address handed to the cabinet (default: the one "
                         "that cabinet was given in 2003)")
    ap.add_argument("--dns-ip", default=None,
                    help="address to answer every name with (default: "
                         "--server-ip)")
    ap.add_argument("--date", dest="at", type=parse_date, default=None,
                    help="serve this date to the cabinet's clock instead of "
                         "today, e.g. 2002-06-01.  Net games and tournaments "
                         "carry validity windows")
    ap.add_argument("--auth", choices=("none", "pap", "chap"), default="none",
                    help="demand PPP authentication (default none; whatever "
                         "the cabinet volunteers is accepted and logged "
                         "either way)")
    ap.add_argument("--accm", type=lambda s: int(s, 0), default=0,
                    help="ACCM to ask the cabinet for (default 0: escape "
                         "nothing.  Try 0xffffffff if framing looks unhappy)")
    ap.add_argument("--mru", type=int, default=1500, help="link MRU")
    ap.add_argument("--log", default=None, help="append the log to this file")
    ap.add_argument("--quiet", action="store_true", help="log to the file only")
    cfg = ap.parse_args(argv)

    if cfg.dns_ip is None:
        cfg.dns_ip = cfg.server_ip

    os.makedirs(cfg.root, exist_ok=True)
    os.makedirs(cfg.captures, exist_ok=True)

    log = Log(cfg.log, cfg.quiet)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((cfg.bind, cfg.port))
    except socket.error as e:
        print("cannot listen on %s:%d -- %s" % (cfg.bind, cfg.port, e),
              file=sys.stderr)
        return 1
    srv.listen(4)
    srv.setblocking(False)

    log("funnetd listening on %s:%d" % (cfg.bind, cfg.port))
    log("serving %s" % os.path.abspath(cfg.root))
    log("uploads kept in %s" % os.path.abspath(cfg.captures))
    if cfg.at:
        log("clock set to %s" % time.strftime("%Y-%m-%d %H:%M:%S",
                                              time.localtime(cfg.at)))
    log("set the cabinet's modem to host %s, port %d, then dial anything"
        % (cfg.bind, cfg.port))

    sessions = []
    n = 0
    try:
        while True:
            socks = [srv] + [s.sock for s in sessions if not s.closed]
            try:
                r, _, _ = select.select(socks, [], [], 0.05)
            except (select.error, OSError):
                r = []
            for s in r:
                if s is srv:
                    try:
                        conn, addr = srv.accept()
                    except socket.error:
                        continue
                    n += 1
                    sessions.append(Session(conn, addr, cfg, log, n))
                else:
                    for sess in sessions:
                        if sess.sock is s:
                            sess.readable()
            for sess in list(sessions):
                if sess.closed:
                    sessions.remove(sess)
                else:
                    sess.tick()
    except KeyboardInterrupt:
        log("stopping")
        for sess in sessions:
            sess.close()
    finally:
        srv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
