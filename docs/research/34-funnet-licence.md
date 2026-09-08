# The fun.net licence number

**It is a self-check, and there is no secret in it.** The registration page in
the operator setup asks for a licence number in the form `NL-893-VISS-461OFK`,
and refuses it with `wronglicnumb` if it does not check out. The check is three
letters at the end computed from the twelve characters in front of them, with a
sum, an XOR by position, and a modulo 26. Nothing about the machine goes into it
-- not the serial number, not the dongle, not the date -- so one algorithm
generates a number every cabinet of every release will accept.

`tools/fnlic` checks and generates them.

## Where the registration lives

`\FN_SYS\FN_SYS.EXE`, the operator shell. The page collects the pub's address
(`pubname`, `pubstreet`, `pubplz`, `pubcity`, `pubcountry`, `publng`) and then
three fields:

| field | mask | keyboard | on failure |
|---|---|---|---|
| `licnumb` | `LLAAAAAAAZZZLLL` | `alphanum.csv` | **`wronglicnumb`** |
| `password` | `AAAAAAAA` | `alphanum.csv` | `plsenterpwd` (empty only) |
| `ppserialnumb` | `ZZZZZZZZ` | `zehner.csv` | `plsenterpp` (empty only) |

Only the first is checked. The password is tested with `cmp byte ptr [bp-0xa], 0`
-- non-empty, nothing more -- and the serial number is mask-checked and prefilled
from `PP_serialnumber`. Both are simply posted to the server.

The mask letters are the input routine's own: `L` a letter, `Z` a digit (German
*Ziffer*, and its keyboard is `zehner.csv`, the numeric pad), `A` alphanumeric.
The display template beside the mask is `__-___-____-______`, which is where the
2-3-4-6 grouping comes from.

## The routine

Image offset `0x626E` in the 2001 build, 458 bytes, called from the registration
page at `0x6739`:

```
006734  lea  ax, [bp - 0x20]     ; the buffer the operator typed into
006737  push ax
006738  push cs                  ; push cs + near call = far call, so it can retf
006739  call 0x626e              ; <- the validator
00673c  pop  cx
00673d  neg  ax                  ;
00673f  sbb  ax, ax              ;  si = (ax == 0)
006741  inc  ax                  ;
006742  mov  si, ax
006744  or   si, si
006746  je   0x6771              ; ax != 0 -> valid, skip the complaint
006748  ...  push "wronglicnumb"
```

so **AX non-zero means valid**.

### The shape test

Fifteen `cmp byte ptr [si+n]` pairs, one per character, exactly matching the
mask:

```
[0], [1]        0x41..0x5A     'A'..'Z'
[2] .. [8]      0x30..0x5A     '0'..'Z'
[9], [10], [11] 0x30..0x39     '0'..'9'
[12],[13],[14]  0x41..0x5A     'A'..'Z'
```

Note that positions 2..8 are a **range** check, not an `isalnum()`. The seven
characters between `'9'` and `'A'` -- `: ; < = > ? @` -- pass on real hardware.
`tools/fnlic` reproduces that rather than tidying it up.

Any failure jumps to `0x6431`, which is `xor ax, ax` / `retf`.

### The three check letters

```
006355  mov  dword ptr [bp-4], 0        ; sum
00635d  xor  cx, cx                     ; i = 0
006361  mov  bx, cx
006363  mov  al, byte ptr [bx+si]       ; s[i]
006365  cbw                             ; AH = 0; s[i] < 0x80
006366  xor  ax, cx                     ; s[i] ^ i
006368  movsx eax, ax
00636c  mov  edx, dword ptr [bp-4]
006370  add  edx, eax
006373  mov  dword ptr [bp-4], edx
006377  inc  cx
006378  cmp  cx, 4
00637b  jle  0x6361                     ; i = 0..4
00637d  mov  eax, dword ptr [bp-4]
006381  mov  ebx, 0x1a                  ; 26
006387  cdq
006389  idiv ebx
00638c  add  dl, 0x41                   ; 'A' + remainder
00638f  mov  byte ptr [bp-0xe], dl      ; c1
```

then the same shape twice more, and the comparison:

```
0063 9A  mov  cx, 5                     ; c2: i = 5..11
0063 A4  mov  dx, cx / add dx, -5       ;     XOR key is (i - 5), not i
0063 DD  xor  cx, cx                    ; c3: i = 0..11, XOR key is i

006412  mov  al, byte ptr [si+0xc] / cmp al, [bp-0xe]   ; s[12] == c1 ?
00641a  mov  al, byte ptr [si+0xd] / cmp al, [bp-0xd]   ; s[13] == c2 ?
006422  mov  al, byte ptr [si+0xe] / cmp al, [bp-0xc]   ; s[14] == c3 ?
00642c  mov  ax, 1                                       ; all three -> valid
```

So, with `s` the fifteen characters:

```
c1 = 'A' + (sum of  s[i] XOR i        for i in  0..4 ) mod 26
c2 = 'A' + (sum of  s[i] XOR (i - 5)  for i in  5..11) mod 26
c3 = 'A' + (sum of  s[i] XOR i        for i in  0..11) mod 26
```

`idiv` is signed, but every term is a small positive number, so the remainder is
always 0..25 and the check letters are always `'A'..'Z'`.

One disassembly note: capstone prints the bare `0x98` at `0x6365` as `cwde` even
in 16-bit mode. It is `CBW` -- `AL` sign-extended into `AX` -- so `AH` is zero for
any input the shape test allows, and the sum is over plain byte values.

## Verification

The 2001 MASTERS cabinet was registered, and its
`\FN_SYS\DATABASE\USER\SETTINGS.TAB` still holds what the operator typed:

```
licnumb        NL893VISS461OFK
ppserialnumb   205085
```

Running the algorithm over the first twelve characters:

```
c1  N^0=78 + L^1=77 + 8^2=58 + 9^3=58 + 3^4=55                    = 326  mod 26 = 14 -> 'O'
c2  V^0=86 + I^1=72 + S^2=81 + S^3=80 + 4^4=48 + 6^5=51 + 1^6=55  = 473  mod 26 =  5 -> 'F'
c3  (the twelve, XOR by position)                                 = 842  mod 26 = 10 -> 'K'
```

`OFK` -- which is exactly what the last three characters of the stored number are.
All three reproduce on the only known-good sample. `tools/fnlic.py selftest`
asserts that, and that 20,000 generated numbers all verify.

## Across releases

The 458 bytes of the validator are **byte-identical** in the 2001 build
(`PP2001NL-MASTERS H9751 SR1`, image `0x626E`) and the I.G.O. 6 build
(`IGO 6 DE ND003`, file `0xA899`). Five years and five generations, unchanged.
The mask string, the error string and the display template are present in both.

## What passing it actually gets you

Not a fun.net connection. The registration page writes the pub address and the
three fields into `SETTINGS.TAB` and raises two flags:

```
sendstatus_LOCADR      the location address is queued to be sent
sendstatus_GENLIC      a licence generation request is queued
```

The next data transmission posts those, and the *server* answers with the machine
licence -- `machlic`, a 12-character value (`5E7000022726` on the 2001 machine,
`5E700037862B` on the IGO 6 one), plus `machlic_file_ok`. That half needs
fun.net, which is gone.

So the licence number check is the **first gate, not the last one**. Getting past
it is what lets a cabinet with no telephone line reach the rest of the
registration flow and be inspected; it is not a way to become a registered
machine. Anything downstream of `machlic` is a separate problem, and probably a
much harder one, since that value does come from a server.

## Open

* What `machlic` is. Two samples, both 12 hex-ish characters, both starting
  `5E70`. Not enough to say anything.
* `CODE_TRANSMIT` and `CODE_PPNSETUP` in `SETTINGS.TAB` -- eight-digit codes,
  presumably operator PINs for the transmission and setup pages. Not looked at.
* Whether `MENU.EXE` or the other fun.net apps have their own gates. Only
  `FN_SYS.EXE` carries the licence strings, so probably not, but that was not
  checked exhaustively.
* The 1999 and 2000 generations were not examined; they predate fun.net.
