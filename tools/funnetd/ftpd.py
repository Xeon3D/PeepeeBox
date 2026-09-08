"""The FTP server the cabinet actually talks to.

`\\FN_SYS\\DFU\\CLIENT.EXE` is a WATTCP FTP client, and its string table names
every command it can send:

    USER PASS TYPE SIZE REST RETR STOR DELE RNFR RNTO LIST QUIT
    PORT %hu,%hu,%hu,%hu,%hu,%hu

There is no PASV in it, so **every transfer is active**: the cabinet listens and
the server dials back.  That is the one thing this module has to get right, and
it is the reason the whole IP stack exists rather than a socket being handed to
the host's OS.

The tree it walks, also from the binary:

    /master/outgoing/country        a script for everyone in this territory
    /master/outgoing/machine        a script for this cabinet alone
    /master/outgoing/password       the operator-password check
    /master/outgoing/newspage/...   fun.news pages and their picture files
    /master/incoming                where the cabinet's own upload goes
    /master/trans                   book-keeping exports (trans.dat)
    /master/data/update             updates, run on the next boot
    /master/data/funmail            fun.mail card artwork

Uploads are the interesting direction.  The cabinet builds
\\FN_SYS\\DFU\\TMP\\SCRIPT.UL out of its databases, STORs it as `tmpfile.1` and
then renames it into place, so a completed session leaves a real sample of
funworld's own record format on this side of the wire.  Every upload is
therefore also copied, untouched, into the capture directory: the format of the
matching *download* is not documented anywhere, and that sample is the way to
learn it.

Anything asked for that is not there gets a clean 550 rather than a stall.  A
cabinet that is told "no" moves on to the next step; a cabinet that is told
nothing sits out its sixty-second CONNECTION TIMEOUT.
"""

import os
import shutil
import stat
import time

CRLF = b"\r\n"


class Data:
    """One data connection: a file going out, or a file coming in."""

    def __init__(self, ctrl, direction, payload=None, path=None, rest=0):
        self.ctrl = ctrl
        self.direction = direction  # "out" or "in"
        self.payload = payload  # bytes, for "out"
        self.path = path  # real path, for "in"
        self.rest = rest
        self.fh = None
        self.count = 0
        self.opened = False

    def on_open(self, sock):
        self.opened = True
        if self.direction == "out":
            sock.send(self.payload)
            self.count = len(self.payload)
            sock.close()
        else:
            try:
                os.makedirs(os.path.dirname(self.path), exist_ok=True)
                self.fh = open(self.path, "r+b" if self.rest else "wb")
                if self.rest:
                    self.fh.seek(self.rest)
            except OSError as e:
                self.ctrl.log("ftp: cannot write %s: %s" % (self.path, e))
                sock.abort()

    def on_data(self, sock, data):
        if self.direction == "in" and data and self.fh is not None:
            self.fh.write(data)
            self.count += len(data)

    def on_close(self, sock):
        if self.fh is not None:
            self.fh.close()
            self.fh = None
        if not self.opened:
            self.ctrl.finish_data(self, ok=False)
            return
        if self.direction == "in":
            self.ctrl.srv.captured(self.path)
        self.ctrl.finish_data(self, ok=True)


class Control:
    """One FTP control connection."""

    def __init__(self, server):
        self.srv = server
        self.log = server.log
        self.sock = None
        self.line = bytearray()

        self.user = None
        self.cwd = "/"
        self.binary = True
        self.rest = 0
        self.rnfr = None
        self.port = None  # (addr bytes, port) from PORT
        self.data = None
        self.after = None  # reply to send when the data connection ends

    # -- socket face ------------------------------------------------------

    def on_open(self, sock):
        self.sock = sock
        self.log("ftp: connection from %s" % _ip(sock.raddr))
        self.reply(220, "fun.net FTP service ready")

    def on_data(self, sock, data):
        if not data:
            return
        self.line += data
        while True:
            i = self.line.find(b"\n")
            if i < 0:
                break
            raw = bytes(self.line[:i]).rstrip(b"\r")
            del self.line[:i + 1]
            if len(raw) > 1024:
                continue
            self.command(raw.decode("latin-1"))

    def on_close(self, sock):
        self.log("ftp: connection closed")
        if self.data is not None:
            self.data = None

    def reply(self, code, text):
        for ln in text.split("\n")[:-1]:
            self.sock.send(b"%d-%s%s" % (code, ln.encode("latin-1"), CRLF))
        last = text.split("\n")[-1]
        self.log("ftp: <- %d %s" % (code, last))
        self.sock.send(b"%d %s%s" % (code, last.encode("latin-1"), CRLF))

    # -- paths ------------------------------------------------------------

    def vpath(self, arg):
        """Virtual path, absolute and normalised."""
        if not arg:
            return self.cwd
        p = arg if arg.startswith("/") else self.cwd.rstrip("/") + "/" + arg
        parts = []
        for seg in p.split("/"):
            if seg in ("", "."):
                continue
            if seg == "..":
                if parts:
                    parts.pop()
                continue
            parts.append(seg)
        return "/" + "/".join(parts)

    def real(self, vpath):
        """Map a virtual path onto the served tree, matching case loosely.

        The client writes its paths in lower case and DOS is indifferent about
        it; a tree checked into git on a case-sensitive filesystem is not.
        """
        cur = self.srv.root
        for seg in vpath.strip("/").split("/"):
            if not seg:
                continue
            nxt = os.path.join(cur, seg)
            if not os.path.exists(nxt) and os.path.isdir(cur):
                low = seg.lower()
                for name in os.listdir(cur):
                    if name.lower() == low:
                        nxt = os.path.join(cur, name)
                        break
            cur = nxt
        return cur

    # -- commands ---------------------------------------------------------

    def command(self, line):
        cmd, _, arg = line.partition(" ")
        cmd = cmd.upper().strip()
        arg = arg.strip()
        self.log("ftp: -> %s%s" % (cmd, (" " + arg) if arg and cmd != "PASS" else
                                   (" ****" if cmd == "PASS" else "")))

        fn = getattr(self, "cmd_" + cmd, None)
        if fn is None:
            self.reply(502, "%s not understood" % cmd)
            return
        try:
            fn(arg)
        except Exception as e:  # a broken request must not take the server down
            self.log("ftp: %s failed: %r" % (cmd, e))
            self.reply(451, "local error")

    def cmd_USER(self, arg):
        self.user = arg
        self.reply(331, "password required")

    def cmd_PASS(self, arg):
        self.log("ftp: cabinet logged in as %r / %r" % (self.user, arg))
        self.srv.note_login(self.user, arg)
        self.reply(230, "user %s logged in" % self.user)

    def cmd_SYST(self, arg):
        self.reply(215, "UNIX Type: L8")

    def cmd_NOOP(self, arg):
        self.reply(200, "ok")

    def cmd_TYPE(self, arg):
        self.binary = arg[:1].upper() == "I"
        self.reply(200, "type set to %s" % ("I" if self.binary else "A"))

    def cmd_PWD(self, arg):
        self.reply(257, '"%s"' % self.cwd)

    cmd_XPWD = cmd_PWD

    def cmd_CWD(self, arg):
        v = self.vpath(arg)
        r = self.real(v)
        if not os.path.isdir(r):
            self.reply(550, "%s: no such directory" % v)
            return
        self.cwd = v
        self.reply(250, "now in %s" % v)

    def cmd_CDUP(self, arg):
        self.cmd_CWD("..")

    def cmd_MKD(self, arg):
        r = self.real(self.vpath(arg))
        os.makedirs(r, exist_ok=True)
        self.reply(257, '"%s" created' % self.vpath(arg))

    def cmd_PORT(self, arg):
        try:
            n = [int(x) for x in arg.split(",")]
            if len(n) != 6 or any(not 0 <= x <= 255 for x in n):
                raise ValueError
        except ValueError:
            self.reply(501, "bad PORT")
            return
        self.port = (bytes(n[:4]), (n[4] << 8) | n[5])
        self.reply(200, "PORT ok")

    def cmd_PASV(self, arg):
        # Not used by CLIENT.EXE, but a passive server is cheap and makes the
        # tree reachable from an ordinary FTP client over the same link.
        port = self.srv.passive_port()
        self.srv.stack.tcp_listen(port, lambda: self._pending_data())
        self.pasv_port = port
        a = self.sock.laddr
        self.reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)"
                   % (a[0], a[1], a[2], a[3], port >> 8, port & 0xFF))

    def _pending_data(self):
        return self.data

    def cmd_REST(self, arg):
        try:
            self.rest = max(0, int(arg))
        except ValueError:
            self.reply(501, "bad REST")
            return
        self.reply(350, "restarting at %d" % self.rest)

    def cmd_SIZE(self, arg):
        r = self.real(self.vpath(arg))
        if not os.path.isfile(r):
            self.reply(550, "%s: no such file" % self.vpath(arg))
            return
        self.reply(213, str(os.path.getsize(r)))

    def cmd_MDTM(self, arg):
        r = self.real(self.vpath(arg))
        if not os.path.isfile(r):
            self.reply(550, "%s: no such file" % self.vpath(arg))
            return
        self.reply(213, time.strftime("%Y%m%d%H%M%S",
                                      time.gmtime(os.path.getmtime(r))))

    def cmd_DELE(self, arg):
        r = self.real(self.vpath(arg))
        if not os.path.isfile(r):
            self.reply(550, "%s: no such file" % self.vpath(arg))
            return
        os.remove(r)
        self.reply(250, "deleted")

    def cmd_RNFR(self, arg):
        r = self.real(self.vpath(arg))
        if not os.path.exists(r):
            self.reply(550, "%s: no such file" % self.vpath(arg))
            return
        self.rnfr = r
        self.reply(350, "ready for RNTO")

    def cmd_RNTO(self, arg):
        if self.rnfr is None:
            self.reply(503, "RNFR first")
            return
        dst = self.real(self.vpath(arg))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst):
            os.remove(dst)
        os.rename(self.rnfr, dst)
        self.log("ftp: renamed to %s" % self.vpath(arg))
        self.rnfr = None
        self.srv.captured(dst)
        self.reply(250, "renamed")

    def cmd_QUIT(self, arg):
        self.reply(221, "goodbye")
        self.sock.close()

    def cmd_ABOR(self, arg):
        self.reply(226, "nothing to abort")

    # -- transfers --------------------------------------------------------

    def cmd_RETR(self, arg):
        v = self.vpath(arg)
        r = self.real(v)
        if not os.path.isfile(r):
            self.log("ftp: MISS %s" % v)
            self.srv.note_miss(v)
            self.reply(550, "%s: no such file" % v)
            self.rest = 0
            return
        with open(r, "rb") as fh:
            if self.rest:
                fh.seek(self.rest)
            payload = fh.read()
        self.log("ftp: sending %s (%d bytes%s)"
                 % (v, len(payload), " from %d" % self.rest if self.rest else ""))
        self.rest = 0
        self.start_data(Data(self, "out", payload=payload), len(payload))

    def cmd_STOR(self, arg):
        v = self.vpath(arg)
        r = self.real(v)
        self.log("ftp: receiving %s" % v)
        d = Data(self, "in", path=r, rest=self.rest)
        self.rest = 0
        self.start_data(d, None)

    cmd_APPE = cmd_STOR

    def cmd_LIST(self, arg):
        self._listing(arg, long=True)

    def cmd_NLST(self, arg):
        self._listing(arg, long=False)

    def _listing(self, arg, long):
        arg = " ".join(a for a in arg.split() if not a.startswith("-"))
        v = self.vpath(arg)
        r = self.real(v)
        lines = []
        if os.path.isdir(r):
            names = sorted(os.listdir(r))
            for name in names:
                p = os.path.join(r, name)
                lines.append(_ls(p, name) if long else name)
        elif os.path.isfile(r):
            lines.append(_ls(r, os.path.basename(r)) if long
                         else os.path.basename(r))
        else:
            self.reply(550, "%s: no such file or directory" % v)
            return
        payload = CRLF.join(x.encode("latin-1") for x in lines) + (CRLF if lines else b"")
        self.start_data(Data(self, "out", payload=payload), len(payload))

    def start_data(self, data, size):
        if self.port is None:
            self.reply(425, "use PORT first")
            return
        self.data = data
        self.reply(150, "opening %s mode data connection%s"
                   % ("BINARY" if self.binary else "ASCII",
                      " (%d bytes)" % size if size is not None else ""))
        addr, port = self.port
        self.port = None
        self.srv.stack.tcp_connect(self.sock.laddr, 20, addr, port, data)

    def finish_data(self, data, ok):
        if self.data is not data:
            return
        self.data = None
        if ok:
            self.reply(226, "transfer complete (%d bytes)" % data.count)
        else:
            self.reply(425, "cannot open data connection")


def _ls(path, name):
    try:
        st = os.stat(path)
    except OSError:
        return "----------   1 funnet funnet            0 Jan  1  1970 " + name
    isdir = stat.S_ISDIR(st.st_mode)
    mode = ("drwxr-xr-x" if isdir else "-rw-r--r--")
    when = time.strftime("%b %d  %Y", time.localtime(st.st_mtime))
    return "%s   1 funnet funnet %12d %s %s" % (mode, st.st_size, when, name)


def _ip(b):
    return ".".join(str(x) for x in b)


class FtpServer:
    """Holds the served tree and hands out one Control per connection."""

    def __init__(self, stack, root, captures, log):
        self.stack = stack
        self.root = root
        self.captures = captures
        self.log = log
        self.misses = []
        self.logins = []
        self._pasv = 30000

    def factory(self):
        return Control(self)

    def passive_port(self):
        self._pasv += 1
        if self._pasv > 30500:
            self._pasv = 30000
        return self._pasv

    def note_miss(self, vpath):
        if vpath not in self.misses:
            self.misses.append(vpath)

    def note_login(self, user, pw):
        if (user, pw) not in self.logins:
            self.logins.append((user, pw))

    def captured(self, path):
        """Keep a dated copy of anything the cabinet uploaded."""
        if not os.path.isfile(path):
            return
        try:
            os.makedirs(self.captures, exist_ok=True)
            base = os.path.basename(path)
            dst = os.path.join(self.captures,
                               time.strftime("%Y%m%d-%H%M%S-") + base)
            shutil.copy2(path, dst)
            self.log("ftp: captured %s (%d bytes)" % (dst, os.path.getsize(dst)))
        except OSError as e:
            self.log("ftp: could not capture %s: %s" % (path, e))
