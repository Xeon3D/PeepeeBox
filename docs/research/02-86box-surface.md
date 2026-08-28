# Phase 2 — The second surface: hardware waits and 86Box

Discovered mid-execution (expected per `METHOD.md` guardrail 8). **None of this is dongle
protection.** It is code that waits on real cabinet hardware, and it is why some of the
reference "keyless" builds carry edits that `ppkeyless` deliberately does not make.

## The extra edits in the reference builds

`IGO7AT` (from `Reversing Information/Reversing/IGO7AT Differences/menu.exe differences.txt`,
describing a *different* IGO7 MENU build than the `-K` image supplied here) and
`IGO8ES-VM007-K` carry these on top of the dongle patch:

| site | original | patched | what it is |
|---|---|---|---|
| IGO7 `0xC9BD` | `74 13` | `EB 0F` | **wait-for-ESC loop.** `kbhit(); if(!key) loop; getch(); if(key!=0x1B) loop`. It sits *inside the dongle error screen* (between the error `jnz` and its `ok` target), so with the dongle patch applied it is unreachable. Patching it is harmless but pointless. |
| IGO7 `0x2099F` | `75 F4` | `EB 07` | **wait-for-hardware poll.** `do { poll(); } while ([0x60F0] == 0); if ([0x60F2] == 0) repeat;` — spins until a device counter moves. |
| IGO8 `0x5C0A` | `CD 21` | `CC 90` | Borland startup `mov ah,30h; int 21h` (get DOS version) replaced by `int 3; nop`. INT 3's default DOS handler just IRETs, so `_osmajor` ends up as 0x30 instead of the real PTS-DOS version — i.e. a **forced DOS-version spoof**. |
| IGO8 `0xC0D8` | `74 B9` | `90 90` | **the `{1,2,3,4}` retry loop** (below). |
| IGO8 `0xC0F9` | `75 04` | `EB 04` | skip a `push cs; call` when `[bp-6] == 1`. |
| IGO8 `0xC30D` | `75 18` | `EB 18` | skip *calling* the MENU-side dongle/hardware probe entirely. Redundant with the `0xC3FD` patch we do make. |
| IGO8 `0xC32D` | `74 05` | `90 90` | forces `[bp-2] = 1`, i.e. forces an error code — then `0xC3FD` suppresses the screen anyway. Redundant. |

## The `{1,2,3,4}` loop

Present in **every** `MENU.EXE` from 2001 through IGO8 (IGO6 `0xBF3C`, IGO7 `0xC50C`,
IGO8 `0xC0BF`, IGO4 `0xB3EE`, 2001 `0xB8F5`), and on the **normal** path, not the error
path:

```asm
loop:
    push 0 / push 0x10 / push ds / push OFFSET msg
    call far <show message>
    push 0 / push dword 0x01DF027F      ; 639,479 -> a full 640x480 rectangle
    push dword 0
    call far <draw>
    call far <delay/poll>
    les  bx,[far_ptr]
    cmp  word [es:bx],   1 ; jnz out
    cmp  word [es:bx+2], 2 ; jnz out
    cmp  word [es:bx+4], 3 ; jnz out
    cmp  word [es:bx+6], 4 ; jz  loop      ; <- spins while the array still reads 1,2,3,4
out:
```

It spins, redrawing a full-screen message, for as long as a four-word array still holds
its placeholder pattern `1,2,3,4` — i.e. **while a device has not yet supplied real
data**. Immediately before it there is a timeout loop that ends in

```asm
    mov ax,0 / mov es,ax / mov bx,0x472 / mov [es:bx],ax   ; BIOS warm-boot flag := cold
    push 0xFFFF / push 0 / retf                            ; far jump to FFFF:0000
```

— MENU.EXE **reboots the machine** when the hardware wait times out. Anyone chasing an
apparent "reboot loop" on 86Box should look here first, not at the dongle.

## Status

`IGO6AT-ND003-K` and `IGO7AT-MK002-K` — the user's own working keyless images — leave both
the wait-for-ESC and the `{1,2,3,4}` loop **unpatched**. So on those builds these loops are
not fatal, and `ppkeyless` correctly does not touch them. Whether a given cabinet build
needs them patched is a per-image question that should be answered by observation, not
applied blindly: each of these edits disables a real hardware wait, and doing that when it
was not needed can mask a genuine misconfiguration (wrong COM port, missing touch driver).

## The touchscreen — what actually matters

> ~~"the image loads the **ELO** driver while 86Box emulates a **MicroTouch**, and that
> mismatch is why touches do nothing"~~ — **falsified Phase 2**, as soon as a working
> reference was available. `\TOUCH\TCH_INIT.BAT` is byte-identical in IGO6 and IGO8:
>
> ```
> \touch\elo\ELODEV 2310,3,9600,3 -C1,4096,4096,1,1,255 -W0 -I65
> ```
>
> …and IGO8 *works*. So `ELODEV` simply fails silently on a machine with no ELO
> controller, and it is **`MENU.EXE` itself** that drives the MicroTouch over the serial
> port — which is why 86Box logs `Command received: AD / PN81 / AD / PL` regardless of
> which driver the batch file loaded. Both driver families ship in `\TOUCH\` (`ELO\`,
> `MT\`) because one binary set covers cabinets of both types. Do not "fix" `TCH_INIT.BAT`.

The variable that actually mattered was the **86Box side**:

| | `86box/` (touch dead) | `86boxv2/` (touch works) |
|---|---|---|
| build | 4.2 / 5876 | 5950 |
| touch device section | `[3M MicroTouch TouchPen 4]` | `[3M MicroTouch (Serial)]` |
| video | `cl_gd5426_isa` | `cl_gd5480_pci` |
| sound | `sbprov2` | `ess_es1688` |
| RAM | 32 MB | 16 MB |
| serial | COM3 | COM3 + COM4 |

Both set `mouse_type = microtouch_touchpen` and both put the controller on port 2 (COM3) at
9600 baud, so the emulated *device model* — `TouchPen 4` vs the plain serial MicroTouch in
the newer build — is the leading suspect, not anything inside the disk image.

## Open

- Which device backs the `{1,2,3,4}` array. Candidates: the MicroTouch controller, the
  coin/bill acceptor (`CoinsBills` strings), or the `\FN_SYS` "funnet" data link.
- `\MENU\TOUCH.INI` is absent in every supplied image, and `AUTOPTS.BAT` deletes it after
  each calibration run and restarts — so a fresh image always enters the calibration path
  on first boot. Pre-seeding a valid `TOUCH.INI` would skip it; the format is not yet known.
