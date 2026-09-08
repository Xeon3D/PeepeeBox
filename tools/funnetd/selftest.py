r"""End-to-end test for funnetd, with no cabinet and no emulator involved.

It stands a second PPP peer and IP stack up on the other end of a real TCP
socket and makes it behave the way `CLIENT.EXE` does: negotiate the link, ask
the name server for `ftp.nl.funsys.com`, read the clock, then log in over FTP
and move files in both directions -- always with PORT, never PASV, because that
is the only mode the cabinet's client has.

    python selftest.py

Every step asserts.  A clean run means the framing, the two control protocols,
the IP stack, both TCP directions including an inbound active-mode data
connection, DNS, both time services and the FTP command set all work against
each other.  What it cannot prove is that funworld's client agrees with any of
it; that needs a cabinet, and `--raw` on the server plus this file as a
reference is how to compare the two.
"""

import os
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import funnetd  # noqa: E402
import ipstack  # noqa: E402
import ppp  # noqa: E402

SERVER_IP = "195.170.72.254"
CLIENT_IP = "195.170.72.135"
FTP_IP = "213.9.9.9"  # whatever DNS answered; the link carries it regardless
NAMESERVER = "194.158.160.10"  # a dead address out of a real WATTCP.CFG

FAILED = []


def check(ok, what):
    print("  %-4s %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        FAILED.append(what)


class Peer:
    """The cabinet side: PPP plus a stack, driven by pump()."""

    def __init__(self, host, port, quiet=True):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.setblocking(False)
        self.out = bytearray()
        self.quiet = quiet

        self.ppp = ppp.Ppp(self._write, self._log, local_ip=CLIENT_IP,
                           peer_ip=SERVER_IP, dns_ip=SERVER_IP, accm=0)
        self.stack = ipstack.IpStack(self.ppp.send_ip, self._log)
        self.ppp.on_ip = self.stack.on_packet
        self.udp_rx = []
        self.stack.udp_listen(5353, self._udp)
        self.stack.udp_listen(5337, self._udp)
        self.stack.udp_listen(5123, self._udp)

    def _log(self, msg):
        if not self.quiet:
            print("    cabinet: %s" % msg)

    def _write(self, data):
        self.out += data
        self._flush()

    def _flush(self):
        while self.out:
            try:
                n = self.sock.send(bytes(self.out[:4096]))
            except socket.error:
                return
            if n <= 0:
                return
            del self.out[:n]

    def _udp(self, stack, laddr, lport, raddr, rport, data):
        self.udp_rx.append((lport, data))

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                data = self.sock.recv(4096)
                if data:
                    self.ppp.feed(data)
                elif data == b"":
                    return
            except socket.error:
                pass
            self.ppp.tick()
            self.stack.tick()
            self._flush()
            time.sleep(0.002)

    def wait(self, pred, seconds=10.0):
        end = time.time() + seconds
        while time.time() < end:
            if pred():
                return True
            self.pump(0.02)
        return pred()

    def close(self):
        try:
            self.sock.close()
        except socket.error:
            pass


class Sink:
    """Collects everything a connection delivers."""

    def __init__(self):
        self.buf = bytearray()
        self.opened = False
        self.closed = False
        self.sock = None

    def on_open(self, sock):
        self.opened = True
        self.sock = sock

    def on_data(self, sock, data):
        self.buf += data

    def on_close(self, sock):
        self.closed = True


class Upload(Sink):
    """A data connection that pushes a payload and closes, as STOR does."""

    def __init__(self, payload):
        Sink.__init__(self)
        self.payload = payload

    def on_open(self, sock):
        Sink.on_open(self, sock)
        sock.send(self.payload)
        sock.close()


class Ftp:
    """Just enough FTP client to imitate CLIENT.EXE."""

    def __init__(self, peer):
        self.peer = peer
        self.ctrl = Sink()
        self.dataport = 4000
        self.peer.stack.tcp_connect(ipstack.s2ip(CLIENT_IP), 3000,
                                    ipstack.s2ip(FTP_IP), 21, self.ctrl)
        self.peer.wait(lambda: self.ctrl.opened, 8)

    def reply(self, seconds=8.0):
        """Read one complete reply line (final lines have a space at index 3)."""
        end = time.time() + seconds
        while time.time() < end:
            i = self.ctrl.buf.find(b"\r\n")
            if i >= 0:
                line = bytes(self.ctrl.buf[:i]).decode("latin-1")
                del self.ctrl.buf[:i + 2]
                if len(line) > 3 and line[3] == "-":
                    continue
                return line
            self.peer.pump(0.02)
        return ""

    def cmd(self, text, seconds=8.0):
        self.ctrl.sock.send(text.encode("latin-1") + b"\r\n")
        return self.reply(seconds)

    def port(self, handler):
        """Listen, then tell the server where to dial back."""
        self.dataport += 1
        p = self.dataport
        self.peer.stack.tcp_listen(p, lambda: handler)
        a = [int(x) for x in CLIENT_IP.split(".")]
        return self.cmd("PORT %d,%d,%d,%d,%d,%d"
                        % (a[0], a[1], a[2], a[3], p >> 8, p & 0xFF))

    def transfer(self, command, handler, seconds=15.0):
        self.port(handler)
        first = self.cmd(command)
        self.peer.wait(lambda: handler.closed, seconds)
        second = self.reply(seconds)
        return first, second


def dns_query(peer, name):
    q = struct.pack(">HHHHHH", 0x1234, 0x0100, 1, 0, 0, 0)
    for label in name.split("."):
        q += bytes((len(label),)) + label.encode("latin-1")
    q += b"\x00" + struct.pack(">HH", 1, 1)
    peer.stack.send_udp(ipstack.s2ip(CLIENT_IP), 5353,
                        ipstack.s2ip(NAMESERVER), 53, q)
    peer.wait(lambda: any(p == 5353 for p, _ in peer.udp_rx), 6)
    for p, data in peer.udp_rx:
        if p == 5353:
            peer.udp_rx.remove((p, data))
            if len(data) > 12 and struct.unpack(">H", data[6:8])[0] >= 1:
                return ".".join(str(b) for b in data[-4:])
    return None


def main():
    root = tempfile.mkdtemp(prefix="funnetd-test-")
    site = os.path.join(root, "site")
    caps = os.path.join(root, "captures")
    os.makedirs(os.path.join(site, "master", "outgoing", "machine"))
    os.makedirs(os.path.join(site, "master", "incoming"))

    script = b"SYSTEM\r\nPPDOS\r\n" + b"X" * 9000  # spans many segments
    with open(os.path.join(site, "master", "outgoing", "machine",
                           "5E700037862B.dl"), "wb") as fh:
        fh.write(script)

    port = 0
    probe = socket.socket()
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()

    argv = ["--bind", "127.0.0.1", "--port", str(port),
            "--root", site, "--captures", caps, "--quiet",
            "--date", "2002-06-01"]
    t = threading.Thread(target=funnetd.main, args=(argv,), daemon=True)
    t.start()
    time.sleep(0.4)

    print("link")
    peer = Peer("127.0.0.1", port)
    peer.ppp.start()
    check(peer.wait(lambda: peer.ppp.lcp_up, 12), "LCP comes up")
    check(peer.wait(lambda: peer.ppp.up, 12), "IPCP comes up")
    check(ppp.ip2s(peer.ppp.peer_ip) == SERVER_IP,
          "server is %s" % SERVER_IP)

    print("name service")
    got = dns_query(peer, "ftp.nl.funsys.com")
    check(got == SERVER_IP, "ftp.nl.funsys.com -> %s (got %s)" % (SERVER_IP, got))
    got = dns_query(peer, "ftp.backup.funnet.cc")
    check(got == SERVER_IP, "the hard-coded backup name resolves too")

    print("clock")
    peer.stack.send_udp(ipstack.s2ip(CLIENT_IP), 5337,
                        ipstack.s2ip("193.83.149.137"), 37, b"\x00")
    peer.wait(lambda: any(p == 5337 for p, _ in peer.udp_rx), 6)
    secs = None
    for p, data in list(peer.udp_rx):
        if p == 5337 and len(data) == 4:
            secs = struct.unpack(">I", data)[0] - 2208988800
            peer.udp_rx.remove((p, data))
    check(secs is not None, "RFC 868 over UDP answers")
    if secs:
        served = time.strftime("%Y-%m-%d", time.localtime(secs))
        check(served == "2002-06-01", "--date is honoured (got %s)" % served)

    tsink = Sink()
    peer.stack.tcp_connect(ipstack.s2ip(CLIENT_IP), 3100,
                           ipstack.s2ip("193.83.149.137"), 37, tsink)
    peer.wait(lambda: tsink.closed, 8)
    check(len(tsink.buf) == 4, "RFC 868 over TCP answers with four bytes")

    sntp = struct.pack(">BBBb11I", 0x1B, 0, 6, -20, *([0] * 11))
    peer.stack.send_udp(ipstack.s2ip(CLIENT_IP), 5123,
                        ipstack.s2ip("193.83.149.137"), 123, sntp)
    peer.wait(lambda: any(p == 5123 for p, _ in peer.udp_rx), 6)
    check(any(p == 5123 and len(d) == 48 for p, d in peer.udp_rx),
          "SNTP answers as well")

    print("ftp")
    ftp = Ftp(peer)
    check(ftp.ctrl.opened, "control connection accepted on port 21")
    check(ftp.reply().startswith("220"), "greeting")
    check(ftp.cmd("USER pp").startswith("331"), "USER pp")
    check(ftp.cmd("PASS 92tx45dt").startswith("230"), "PASS accepted")
    check(ftp.cmd("TYPE I").startswith("200"), "TYPE I")
    check(ftp.cmd("CWD /master/outgoing/machine").startswith("250"),
          "CWD /master/outgoing/machine")
    check(ftp.cmd("SIZE 5E700037862B.dl") == "213 %d" % len(script),
          "SIZE reports %d" % len(script))

    sink = Sink()
    first, second = ftp.transfer("RETR 5E700037862B.dl", sink)
    check(first.startswith("150"), "RETR opens the data connection")
    check(bytes(sink.buf) == script,
          "RETR delivered all %d bytes intact" % len(script))
    check(second.startswith("226"), "RETR finishes 226")

    check(ftp.cmd("REST 9000").startswith("350"), "REST 9000")
    sink = Sink()
    ftp.transfer("RETR 5E700037862B.dl", sink)
    check(bytes(sink.buf) == script[9000:], "REST resumed at the right offset")

    check(ftp.cmd("CWD /master/incoming").startswith("250"),
          "CWD /master/incoming")
    payload = b"MACHLIC 5E700037862B\r\nDATETIME 2002-06-01 09:00:00\r\n" * 200
    first, second = ftp.transfer("STOR tmpfile.1", Upload(payload))
    check(first.startswith("150"), "STOR opens the data connection")
    check(second.startswith("226"), "STOR finishes 226")
    landed = os.path.join(site, "master", "incoming", "tmpfile.1")
    check(os.path.isfile(landed) and open(landed, "rb").read() == payload,
          "the uploaded bytes reached the tree intact")

    check(ftp.cmd("RNFR tmpfile.1").startswith("350"), "RNFR tmpfile.1")
    check(ftp.cmd("RNTO 5E700037862B.ul").startswith("250"),
          "RNTO 5E700037862B.ul -- the rename CLIENT.EXE does")
    check(os.path.isfile(os.path.join(site, "master", "incoming",
                                      "5E700037862B.ul")), "the rename landed")
    kept = [f for f in os.listdir(caps) if f.endswith(".ul")]
    check(bool(kept), "the upload was captured (%s)" % (kept[:1] or "none"))

    sink = Sink()
    ftp.transfer("LIST", sink)
    check(b"5E700037862B.ul" in bytes(sink.buf), "LIST shows the new file")

    check(ftp.cmd("RETR nothing.here").startswith("550"),
          "a missing file is refused, not stalled")
    check(ftp.cmd("CWD /master/nowhere").startswith("550"),
          "a missing directory is refused")
    check(ftp.cmd("DELE 5E700037862B.ul").startswith("250"), "DELE")
    check(ftp.cmd("QUIT").startswith("221"), "QUIT")

    peer.pump(0.5)
    peer.close()
    shutil.rmtree(root, ignore_errors=True)

    print()
    if FAILED:
        print("%d check(s) failed:" % len(FAILED))
        for f in FAILED:
            print("  - %s" % f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
