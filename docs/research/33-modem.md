# The modem on COM4

**The cabinet tells you which modem it had.** `\FN_SYS\FN_SYS.EXE` carries
funworld's entire modem database on the disk, and it carries the recipe for
recognising each part -- which `ATI` command to send and which substring to look
for -- so "emulate the modem" has an answer that can be checked rather than
argued about.

Two of those rows are the parts these machines are actually found with:

```
2  ELSA MicroLink 56k             9 11 -1 -1  ATi3
3  Diamond SupraExpress 56e PRO  15 -1 -1 -1  ATi7
```

Both are emulated.

Two images are used below, and they agree on everything that matters:

| | `IGO 6 DE ND003` | `PP2001NL-MASTERS-NSB H9751 SR1` |
|---|---|---|
| release | I.G.O. 6 (2006), DE | Photo Play 2001, NL |
| `MD_NAME.CSV` | 30 rows, 9 columns | 14 rows, 7 columns |
| `MD_INFOS.CSV` | 43 tokens | 20 tokens |
| `NET.CFG` | present, from a real dial | present, from a real dial |
| `TECHDATA.TAB` | absent | **present** |

Five years and five generations apart, and the wiring is identical.

## What the guest does with a modem

`FN_SYS.EXE` is the operator shell -- book-keeping, statistics, and the *data
transmission* that once uploaded them to fun.net. It is the only binary in the
tree that imports the full COM configuration API (`SetupComm`, `GetCommState`,
`SetCommState`, `SetCommTimeouts`, `PurgeComm`); `igo-02c-tdongle.md` § File
inventory noticed that back when it was ruling the DLL out as a dongle carrier
and filed it as "the modem on COM4". This is that thread, pulled.

The sequence, from the string table around `0x31941`:

```
"\r\r"      "AT\r"      "ATE0\r"
\fn_sys\database\network\md_infos.csv
\fn_sys\database\network\md_name.csv
...                                   "Standard-Modem"
```

Two bare carriage returns to wake the port, `AT` for an `OK`, `ATE0` to stop the
echo -- and then the two tables decide what the part is. A modem that matches
nothing is reported as `Standard-Modem`; a port with nothing on it gets
`ERROR: can't talk to modem`.

What it finds ends up on screen, and in `\FN_SYS\DATABASE\USER\techdata.tab`:

```
MODEM   : %s          -> MODEMTYP
FIRMWARE: %s          -> MODEMFMW
```

## MD_NAME.CSV -- the thirty parts

`\FN_SYS\DATABASE\NETWORK\MD_NAME.CSV`, tab separated:

```
id  name                                  tok tok tok tok  fmw    isdn gsm
 1  ELSA MicroLink 33.6TQV                  9  10  -1  -1  ATi3    0   0
 2  ELSA MicroLink 56k                      9  11  -1  -1  ATi3    0   0
 3  Diamond SupraExpress 56e PRO           15  -1  -1  -1  ATi7    0   0
 4  ELSA TanGo 1000                        12  13  -1  -1  ATi3    1   0
...
13  BestMatic Conexant 56k                 27  11  -1  -1  ATi6    0   0
14  Diamond SupraExpress 56e PRO V.92      15  28  -1  -1  ATi7    0   0
...
32  INSYS Pocket 56k Modem                 51  52  -1  -1  ATi6    0   0
```

Columns 3-6 are token ids into `MD_INFOS.CSV`; a row matches when **all** of its
non-`-1` tokens do. Column 7 is the command whose answer is filed as the
firmware.

## MD_INFOS.CSV -- how each token is tested

```
 9  ATi6  MicroLink        15  ATi3  SupraExpress     28  ATi3  V.92
10  ATi6  33.6             18  ATi6  Internet         39  ATi6  pro
11  ATi6  56               19  ATi0  WAVECOM          51  ATi0  56000
12  ATi6  TanGo            27  ATi3  P2107-V90        52  ATi1  255
14  ATi6  ISDN             34  ATi1  Voice            ...
```

So the two parts are recognised by different things, and hand back their firmware
from different places:

| | identified by | firmware from |
|---|---|---|
| **Diamond SupraExpress 56e PRO** (row 3) | `SupraExpress` in `ATI3` | `ATI7` |
| **ELSA MicroLink 56k** (row 2) | `MicroLink` **and** `56`, both in `ATI6` | `ATI3` |

The two swap the roles of `ATI3` and `ATI6` completely, which is why
`char_modem.c` keeps a model table with an `ident_at` and a `fmw_at` rather than
special-casing one part.

### The collision analysis

A row matches when all of its non-`-1` tokens do, so the question for each model
is not "does the right row match" but "does **only** the right row match". Every
`ATIn` answer was picked against the whole table with that in mind:

**Diamond SupraExpress 56e PRO**

| | answers | why that, and not something else |
|---|---|---|
| `ATI0` | `1794` | tokens on `ATi0` are `WAVECOM`, `1800`, `900`, `Nokia`, `1292`, `1.2`, `TA+PP2`, `TINTORETTO`, `251`, `MULTIBAND`, `900E`, `56000`. A product code collides with none. Notably **not** `56000`, token 51 |
| `ATI1` | `168` | tokens on `ATi1` are `Riser`, `Voice`, `SmartUSB56`, `255`. Notably **not** `255`, token 52 -- `56000` + `255` together are row 32 |
| `ATI2` | `OK` | no token tests `ATi2` |
| `ATI3` | `SupraExpress 56e PRO` | token 15, the one that matters. Carries no `V.92` (token 28 -- that is row 14, the same part's V.92 variant), and none of `Sportster`, `56000`, `Voice`, `P2107-V90`, `Nokia 30`, `TP560`, `Tornado`, `ISDN adapter`, `ZyXEL` |
| `ATI4` | `Diamond Multimedia SupraExpress 56e PRO` | tokens on `ATi4` are `Sportster` and `ISDN` -- neither present |
| `ATI5` | `Country Code: nn` | the only token on `ATi5` is `ROPER FLYING` |
| `ATI6` | `RCVDL56ACF/SP Rev 1.100` | contains `56`, which *is* token 11 -- harmless, because every row needing 11 also needs `MicroLink` (9, `ATi6`) or `P2107-V90` (27, `ATi3`), and neither is present. Deliberately carries no `pro` (39), `-i` (32) or `k i` (38) |
| `ATI7` | `V1.100-V90_2M_DLS` | not tested by any token; this is the firmware |

**ELSA MicroLink 56k**

| | answers | why that, and not something else |
|---|---|---|
| `ATI0` | `1442` | as above, avoiding `56000` |
| `ATI1` | `168` | as above, avoiding `255` |
| `ATI2` | `OK` | untested |
| `ATI3` | `2.274` | the firmware, and the reason it must be a bare version: `ATi3` is where `SupraExpress` (15), `Sportster` (24), `56000` (25), `Voice` (26), `P2107-V90` (27) and `V.92` (28) are looked for |
| `ATI4` | `ELSA MicroLink 56k` | tokens on `ATi4` are `Sportster` and `ISDN` -- neither present |
| `ATI5` | `Country Code: nn` | untested here |
| `ATI6` | `ELSA MicroLink 56k` | tokens 9 and 11 both hit, which is row 2. The four other MicroLink 56k rows each need one *more* `ATi6` substring -- `Internet` (18, row 7), `-i` + `k i` (32/38, row 18), `pro` (39, row 22) -- and row 1 needs `33.6` (10). None appear, so none of them can match |
| `ATI7` | `ELSA AG, Aachen` | untested |

`tools/modemtest` does not check this table by hand. It carries **both images'
`MD_NAME.CSV` and `MD_INFOS.CSV` transcribed as data**, implements the matcher,
runs it over what the built device actually answers, and asserts that exactly one
row matches and that it is the right one. Both models pass against both the
fourteen-row 2001 table and the thirty-row I.G.O. 6 one.

### What is measured and what is chosen

Measured: **`SupraExpress` in `ATI3`** for one part, **`MicroLink` and `56` in
`ATI6`** for the other, and which `ATIn` each one's firmware comes from. The
guest's own tables demand all of that.

Chosen: everything else. The remaining answers are plausible for the part and
*proven not to collide*, but they are not captures from real hardware. Both the
identification and firmware strings are settings -- hidden in the device dialog,
present in the ini, blank meaning "whatever the model table says" -- so a rig
with a real SupraExpress or MicroLink on a passthrough port can be made to agree
with it byte for byte. If that ever happens, the captured strings should become
the defaults and this section should say so.

## The wiring, from two real dial sessions

Both images still have the `NET.CFG` their last dial wrote, in `\FN_SYS\DFU\`.
`IGO 6 DE ND003` first:

```
LINK DRIVER PPP
    CONFIGURATION Current
    FRAME PPP
    PORT 02E8
    INT 10
    BAUD 57,600
    FLOW CONTROL HARDWARE
    CONNECTION MODEM
    OPEN PASSIVE
    DIAL 0,0718915252
    MODEM INIT1 "ate0m1l3S8=5S7=20S10=40x3"
    MODEM INIT2 ""
    MODEM INIT3 ""
    CONNECTION RETRIES 1
    CONNECTION TIMEOUT 60
    USER "funny150" "<32 bytes, obscured>"
    MODEM DIAL "ATDT"
```

and beside it `WATTCP.CFG`, with the address the cabinet was given:

```
my_ip = 195.170.72.135
netmask = 255.255.255.0
gateway = 195.170.72.254
nameserver=195.170.70.72 / .75 / 193.83.149.137 / 137.39.1.3
```

The 2001 NL machine, five years and five generations earlier, wrote the same
file with a different phone number and a different account:

```
    PORT 02E8
    INT 10
    BAUD 57,600
    FLOW CONTROL HARDWARE
    CONNECTION MODEM
    DIAL 0676077111
    MODEM INIT1 "ate0m1l3S8=5S7=20S10=40"
    MODEM INIT2 ""
    USER "funworld" "<32 bytes, obscured>"
    MODEM DIAL "ATDT"
```

**COM4 at 0x02E8, IRQ 10, 57600 8N1, RTS/CTS.** `PORT`, `INT` and `BAUD` are
literals in `FN_SYS.EXE`, so they are the same on every cabinet -- the file is
generated, not edited -- and two independent captures five years apart confirm
it. The IRQ is the reason `photoplay_com4_irq()` exists: the ODI PPP driver
installs an ISR on the interrupt `NET.CFG` names and then stops polling, so a
modem on the PC-standard IRQ 3 would identify perfectly and then die the moment
PPP started. Exactly the failure shape `photoplay_com3_irq()` was written for.

The link stack is Novell ODI -- `LSL.COM`, a `PPP` ODI driver, `PPPDIAL.EXE`,
then WATTCP on top and `CLIENT.EXE` doing FTP. All of it is in `\FN_SYS\DFU\`.

### Which part each cabinet had

`MD_INIT2.CSV` gives modem 3 an `AT%C3&K3BN1W2` on **every** one of its 51 ISP
rows, and gives modem 2 no row at all. So a cabinet running a SupraExpress
always writes that string into `MODEM INIT2`, and a cabinet running an ELSA
MicroLink 56k always writes `""`.

Both captured `NET.CFG` files have `MODEM INIT2 ""`. **Neither of these two
machines was running a SupraExpress.** That does not pin them to the ELSA on its
own -- eighteen of the thirty rows have no `MD_INIT2` entry -- but it rules row 3
out, and the ELSA is the one of the eighteen these machines are known to be found
with.

## TECHDATA.TAB -- what the machine wrote down about itself

The 2001 MASTERS image has the file FN_SYS files its findings in,
`\FN_SYS\DATABASE\USER\TECHDATA.TAB`. Its technical fields, verbatim:

```
PPSERIAL  205085
MODEMTYP  none
MODEMFMW
MD_INIT1  ate0m1l3S8=5S7=20S10=40
MD_INIT2
MD_INIT3
TELNUMB   0676077111
VERSION   Version 2001 (NL)
PROFILE   NL_EURO
DT_START  465 min          <- 07:45
DT_END    495 min          <- 08:15
HD_TOTAL  1585184768
HD_PHYS   503 MB
NSB_NR    H9751 SR1
TOUCHSCR  MICROTOUCH
MBOARD    unknown
```

Three things worth having:

* **`MODEMTYP` is `none`.** So `none` -- not `Standard-Modem` -- is what gets
  recorded when the port has nothing on it. `Standard-Modem` is for a modem that
  answers but matches no row. The modem had been unplugged by the time this
  machine was imaged, even though it had plainly been online for years.
* **`TOUCHSCR` is `MICROTOUCH`**, which is the same shape of field for the
  touchscreen and independently confirms the fork's default for a 2001 cabinet.
* `MD_INIT1` is the `**` row of `MD_INIT1.CSV` verbatim, with no `x3`. The `x3`
  on ND003's is therefore not something the software appends -- it was edited in,
  or that build's table differed.

## The init strings

Three tables, applied in order.

**`MD_INIT0.CSV`** -- country, keyed by territory *and* modem id, and the two
parts want different commands for it:

```
    modem 3 (Supra)              modem 2 (ELSA)
AT  AT*NC1Z    AT&F2&W           AT+GCI=0A  AT&F2&W
BE  AT*NC2Z    AT&F2&W           AT+GCI=0F  AT&F2&W
CH  AT*NC15Z   AT&F2&W           AT+GCI=A6  AT&F2&W
DE  AT*NC6Z    AT&F2&W           AT+GCI=04  AT&F2&W
DK  AT*NC3Z    AT&F2&W           AT+GCI=31  AT&F2&W
ES  AT*NC13Z   AT&F2&W           AT+GCI=A0  AT&F2&W
FI  AT*NC4Z    AT&F2&W           AT+GCI=3C  AT&F2&W
FR  AT*NC5Z    AT&F2&W           AT+GCI=3D  AT&F2&W
GR  AT*NC17Z   AT&F2&W           AT+GCI=46  AT&F2&W
IR  AT*NC7Z    AT&F2&W           AT+GCI=57  AT&F2&W
IT  AT*NC8Z    AT&F2&W           AT+GCI=59  AT&F2&W
NL  AT*NC10Z   AT&F2&W           AT+GCI=7B  AT&F2&W
NO  AT*NC11Z   AT&F2&W           AT+GCI=82  AT&F2&W
PT  AT*NC12Z   AT&F2&W           AT+GCI=8B  AT&F2&W
SI  AT*NC1Z    AT&F2&W           --
UK  --                           AT+GCI=B4  AT&F2&W
```

`AT*NCn` is the Rockwell country-select command, and the trailing `Z` is a
separate reset that makes it take; `+GCI` is the ITU one, with a T.35 country
code. Both are implemented.

**`MD_INIT1.CSV`** -- the port, keyed by territory only:

```
**  ate0m1l3S8=5S7=20S10=40
GR  ate0m1l3S8=1S7=20S10=40x3
```

Echo off, speaker on until connect at volume 3, comma pause 5 s, wait 20 s for
carrier, 1.4 s carrier-loss timeout. The 2001 machine's `TECHDATA.TAB` and
`NET.CFG` both carry the `**` row verbatim; ND003's carries it with `x3`
appended, which is neither table row -- operator-edited, or a build difference.

**`MD_INIT2.CSV`** -- the ISP, keyed by POP id and modem id. All 51 rows for
modem 3 are identical:

```
AT%C3&K3BN1W2
```

Compression negotiated both ways, RTS/CTS flow control, V.34-and-below
modulation, error correction on, and `W2` -- report the *carrier* speed in
`CONNECT`, not the port speed. **Modem 2 has no rows here at all**, which is the
fingerprint used above to rule the Supra out of both captured cabinets.

`MD_DWAIT.CSV` gives the dial-wait character: `,` for everywhere, `,,,` for GR.

## What was built

`src/char/char_modem.c` -- an 86Box character device on the serial bus, so it
attaches to a COM port the way the touchscreen and the dongle attach to theirs,
rather than through the network-card path 86Box's own generic Hayes modem uses.
It has:

* the Hayes command set the tables above exercise, plus `AT*NC`, `AT%C`,
  `AT&F/&W/&Y`, `AT+GCI` and `AT+MS`, so nothing in either part's init errors;
* a **model table** carrying both parts -- which `ATIn` holds the identification,
  which holds the firmware, and what every other `ATIn` answers. Adding a third
  modem out of the thirty is a row there and a row in `photoplay_modem_list()`;
* S-registers, `ATX` levels, `ATW`, `AT&C/&D/&K/&S`, `+++` with guard time, and
  DTR handling -- the things the PPP driver actually leans on;
* a line side that is **dead by default**. Dialling into it fails the way a real
  modem fails: `NO DIALTONE` under X2/X4, or a blind dial that waits out S7 and
  answers `NO CARRIER`. fun.net is gone; pretending otherwise would only make
  the cabinet hang for its 60-second `CONNECTION TIMEOUT`.
* an optional TCP host, in which case dialling anything connects there and the
  modem becomes a transparent pipe -- which is what the guest's PPP wants, and
  what a fun.net stand-in would need.

`photoplay.c` fits the chosen part to COM4 on IRQ 10; `[Photo Play] modem` holds
its device internal name, and an empty string -- the default -- means no modem.
Set from the **Tools -> Modem...** dialog and its toolbar button. Off by default:
plenty of cabinets never had one and no game needs one.

## What is verified, and what is not

Verified mechanically, by `tools/modemtest`. It transcribes both images'
`MD_NAME.CSV` and `MD_INFOS.CSV`, implements the cabinet's matcher, and runs it
over what the built device actually answers -- so the claim it checks is
"resolves to exactly one row, and it is the right one", not "contains the right
substring". Both models pass against both tables. It also drives the wake-up,
the firmware query, every init string in all three tables including both
captured `NET.CFG` ones, `ATS7?`, `ATX`-dependent dial failure, numeric result
codes, and an unknown command being refused.

Verified in a real boot of `IGO 6 DE ND003`: COM4 is created, the device
attaches to it, and the guest opens and reprograms the port.

**Not verified:** the operator menu's Data transmission page showing
`MODEM   : ...` with the right name in it. That needs someone in front of the
cabinet, in the operator setup, with the password. Until that has been seen on
screen, this is a modem that satisfies the guest's tables on paper.

## Open

* No captured `ATIn` output from either real part. `TECHDATA.TAB` would have had
  it, but the one image that has that file recorded `MODEMTYP none` -- its modem
  had been unplugged before it was imaged. Another online machine's
  `TECHDATA.TAB` would settle the identification and firmware strings outright.
* `\FN_SYS\KEYBOARD\<lang>\modem.csv` -- an on-screen keyboard layout for
  entering the init string by hand, not part of detection.
* The standalone COM ports are `ns8250`, not `ns16550`: no FIFO. That is fine
  for the AT conversation and is what COM3 has always been, but 57600 PPP into a
  FIFO-less UART is not what the cabinet had. If a fun.net stand-in ever gets
  far enough to matter, that is the next thing to look at.
* `isp.csv`, `isp_pop.csv` and `isp_user.csv` hold the dial-up accounts -- 49 KB
  and 66 KB of them on the 2001 machine. Not read here.
* The rest of fun.net. `\FN_MST\` is the MASTERS tournament client and the 2001
  MASTERS image has its data tables filled in from real play; `NETGAME1..5` and
  `NETQUIZ1..5` are the slots downloaded content landed in. None of that is the
  modem's business, but it is the thing the modem existed for.
