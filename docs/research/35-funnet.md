# fun.net, and a server to stand in for it

`33-modem.md` got the cabinet as far as a dial tone. This is what was on the
other end of the call, and what it takes to be that thing now.

Two images carry the evidence, and they are five years and one country apart:

* **`IGO 6 DE ND003`** — a German I.G.O. 6. Its `NET.CFG` survives from its last
  real dial, but its databases were wiped.
* **`PP2001NL-MASTERS-NSB H9751 SR1`** — a Dutch Photo Play 2001 MASTERS
  cabinet out of a bar in Franeker, and the important one. It kept everything:
  its fun.net account, its licence, its tournament state, 2 MB of downloaded
  high-score tables, 500 KB of fun.mail, and a net game that arrived over the
  wire. It is a machine that *worked*, and nearly every fact below comes out
  of it.

## The session, top to bottom

`\FN_SYS\DFU\TRANSMIT.BAT` is the entire nightly job, and it is short enough to
quote whole in outline:

```
\FN_SYS\FN_SYS /datatrans_start        announce it on screen
\FN_SYS\FN_SYS /export                 build the outgoing data
LSL / PPP / IPSTUB                     load the Novell ODI stack
PPPMENU /CONNECT                       dial and negotiate
PPPSTATE WAIT=30 LCP                   wait for the link
PPPSTATE WAIT=30 IP                    wait for an address
PPPWAT                                 hand that address to WATTCP
CLIENT                                 do the work
\FN_SYS\FN_SYS /datatrans_success      or /datatrans_error
call \fn_sys\update\execute\update.bat run anything that was downloaded
```

So: **PPP over a modem, then WATTCP, then one FTP session.** No web, no
proprietary transport. The whole of fun.net, from the cabinet's point of view,
is an FTP server with a known directory layout.

## The link

`\FN_SYS\DFU\NET.CFG`, from both machines, differing only where you would
expect:

| | IGO 6 DE (2003-ish) | PP2001 NL MASTERS |
|---|---|---|
| dial | `0,0718915252` | `0676077111` |
| PPP user | `funny150` | `funworld` |
| password | 32 bytes, obscured | 32 bytes, obscured |
| everything else | identical | identical |

Identical means: `PORT 02E8`, `INT 10`, `BAUD 57,600`, `FLOW CONTROL HARDWARE`,
`OPEN PASSIVE`, `IPXCP DISABLE`, `SPAP DISABLE`, `PCOMP ON`, `ACCOMP ON`,
`TCPIPCOMP 16`, `MAGIC NUMBER 03000422`, `MODEM DIAL "ATDT"`. The file's own
first line says `This file was created by fun.net international` — it is
generated from the tables in `\FN_SYS\DATABASE\NETWORK\`, not hand-edited, which
is why the two agree so exactly.

Three of those lines decide how a stand-in has to behave:

* **`OPEN PASSIVE`** — the cabinet does not start LCP. The server sends the
  first Configure-Request, or nothing ever happens.
* **`TCPIPCOMP 16`** — it will ask for Van Jacobson header compression. A
  Configure-Reject is the documented way out and the ODI driver takes it.
* **`PCOMP` / `ACCOMP ON`** — its frames may arrive with the address, control
  and protocol fields compressed away. The receive path has to handle both
  shapes.

The password is obscured on disk, but PPP authentication is answered in the
clear, so a cabinet dialling a server that challenges it hands over the real
one. `funnetd` logs it for that reason.

## The account

`\FN_SYS\DATABASE\USER\SETTINGS.TAB` on the MASTERS machine — 128-byte records,
a present flag, a 26-byte key, then the value — is a complete fun.net identity:

```
ftpcli_ftpdomain    ftp.nl.funsys.com
ftpcli_ftpuser      pp
ftpcli_ftppass      92tx45dt
ftpcli_ntpdomain    time.nl.funsys.com
ftpcli_ntpoffset    360
ftpcli_country      NL
ftpcli_version      Version 2001 (NL)
machlic             5E7000022726
licnumb             NL893VISS461OFK
password            <redacted>             (the operator's own, 5 chars)
ppserialnumb        205085
system_LOCID        24878
popid               965
ftpstart / ftpende  465 / 495             minutes past midnight: 07:45-08:15
ftpauto / ftpenable 1 / 1
ftperror_count      287
DT_STARTTIME        2016-04-20 09:00:24
```

`licnumb` is the number the operator typed into the registration page, and
`34-funnet-licence.md` takes that field apart on this same cabinet: it is a
self-check with nothing of the machine in it. So of everything in this record,
the licence is the one field a stand-in never has to verify.

`ftp.<cc>.funsys.com` and `time.<cc>.funsys.com` are the pattern; `CLIENT.EXE`
also carries one hard-coded fallback, `ftp.backup.funnet.cc`. The FTP account
is not per-cabinet — `pp` / `92tx45dt` is a plain shared login, and the cabinet
identifies itself by `machlic` inside the session instead.

The last two lines are the quiet ones. `ftperror_count` is 287 and
`DT_STARTTIME` is **2016**: this cabinet went on dialling a service that no
longer existed, every night, for about a decade, and counted every failure.

## What CLIENT.EXE does

`\FN_SYS\DFU\CLIENT.EXE` is a Borland-built WATTCP program, 150 KB, and its
string table names every command it can send:

```
USER PASS TYPE SIZE REST RETR STOR DELE RNFR RNTO LIST QUIT
PORT %hu,%hu,%hu,%hu,%hu,%hu
```

**There is no PASV.** Every transfer is active: the cabinet listens and the
server dials back into it. That single fact is why a stand-in cannot be an
off-the-shelf FTP daemon behind a PPP terminator — the data connection is
opened *towards* a private address that exists only on the link, so whatever
terminates the PPP has to own the IP stack too.

The order of business, from the client's own progress strings:

1. `connecting Time-Server` — resolve `ftpcli_ntpdomain`, set the clock.
2. `connecting fun.net server`, then `connecting fun.net backup server` if that
   fails.
3. `binary connection established`, and it walks:

```
/master/outgoing/country          a script for everyone in this territory
/master/outgoing/machine          a script for this cabinet alone
/master/outgoing/password         the operator-password check -> \fn_sys\dfu\tmp\pwd
/master/outgoing/newspage/newspagefile   fun.news pages and pictures
/master/incoming                  where the cabinet's upload goes
/master/trans                     book-keeping exports (trans.dat)
/master/data/update               updates -> \fn_sys\update\%ld.exe
/master/data/funmail              fun.mail card artwork
outgoing/masters/%ld/             tournament results
outgoing/reward/%ld               rewards
```

4. Downloads land in `\FN_SYS\DFU\TMP\SCRIPT.DL`; the upload is built in
   `\FN_SYS\DFU\TMP\SCRIPT.UL`, STORed as `tmpfile.1`, then renamed into place
   with `RNFR`/`RNTO`. Updates are resumable — `SIZE` then `REST` — into
   `\FN_SYS\DFU\RESUME\%ld.upd`.
5. Then the local work: `check fragmented rankings`, `check new masters`,
   `check masters review`, `check new fun.mails`, `import userauth`,
   `import mcr`, `import rewardings`, `check address-changes`,
   `transfer temporary databases`.

### The record vocabulary

Both scripts are built out of one keyword set, and the binary lists it:

```
MACHLIC DATETIME CONFIRM IDCHG IDDEL FUNAPP UPDATE MSTMAIN MSTNAME
HISROW HISNAME HISPARENT HISUPDOWN USERAUTH INCLUDE LOCID NEWSPAGE
FMCARD FMSYSBOX FMMSGOUT CODE SEND HISNUM HISDONE CHGPLY MSTCREDIT
POTDEF POTCOUNT HISPRIZE MPMAIN MPOUT SMSGROUP SMSMEDIA SMSDATA
SMSPROV SMSSERVICE SMSAVAIL NPMAIN NPDOC NPITEM NPFILE NETGAME
REWMAIN REWDEF COMPREW MCRMAIN MCRNAME MCRROW MCRDONE MSTMAIN2
```

with `SYSTEM` and `PPDOS` as section markers and a `%s %s` line format. The
upload is assembled in six labelled parts: `system data`, `MASTERS data`,
`FUNMAIL data`, `FUNNEWS data`, `FUNSMS data`, `FLIRTBOX data`.

**The grammar of those lines is not established.** It cannot be read off the
string table, and no sample of a fun.net *download* survives in any image here.
What does survive is the other direction: the cabinet builds `SCRIPT.UL` out of
the same vocabulary and uploads it, so one completed session against a stand-in
leaves a real sample. That is the route, and it is why `funnetd` keeps a copy of
every upload.

## What came back down, on this cabinet

The MASTERS machine is proof the whole thing worked, and it is worth listing
what a live cabinet accumulated:

| file | size | what it is |
|---|---|---|
| `HIS_OPER.RVW` | 946 KB | operator-cup rankings |
| `HIS_NODE.RVW` | 816 KB | node rankings |
| `LOGIN.TAB` | 229 KB | registered players |
| `FMMSGIN.TAB` | 319 KB | fun.mail received |
| `FMMSGOUT.TAB` | 227 KB | fun.mail to send |
| `SMSDATA.TAB` | 218 KB | the SMS service |
| `MSTSCORE.TAB` | 98 KB | MASTERS scores |
| `PLAYER.TAB` | 145 KB | player records |

and `NETGAME.TAB`, which has exactly one live record:

```
01  05  NETQUIZ1   2002-05-27 .. 2002-06-30
```

A net game with an **id, a directory name and a validity window** — and
`\NETGAME1`..`\NETGAME5` and `\NETQUIZ1`..`\NETQUIZ5` all exist on the disk.
(The field breakdown is read off one record; treat the layout as probable
rather than settled.) The consequence for emulation is concrete: a cabinet
whose clock says 2026 will fetch a game and then decline to show it. The clock
the server hands out is part of the content.

The tournament side is in `SETTINGS.TAB` too — `lastmstid 923`,
`mststarttime 2002-06-05 13:00:16`, `wait_for_confirm 2002-06-05 14:17:37`,
`mstguestmode 0`. MASTERS was run *through* fun.net: the cabinet uploaded
scores and downloaded the standings.

## How a game reached a cabinet

This is the part that matters most, and the MASTERS machine documents it
completely — not the format, but the mechanism and the fact that it worked.

`\FN_SYS\UPDATE\FNUPDATE.TAB` is the cabinet's own record of every update it
has ever taken. 81-byte records: a present flag, a little-endian id, a label,
a name, and a date. Sixty-three of them are live. The first thirty-four are
`rev_000NN_preinstalled`, dated 2000-12-19 and 2001-01-04/11 — the factory
image. **The remaining twenty-nine arrived over the telephone**, and they are
dated and named:

```
2001-05-11  36  exe2001         2001-10-15   72  mtvgame
2001-05-11  43  maincom         2001-10-17   74  menuexe
2001-05-11  44  clientexe       2001-10-30   77  same
2001-05-11  48  delnetgame      2001-11-29   82  buttons
2001-05-11  54  kniffler        2001-12-27   90  wintersame2001
2001-06-26  50  flirtbox        2002-01-24   97  wintertowers2001
2001-07-11  51  fbexe           2002-02-01   98  undosamegame
2001-08-31  62  np2001          2002-03-05  106  winterundo2001
2001-09-10  64  summergame      2002-03-28  115  magic2001
2001-09-22  65  sms             2002-04-02  111  network
2001-09-28  70  mtvvote         2002-04-23  117  eday2001
                                2002-05-18  130  soccerq2001
                                2002-05-23  135  soccerm2001
                                2002-05-28  131  netquizfix
                                2002-05-30  120  newppnet
                                2002-05-30  121  menu2001
                                2002-06-02  140  tmpclient
                                2002-06-04  139  thebar2001
```

Games (`kniffler`, `same`, `summergame`, `wintersame2001`, `wintertowers2001`,
`undosamegame`, `winterundo2001`, `magic2001`, `soccerq2001`, `soccerm2001`,
`thebar2001`), whole subsystems (`flirtbox`, `sms`, `np2001` for fun.news), and
the client and menu binaries updating themselves (`clientexe`, `menuexe`,
`maincom`, `menu2001`). The ids are not sequential in time — 111 arrives after
115 — so the server chose per cabinet what to send and when.

### The pipeline

Reading the strings of `CLIENT.EXE` in order, and `TRANSMIT.BAT` for the tail:

1. The downloaded script names an update by **id** and **name**.
2. `downloading update (%ld)` — the client CWDs to `/master/data/update` and
   retrieves it into `\FN_SYS\DFU\RESUME\<id>.upd`. That directory is called
   `RESUME` for a reason: the transfer is `SIZE` then `REST`, so a call that
   drops mid-update continues the next night instead of starting over. At
   57600 baud that is not a nicety.
3. `- download finished - decrypting:` then `success` or `failed` — the `.upd`
   is **encrypted**, and is decrypted into `\FN_SYS\UPDATE\<id>.exe`.
4. The client then writes `\FN_SYS\UPDATE\EXECUTE\UPDATE.BAT`, from literal
   fragments still in the binary:

   ```
   CD \fn_sys\update\execute\<id>
   \fn_sys\update\<id>.exe        <- self-extracting: unpacks into the cwd
   CALL start.bat                 <- the update's own installer
   \fn_sys\fn_sys.exe /confupd=<id>
   DEL /Q *.*
   DEL \fn_sys\update\<id>.exe
   CD \fn_sys\dfu
   ```

5. `TRANSMIT.BAT`'s last line is `call \fn_sys\update\execute\update.bat`, so
   it runs the moment the call ends.

So **an update is a self-extracting archive containing a `start.bat`**, and
`start.bat` may do anything DOS can — which is how a game arrives as
`\EXE\NETQUIZ1.EXE` plus a `\NETQUIZ1\` data tree, exactly the shape found on
this disk. `/confupd=<id>` is what appends the row to `FNUPDATE.TAB`, which is
also what gets reported back up on the next call so the server knows not to
send it twice. (`PKZIP.EXE` and `PKUNZIP.EXE` sit in `\FN_SYS\DFU\TRANS\`,
which makes a PKZIP self-extractor the obvious guess for the archive format.
Not confirmed.)

A game also needs to be *shown*, and that is a separate, smaller thing:
`NETGAME.TAB` carries the id, the directory name and the validity window. That
table comes down in the script, not in the archive.

### What blocks doing it today

Two things, and they are the same two that block everything else:

1. **The script grammar** — the `UPDATE` record that names the id and file.
2. **The `.upd` cipher.** No `.upd` file survives anywhere in the collection
   to attack it with, and the routine has not been disassembled. The only
   thread so far is the constant `0x08088405` — Turbo Pascal's LCG multiplier —
   at offset `0x15C47` in `CLIENT.EXE`. That is a lead, not a finding; it may
   equally be runtime code.

Both are downstream of one thing: **a captured `SCRIPT.UL`.** The cabinet
builds its upload from `\FN_SYS\UPDATE\FNUPDATE.TAB` among other tables, using
the *same* record vocabulary as the download — `UPDATE` is in that list. So a
single completed session shows how an `UPDATE` record is written, going up,
and by symmetry how to write one going down.

### The route that works now

For emulation there is a shortcut that needs neither answer. `start.bat` only
ever copies files into place, and `tools/imgput.py` already writes files into a
disk image. Installing a game into an emulated cabinet is therefore a matter of
putting `\EXE\<GAME>.EXE` and its data directory into the image and adding the
`NETGAME.TAB` row — no cipher, no script, no telephone. That is not the same
achievement as delivering it over the wire, and it is not what was asked for,
but it is the thing that can be done today while the two unknowns above wait on
a first real session.

## What was built

[**fun.net-server**](https://github.com/Xeon3D/fun.net-server) — a server the emulated modem dials, in Python 3
with no third-party modules, about 1,500 lines over five files.  It lives in a
repository of its own rather than in this tree, because nothing in it is
86Box-specific: it speaks to a byte pipe, so a real cabinet on a real modem
would work the same way.

* `ppp.py` — HDLC framing with FCS-16, LCP, PAP, CHAP, IPCP. Sends the first
  Configure-Request because the cabinet will not, rejects VJ compression,
  accepts and logs whatever credentials are offered.
* `ipstack.py` — IPv4, ICMP echo, UDP, and a TCP with active open, so the
  server can dial back into the cabinet for `PORT` data connections. It
  **answers for every destination address on the link**, not just its own,
  because `WATTCP.CFG` hard-codes dead nameserver addresses (and on the IGO 6
  image a dead `my_ip`) and those packets have nowhere else to go. A cabinet
  therefore needs no reconfiguration at all.
* `services.py` — a name server that answers everything with the one address
  there is, and the clock: RFC 868 on UDP/37 and TCP/37 and SNTP on UDP/123,
  all three, because which one the client speaks is not legible in the binary.
  `--date` serves an arbitrary date, for the validity windows above.
* `ftpd.py` — the FTP server, active mode included, over that stack. Missing
  files get a clean `550` rather than a stall; a cabinet told "no" moves on,
  a cabinet told nothing waits out its 60-second `CONNECTION TIMEOUT`.
* `funnetd.py` — the listener and session glue.

## What is verified, and what is not

**Verified mechanically**, by that repository's `selftest.py`, which stands a second
PPP peer and IP stack on the other end of a real socket and makes it behave the
way `CLIENT.EXE` does — 34 checks, all passing: LCP and IPCP come up, DNS
answers `ftp.nl.funsys.com` and the hard-coded backup name, all three time
services answer, `--date` is honoured, then a full FTP session with active-mode
`PORT` in both directions — `SIZE`, `RETR` of a 9 KB file across many segments,
`REST` resuming at an offset, `STOR`, the `RNFR`/`RNTO` rename `CLIENT.EXE`
performs, `LIST`, `DELE`, and a missing file refused rather than stalled.

**Not verified:** any of it against funworld's own client. Nothing here has
been dialled by a real cabinet yet. The protocol facts above are read out of
`CLIENT.EXE`, `TRANSMIT.BAT`, `NET.CFG` and `SETTINGS.TAB`; the implementation
is checked against a model of that client, which is not the same as the client.

**Not attempted:** the script grammar. Until a cabinet completes a session and
leaves a `SCRIPT.UL` behind, the server has nothing truthful to put in
`/master/outgoing/machine/`, and it says so by answering `550` and listing
every path that was asked for and missed.

## Open

* The first real session. Everything in the paragraph above turns into
  measurement the moment one cabinet dials.
* The exact filename under `/master/outgoing/machine/` — almost certainly keyed
  on `machlic`, but that is a guess until the miss list names it.
* Which time protocol is actually used. Serving all three hides the answer;
  a session log with `--raw` will show which one it asked for.
* **The `.upd` cipher**, and with it game delivery over the wire. No sample
  survives in the collection; the decrypt routine in `CLIENT.EXE` has not been
  disassembled, and `0x08088405` at `0x15C47` is the only thread.
* Whether the update archive is a PKZIP self-extractor. `PKZIP.EXE` and
  `PKUNZIP.EXE` ship in `\FN_SYS\DFU\TRANS\`, which is suggestive and no more.
* `isp.csv`, `isp_pop.csv`, `isp_user.csv` — 120 KB of dial-up accounts across
  the two images, still unread. `popid 965` and `providerpopid` index into them.
* Whether the 1998/1999 generations used the same protocol. They have `FN_SYS`
  trees too and were not read for this.
* `H:\Photoplay\Photoplay NET Server` is **not** this. It is a Windows Server
  2008 VHD pair serving the later SYSTEM releases, not the DOS I.G.O. cabinets;
  its 107 MB data disk carries none of the strings above. The fun.net side of
  the DOS era has left no server-side artefact in the collection, which is why
  everything here is read from the client.
