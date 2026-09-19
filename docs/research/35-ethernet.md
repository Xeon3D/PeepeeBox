# Phase 35 — Ethernet: fun.net without the modem

**No Photo Play ever had a network card, and the software knows nothing of
one.** The MASTERS image was searched end to end for a NIC driver — `RTL8139`,
`Realtek`, `NE2000`, `PROTOCOL.INI`, `*.DOS`, a Crynwr packet driver, an ODI
MLID for anything but a serial line — and there is none. What the cabinet ships
for fun.net is exactly this, in `\FN_SYS\DFU\`:

| | |
|---|---|
| `LSL.COM` | Novell ODI Link Support Layer 4.0, reads `NET.CFG` |
| `PPP.EXE` | Klos Technologies *"Async PPP MLID v1.49 (980111)"* — PPP over a serial port, `PORT 02E8 / INT 10 / BAUD 57,600`, i.e. the modem on COM4 (see [`33-modem.md`](33-modem.md)) |
| `IPSTUB.EXE` | Klos TCPTSR: presents the ODI board as a *packet driver* on a software interrupt |
| `PPPMENU`, `PPPSTATE`, `PPPWAT` | dial, wait for LCP/IP, write the negotiated address into `WATTCP.CFG` |
| `CLIENT.EXE` | the actual work: WATTCP-based FTP client |

And yet the whole thing turns on one string in `CLIENT.EXE`:

```
ERROR: only Ethernet or SLIP packet drivers allowed
```

`CLIENT.EXE` is a plain Waterloo TCP program. It looks for a packet driver on
INT 60h–80h, reads `WATTCP.CFG`, and does not care whether the driver underneath
is `IPSTUB` on a PPP link or a real card. It even carries WATTCP's BOOTP/DHCP
client (*"Configuring through BOOTP/DHCP"*). So a cabinet with an Ethernet card
needs no new client at all — only a packet driver, and a `FN_SYS.EXE` that stops
insisting on a modem.

This phase did that, then gave PeepeeBox the card and the wire, and the
fun.net-server the other end of it. A MASTERS cabinet has since completed full
sessions — DHCP, clock, FTP download of the country and machine scripts,
`SCRIPT.UL` upload — over the emulated card, both to a server on the same PC and
through the remote switch.

## 1. Why the modem was in the way: `FN_SYS.EXE /export`

`TRANSMIT.BAT` calls `FN_SYS /export` before it loads any network stack, and that
routine (Borland C++ 3.x, medium model; the function at `4B3:7893`) does three
things a card cannot live with:

1. it **deletes** `NET.CFG` and `WATTCP.CFG`;
2. it **probes the modem** on COM4 (`AT`, `ATE1`, the init strings from
   `MD_INIT0.CSV`), and when nothing answers prints `ERROR: can't talk to
   modem` and returns errorlevel 1 — which `TRANSMIT.BAT` turns into `goto
   FEHLER` before anything else happens;
3. it **rewrites** both files from the ISP tables in `\FN_SYS\DATABASE\NETWORK\`
   (`ISP.CSV`, `ISP_POP.CSV`: dial-in numbers and PPP accounts per country) —
   hard-coded `LINK DRIVER PPP / CONNECTION MODEM`, so a hand-edited `NET.CFG`
   would not survive a transmission anyway.

There is no LAN, DHCP or "static IP" choice anywhere: the operator UI knows
fourteen modems (`MD_NAME.CSV`) and a list of ISP points of presence, nothing
else.

## 2. The FN_SYS.EXE patch

Done as a reproducible script (`patch_fn_sys.py` in the *photoplay-ethernet-patch*
tool; the analysis is in the MASTERS workspace,
`reverse-engineering/fn_sys_ethernet/README.md`). In outline:

* a new setting **`CONNTYPE`** (`MODEM` / `ETHERNET`) in `SETTINGS.TAB`, a
  generic key/value store the program already reads with a variadic
  `get_FNsetting(key, buf, …, NULL)`;
* a new switch **`FN_SYS /conntype`** → errorlevel 1 when Ethernet, for batch
  files;
* in Ethernet mode `/export` skips the modem probe (resuming exactly where the
  *"can't talk to modem"* path resumes, minus the error return) and neither
  deletes nor rewrites `WATTCP.CFG`;
* the touch-screen dial-in settings wizard (*basic settings → "How does the Photo
  Play dial to the network?"*) gets a **4th field, "connection type"**, whose
  chooser is a byte-for-byte clone of the pulse/tone dialog with the text keys
  and result characters swapped. The three original fields are re-laid out at a
  48 px pitch so all four stay above the on-screen keyboard.

The binary has no free space at all — every zero run in `DGROUP` is live
data — so the patch inserts a 1.5 KB code segment in front of `DGROUP` and
rewrites all 3254 MZ relocations to shift it, grows `DGROUP` by 512 bytes by
patching the two C0 start-up constants (`0x6874 → 0x6A74`) to get a zeroed
scratch area, and de-duplicates two string literals Borland emitted twice for
the two new strings. Seven hook sites, all 5–9 byte far calls. Four text keys
(`INP_conntype`, `INP_conntype_exp`, `conn_modem`, `conn_ethernet`) go into
`\FN_SYS\TEXT\*.CSV`.

## 3. TRANSMIT.BAT and the driver

The patched script asks `FN_SYS /conntype` after `/export` and branches:

```
RTSPKT 0x60                 Realtek RTL8139 packet driver, INT 60h
CLIENT                      unchanged
RTSPKT -u                   unload; games keep their conventional memory
```

with `WATTCP.ETH` (`my_ip = dhcp`) copied over `WATTCP.CFG` first. The driver is
Realtek's own `RTSPKT.COM` v3.40 — Crynwr-skeleton based, `-u` uninstall, packet
interrupts 60h–66h/68h–6Fh/78h–7Eh. On the emulated RTL8139C+ it reports
`IRQ 0xA, I/O 0x6000`, MAC `00:E0:4C:80:22:50`. The modem branch is untouched,
and `SETETH ON|OFF` flips the setting from a DOS prompt.

## 4. What PeepeeBox does about it

**The card.** `photoplay_apply_profile()` fits an RTL8139C+ as the first
network card (`pp_apply_network()`, `PHOTOPLAY_NIC`) when `[Photo Play]
network = 1`. From 1.9 to 1.10 it was fitted always; after 1.10 it is **off by
default**, like the drives, since no cabinet had one, and choosing it as NIC 1
in the Network dialog is what switches it on. The flag is its own key because
every config saved while the card was unconditional names it in `[Network]`.
What the card is plugged into stays the user's choice in the Network dialog and
persists in `[Network]` as usual, card or no card; an unset type becomes the
local switch. Note the config
keys are **1-based**: `net_01_card`, `net_01_net_type`, … — `net_00_*` is
silently ignored, which cost an afternoon.

**The wire, part one — the local switch.** 86Box 5's `net_switch.c` sends every
Ethernet frame as one UDP datagram to port 8086: broadcast on the loopback
interface (`127.255.255.255`), multicast `239.255.86.86` (or `.80.86` with a
secret) on the others. Any process on the host that binds `0.0.0.0:8086` with
`SO_REUSEADDR` sees the cabinet's frames and can answer with loopback
broadcasts. No Npcap, no drivers. (Windows delivers each broadcast to a listener
several times, once per interface the emulator sent it on; a receiver has to
de-duplicate.)

**The wire, part two — the remote switch.** `NET_TYPE_NRSWITCH` was a config
placeholder: parsed, offered nowhere, and the driver ran the local code for it.
It is now implemented in `net_switch.c`:

* `net_01_nrs_host = host[:port]` (default port 8086) is resolved once at init;
* the receive socket binds an ephemeral port and every frame is sent **from that
  socket** as unicast to the peer, so replies — and the NAT mapping on the way —
  come back to it;
* datagrams from any other address are dropped;
* an empty datagram goes out every 20 s (and once at init) as a NAT keep-alive;
  every receiver already ignores anything shorter than a MAC pair.

The frame format is unchanged from the local switch: optional 32-byte
SHA3-256(secret) prefix, then the raw frame. "Remote Switch" is now offered in
the Network dialog (it was compiled out behind `ENABLE_NET_NRSWITCH`).

## 5. The other end: fun.net-server-ethernet

The dial-up `funnetd.py` terminates PPP and runs its own IPv4 stack, DNS,
clock and FTP on top, "answering for every address on the link". The Ethernet
sibling keeps all of that and swaps only the bottom layer (`ether.py`): a switch
transport, ARP that answers for every address but the cabinet's own, and a
one-lease DHCP server whose router and nameserver are the server itself. Two
transports:

* `LocalSwitch` — the loopback-broadcast scheme above, one cabinet on this PC;
* `Listener` — `--listen [host:]port [--secret …]`: a public UDP port, one
  isolated segment/stack/lease/FTP session **per remote (address, port)**, idle
  links expire.

This is the only shape an internet fun.net can take, because `CLIENT.EXE` does
**active-mode FTP** (`PORT`, no `PASV`): a real server on the internet can never
connect back into a player's home NAT, while a server that *is* the far end of
the cabinet's Ethernet segment connects "back" over the same tunnel. Only
outbound UDP is needed on the player's side.

## 6. The machine licence, and a pool of cabinets

`SETTINGS.TAB` carries a `machlic`, and fun.net addresses the cabinet by it
(`/master/outgoing/machine/<machlic>`, `/master/incoming/<machlic>.ul`).  It is
not typed in anywhere: `FN_SYS.EXE` regenerates it on every `/export` from the
**dongle's iButton ROM serial** -- six bytes read through LPT1, written as
twelve hex digits in reverse byte order -- and a changed value means a new
machine (`TECHDATA.TAB` and `COUNTER.TAB` wiped, send status reset).

PeepeeBox's dongle presented a fixed serial, `50 50 42 4F 58 00` -- "PPBOX"
-- because the games only check the ROM's CRC; so every image on every
PeepeeBox was cabinet `00584F425050` to a fun.net server.  It is now a
`[Photo Play]` setting:

| `machlic =` | effect |
|---|---|
| `00584F425050` (12 hex digits) | fixed, in the cabinet's own notation |
| `random` | an entry of the pool below, drawn at start and written back so it persists |
| `pool:N` | entry N of the pool |
| absent | the old PPBOX default |

The dongle dialog (the toolbar's settings button) shows the licence in force and
the dongle serial it becomes, says whether it is the shared default, a pool
entry or a licence of its own, and has **Random from the pool** and **Default**
buttons; the version and territory options moved behind its **Options…**
button.

**The pool.** 2000 licences shared with the fun.net stand-in:
`pool(i) = SHA3-256("PeepeeBox machlic i")[:6]` as upper-case hex
(`photoplay_machlic_pool()` here, `machlic_pool.py` there; entry 0 is
`49A182E59D7E`).  The point is the operator-password check: before the first
real session a cabinet fetches `/master/outgoing/password/<cc>` and takes only
a line whose fields equal its own `licnumb` and `password` settings.  A pooled
cabinet registers itself as `licnumb = machlic`, `password = machlic reversed`
(the image patcher's `--machlic random|pool:N` writes those into
`SETTINGS.TAB` next to the config's `machlic`), and the servers' password files
carry all 2000 such lines for every territory (`mkpool.py`) -- 52 KB, under
the client's 65,520-byte load limit.  A cabinet whose machine script does not
exist is served `machine/_default` instead, so it still passes the system
check and uploads.

## 7. Verified, and not

Verified on the MASTERS image (I.G.O. 1 NL, `FN_SYS.EXE` 190,966 bytes):

* boot, menus and the service wizard with four fields; Modem/Ethernet chosen
  and saved from the touch screen;
* full sessions over the local switch and over the remote switch with a secret
  (DHCP lease, RFC 868 time, FTP login `pp`, 68 KB country script, machine
  script, upload + rename, `QUIT`, driver unloaded), captures decoded;
* a bad secret is ignored; two cabinets on one listener get separate leases.

Not done: any other release. The `FN_SYS.EXE` hooks are at fixed offsets and
the image patcher refuses an unknown build rather than guess; I.G.O. 2/3 and
2002+ images need the same analysis redone. Nor has the remote switch been
tried across a real WAN yet — only loopback — so the keep-alive interval and
the small TCP window of the server's stack (tuned for a 57.6 k serial link)
are still assumptions.
