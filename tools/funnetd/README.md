# funnetd — a fun.net stand-in

A server for emulated Photo Play cabinets to dial with the COM4 modem from
`33-modem.md`. It terminates the cabinet's PPP link, gives it an address,
answers its name lookups, sets its clock, and runs the FTP server its
`CLIENT.EXE` expects.

`docs/research/35-funnet.md` is where the protocol comes from and what is
measured versus inferred. This file is how to run it.

## Run it

Python 3.8 or newer, no third-party modules.

```bash
python tools/funnetd/funnetd.py
```

It listens on `127.0.0.1:23` by default, which is the modem device's own
default port, so there is nothing to match up.

Then fit the modem to the machine — **Tools → Modem…** in the PeepeeBox UI, or
in the machine's ini:

```ini
[Photo Play]
modem = pp_modem

[Modem]
line = 1
host = 127.0.0.1
host_port = 23
```

`line = 1` is "Dial out to a TCP/IP host". Any number the cabinet dials comes
here; its own `NET.CFG` number is ignored, because there is only one place to
go.

Then start the transmission on the cabinet: the operator menu's data
transmission page, or let it fire on its own schedule (the MASTERS machine's
window was `ftpstart 465` to `ftpende 495` — 07:45 to 08:15).

## Options worth knowing

| | |
|---|---|
| `--date 2002-06-01` | serve this date to the cabinet's clock. **Use it.** Net games and tournaments carry validity windows — the one net game on the MASTERS image ran 2002-05-27 to 2002-06-30, and a cabinet that thinks it is 2026 will download a game and then refuse to show it |
| `--root DIR` | the served tree; `/master/...` lives inside it (default `site/`) |
| `--captures DIR` | a dated copy of every upload (default `captures/`) |
| `--raw DIR` | also write the raw serial bytes of each session, for picking apart afterwards |
| `--log FILE` | append the log to a file as well |
| `--bind` / `--port` | where to listen |
| `--date`, `--server-ip`, `--client-ip` | the addresses default to the ones the IGO 6 cabinet was actually given in its last real session |
| `--auth pap` | demand PPP authentication. Not needed, but it makes the cabinet send its `NET.CFG` password in the clear, which is otherwise obscured on disk |
| `--accm 0xffffffff` | escape every control character in HDLC. Only if framing misbehaves |

## What to expect the first time

The link and the FTP session will work. The *content* will not, because nobody
knows what fun.net used to send back.

`CLIENT.EXE` downloads a script into `\FN_SYS\DFU\TMP\SCRIPT.DL` and parses it
for records called `MACHLIC`, `DATETIME`, `UPDATE`, `MSTMAIN`, `NEWSPAGE` and
forty more. That grammar is not published and cannot be guessed from the
binary's string table. So every file the cabinet asks for and does not find is
answered `550` — which the client treats as "nothing for me today" — and the
session ends cleanly instead of hanging.

Two things come out of that first session, and they are the point of running it:

1. **A miss list.** The closing summary names every path the cabinet wanted:

   ```
   summary: 3 path(s) the cabinet wanted and did not get:
   summary:     /master/outgoing/machine/<whatever it is called>
   summary:     /master/outgoing/country/...
   summary:     /master/outgoing/password/...
   ```

   That list is the specification for what goes in `site/` next.

2. **A real upload.** The cabinet builds `SCRIPT.UL` out of the *same* record
   vocabulary as the download and STORs it. A dated copy lands in `captures/`.
   That is a genuine sample of funworld's own format, and it is the way to
   learn what the download should look like.

So: run it, let a cabinet complete a session, then read `captures/`.

## Sending games and updates

This is what fun.net was mostly *for*, and the mechanism is fully mapped —
`35-funnet.md` has it in detail. In outline:

1. The script names an update by id and name (`rev_00054_kniffler.upd`).
2. The client fetches it from `/master/data/update` into
   `\FN_SYS\DFU\RESUME\<id>.upd`, resumably, with `SIZE` and `REST`.
3. It **decrypts** it to `\FN_SYS\UPDATE\<id>.exe`.
4. That file is a self-extracting archive. `TRANSMIT.BAT` runs it on the way
   out, calls the `start.bat` inside it, then `FN_SYS.EXE /confupd=<id>`.

The Dutch MASTERS cabinet took twenty-nine of these between May 2001 and June
2002 — `kniffler`, `same`, `summergame`, `wintertowers2001`, `soccerq2001`,
`thebar2001` and more, plus the client and menu binaries updating themselves.

**Two things stop `funnetd` doing it today**: the script record that names an
update, and the `.upd` cipher — no sample of one survives to attack. Both come
loose from the same place, a captured `SCRIPT.UL`, because the cabinet reports
its own update list upward in the same record vocabulary.

Meanwhile, to get a game into an emulated cabinet at all, `tools/imgput.py`
writes files straight into the disk image — `\EXE\<GAME>.EXE`, its data
directory, and a `NETGAME.TAB` row. No cipher and no telephone involved.

## Verify it without a cabinet

```bash
python tools/funnetd/selftest.py
```

It stands a second PPP peer and IP stack on the other end of a real socket and
drives it the way `CLIENT.EXE` does — including active-mode `PORT` transfers,
which is the only mode the cabinet has. Thirty-four checks: LCP, IPCP, DNS, all
three time services, `--date`, then `SIZE`, `RETR`, `REST`, `STOR`,
`RNFR`/`RNTO`, `LIST`, `DELE`, and a missing file being refused rather than
stalling.

It proves the server agrees with a model of the client. It cannot prove the
server agrees with the client itself; only a cabinet can do that.

## How it fits together

```
     the guest                                    funnetd
  ---------------                          --------------------
  CLIENT.EXE (FTP)                            ftpd.py
  WATTCP                                      services.py  (DNS, time)
  IPSTUB / ODI                                ipstack.py   (IPv4/ICMP/UDP/TCP)
  PPP.EXE + NET.CFG    <--- PPP/HDLC --->     ppp.py
  COM4, 0x2E8 IRQ 10                          funnetd.py
  char_modem.c         <--- TCP socket --->
```

The IP stack is not a convenience — it is required. `CLIENT.EXE` has no `PASV`,
so every data transfer is the server dialling *back* into the cabinet, at an
address that exists only on the PPP link. A host-OS socket cannot reach it.

For the same reason the stack answers for **every** destination address on the
link, not just its own: `WATTCP.CFG` hard-codes nameserver addresses that died
twenty years ago, and on some images a fixed `my_ip` as well. Those packets
arrive here regardless, so here answers them, and no cabinet needs editing.

## Files

| | |
|---|---|
| `funnetd.py` | listener, session glue, command line |
| `ppp.py` | HDLC + FCS-16, LCP, PAP, CHAP, IPCP |
| `ipstack.py` | IPv4, ICMP, UDP, TCP |
| `services.py` | name server, RFC 868 and SNTP clocks |
| `ftpd.py` | the FTP server |
| `selftest.py` | the end-to-end check |
| `site/` | the served tree |
| `captures/` | dated copies of everything uploaded |
