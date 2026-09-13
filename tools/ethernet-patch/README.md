# Photo Play Ethernet patch

Gives a Photo Play cabinet disk image the ability to reach fun.net over an
**Ethernet card** (Realtek RTL8139) instead of the modem — and points a
PeepeeBox machine at a fun.net server, on this PC or on the internet.

```
python patch_image.py Harddisk.img
python patch_image.py Harddisk.img --config 86box.cfg --server funnet.example.org:8086 --secret funworld
python patch_image.py Harddisk.img --wattcp 192.168.200.20/24,192.168.200.1     # static instead of DHCP
python patch_image.py Harddisk.img --revert --config 86box.cfg                   # undo everything
python patch_image.py Harddisk.img --config 86box.cfg --machlic random           # its own cabinet identity
```

Python 3.8+ and the `capstone` module (`pip install capstone`; the binary
patcher disassembles the program to place its hooks).  Nothing else.

## What you get

On the cabinet, the service menu's dial-in settings wizard (*basic settings →
"How does the Photo Play dial to the network?"*) gets a **4th field,
"connection type"**, with a Modem / Ethernet chooser.  Pick Ethernet, confirm,
and from then on a data transmission loads the RTL8139 packet driver and talks
to fun.net over the card; pick Modem and it dials exactly as before.  The same
setting can be flipped from a DOS prompt with `\FN_SYS\DFU\SETETH ON` / `OFF`,
or at patch time with `--enable`.

## What the patcher changes in the image

A backup of the image is written first (`<image>.pre-ethernet.bak`).

| Path in the image | Change |
|---|---|
| `FN_SYS\FN_SYS.EXE` | patched (see below). Original kept as `FN_SYS\FN_SYS.ORG` |
| `FN_SYS\TEXT\*.CSV` | 4 text keys appended in every language (EN/DE/NL/FR/IT/ES translated, others English) |
| `FN_SYS\DFU\TRANSMIT.BAT` | Ethernet branch inserted (`FN_SYS /conntype` decides). Original kept as `TRANSMIT.ORG` |
| `FN_SYS\DFU\RTSPKT.COM`, `RTSPKT.TXT` | Realtek RTL8139 DOS packet driver v3.40 and its readme |
| `FN_SYS\DFU\SETETH.BAT` | `SETETH ON` / `OFF` / (no argument: show) |
| `FN_SYS\DFU\WATTCP.ETH` | WATTCP config used in Ethernet mode: `--wattcp dhcp` (default) or `--wattcp IP/PREFIX,GATEWAY[,DNS]`; `--keep-wattcp` leaves an existing one alone |

Nothing else in the image is touched, and the modem path is byte-for-byte the
original.  Running the patcher again on a patched image is safe (it upgrades
an older patch from the kept original).  `--enable` flips the cabinet to
Ethernet right away (`CONNTYPE=ETHERNET` in `SETTINGS.TAB`).

**`--revert`** restores `FN_SYS.EXE` and `TRANSMIT.BAT` from the kept
originals, removes the driver, helpers and `WATTCP.ETH`, strips the four text
keys and deletes the `CONNTYPE` record; with `--config` the card's mode goes
back to *none*.  Tested: after patch → revert the whole `FN_SYS` tree is
identical to the pristine image (the one exception is the settings table's
free slot, which comes back zeroed rather than with its stale deleted content
— the same thing to the program).

`FN_SYS.EXE` gains: a setting `CONNTYPE` (MODEM / ETHERNET) in
`SETTINGS.TAB`; a switch `FN_SYS /conntype` (errorlevel 1 = Ethernet) for
batch files; the wizard field; in Ethernet mode `/export` no longer probes
the modem (which used to abort the transmission with *"can't talk to modem"*)
nor rewrites `WATTCP.CFG`; and the *"Analysis of the last data transmission"*
page shows a modem response of `ETHERNET` in the label colour instead of the
red reserved for modem errors.

**Supported builds.** The binary patcher finds every hook by structure
(string references, instruction patterns, the program's own call sites), so
one patch serves all the `FN_SYS.EXE` builds in the collection — Photo Play
2001 and I.G.O. 1 through 8, compiled for 286 or 386 alike.  Eight distinct
builds have been patched and statically verified (`verify_patch.py`), and the
image tool only accepts those eight by hash: a build outside the list is
refused rather than attempted, because "located by structure" is not the same
as "tested".  `survey.py F:\HDDImages` lists what your images carry.
Releases without a fun.net client (1999, 2000, Touchtoy) have nothing to patch.

| `FN_SYS.EXE` | family | images |
|---|---|---|
| 177550 / 179040 bytes | 286 | Photo Play 2001 AT / DE / IT / NL / SP |
| 190336 / 191500 bytes | 286 | I.G.O. 1 and 2 IT / NL / GR / DE |
| 190966 / 191300 bytes | 386 | Photo Play 2001 NL MASTERS, I.G.O. 1 / 2 BE, I.G.O. 2 IT / NL / PT |
| 212766 bytes | 386 | I.G.O. 3, 4, 5, 6, 7, 8 and IGO Italy |

Runtime-tested so far: the 2001 NL MASTERS build (full sessions).  The other
seven pass the same static checks; see "Testing" below.

## The emulator side

PeepeeBox (with the network switch changes) always fits an RTL8139C+ as the
first network card.  What it is plugged into is the `[Network]` section of the
machine's `86box.cfg` — written for you with `--config`:

```ini
[Network]                              ; server on this same PC
net_01_card = rtl8139c+
net_01_net_type = nlswitch

[Network]                              ; server somewhere on the internet
net_01_card = rtl8139c+
net_01_net_type = nrswitch
net_01_nrs_host = funnet.example.org:8086
net_01_secret = funworld               ; only if the server uses one
```

(Keys are 1-based: `net_01_*` is the first card.)  The same can be set in the
emulator's *Network* dialog — Mode "Local Switch" or "Remote Switch" — but
note the emulator writes its config when it exits, so edit the file only while
it is closed.

## The cabinet's identity: `--machlic`

A cabinet's machine licence (`machlic`) is the emulated dongle's serial, and
PeepeeBox used to present the same one to every image (`00584F425050`,
"PPBOX").  With `--config`, `--machlic` sets `[Photo Play] machlic`:

* `--machlic random` or `--machlic pool:N` — an entry of the 2000-licence pool
  the fun.net stand-in knows (`machlic_pool.py`).  The patcher also registers
  the cabinet in `SETTINGS.TAB` (`licnumb` = the machlic, `password` = the
  machlic reversed), which is what the servers' password files list for every
  pool entry, so the operator-password check passes on the first call and the
  cabinet then gets `machine/_default` if no script of its own exists.
* `--machlic 00584F425050` — a fixed value; register the operator yourself.

PeepeeBox also accepts `machlic = random` / `pool:N` directly in `[Photo Play]`
(it writes the drawn value back), but then the operator registration is not
touched — use the patcher form when the cabinet should just work.

## The server side

`fun.net-server-ethernet/funnetd_eth.py` (a sibling of the dial-up
`funnetd.py`, sharing its FTP server, site tree, clock and capture logic):

```
python funnetd_eth.py --date 2002-06-01                            # local switch, this PC
python funnetd_eth.py --listen 8086 --secret funworld --date 2002-06-01   # internet
```

In `--listen` mode every cabinet that connects gets its own Ethernet segment,
DHCP lease and FTP session; only outbound UDP is needed on the player's side,
so home NAT is no obstacle, and the client's active-mode FTP works because the
server is the far end of the wire.  Open one UDP port on the host, hand out
the hostname and secret, done.

## Layout of this folder

| | |
|---|---|
| `patch_image.py` | the tool |
| `patch_fn_sys.py` | the FN_SYS.EXE binary patcher (documented inside); `--profile` prints what it located |
| `verify_patch.py` | static checks of a patched FN_SYS.EXE against its original |
| `survey.py` | lists the fun.net client builds in a folder of images |
| `bx.py` | small disassembly helper used while adapting the patch to a new build |
| `fat16.py` | minimal FAT16 read/write for the raw images |
| `payload/` | `RTSPKT.COM` + `RTSPKT.TXT` (from `rtspkt.zip`, Georg Potthast's DOS packet-driver page), `SETETH.BAT`, `WATTCP.ETH` |

## Testing a build

1. `python patch_image.py <copy of the image> --enable --config 86box.cfg --server 127.0.0.1:9086 --secret funworld`
2. `python funnetd_eth.py --listen 9086 --secret funworld --date 2002-06-01` in the server folder
3. Boot the machine.  Check, in this order: the menu comes up; the service
   menu's dial-in settings show four fields with the 4th reading *Ethernet*
   (`--enable` set it); a data transmission goes through the packet driver and
   the server log shows the session; the transmission report shows
   *ETHERNET* in blue.
4. `python patch_image.py <image> --revert` puts the image back.
