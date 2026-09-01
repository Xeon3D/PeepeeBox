# Phase 24 — Photo Play 2.0 and Microcosm CopyControl

Photo Play 2.0 has no dongle. It is protected by **Microcosm CopyControl v1.66
build 94**, and its hard-disk key is not in any file — it is the **physical
layout of the disk**: where two files sit, and a pattern hidden in FAT cluster
slack. Imaging a cabinet's disk file-by-file destroys both, which is why every
2.0 image found so far refuses to start a game.

**It runs.** All three checks pass on the original, unmodified game files, and
PeepeeBox now detects and repairs an affected image by itself (§ 8).

Images: `PP20NL-Asure_` (original from a machine) and `PP20NL Keyless` (shells
stripped — the pre-existing crack, which also runs).

## 1. What is protected

Exactly **21 files**, all in `\EXE\`, each with a ~5.3 KB appended stub:

```
STONES   135330 -> 130000     FINDIT   122505 -> 117168
SHANGHAI 122960 -> 117616     TOWERS   111862 -> 106528
SOLI_A   115153 -> 109824     MINE     103674 ->  98336     ... 21 in all
```

`MAIN\MAIN.COM` is **not** protected (4790 bytes in both images), so a failure
shows when a game is launched, not at boot.

The stub sets the entry to `CS:0682` with `SS:SP` in the same segment and lives
at the end of the file: for `MINE.EXE`, `CS*16 + 32 = 98336`, exactly the size of
the keyless build. It also **encrypts the first ~0x978 bytes** of the real
program — over the 98,336 bytes the two builds share, only 1,359 differ and all
below `0x978`. So the originals genuinely cannot run unless the check passes;
there is no "serve the right answer" equivalent to the dongle work.

## 2. The engine is `PP2000.CCC`, not the executables

`EXE\PP2000.081\` holds `CCONTROL.SYS` (201 bytes) and `PP2000.CCC` (17,347),
both `.RHS`. **`PP2000.CCC` is the CopyControl engine** — code, a numbered
service dispatcher, and message tables in seven languages. `CCONTROL.SYS` is a
header: `01 42` = v1.66 at bytes 0–1, build 94 at `0x7C`, PCODE `PP2000` at
`0x80`.

Two consequences, both of which cost time before they were understood:

* **Patching the stub's strings does nothing** — the messages come from the
  engine's own copy. `Run CCMOVE to create a working copy` was patched to
  `XYZ CCMOVE` in `MINE.EXE`, verified on disk by two independent FAT readers and
  verified present in RAM, and the screen still said `Run CCMOVE`.
* **The licence region `0x000–0x5FF` is encrypted on disk** and decrypts in RAM.
  `Run CCMOVE` lives at engine offset `0x2AA`, inside it.

The decrypted licence, from a memory snapshot:

```
+0000  01 42 00 00 | 21 08 | 14 00 | 01     v1.66; CC serial 0x0821; product serial 0x0014
+0010  "PP2000"        ... 4E 00 00 E8 03
+0022  FD D4                                 = 54525, PP2000.CCC's recorded cluster
+0027  "D:\EXE\PP2000.081\"                  the install path CCMOVE recorded
+0069  01 00 "PROGRAM.EXE" 00 00 4E 00 00 FF FF
+0090  nine more 0x20-byte slots, each "4E 00 00 FF FF"
```

**The code self-decrypts.** The engine points the **INT 1 (single-step) vector**
at `0x1225` (`mov ax, 0x2501` at `0x1317`) and runs under the trap flag; the
handler keeps a rolling **14-byte plaintext window**, re-encrypting behind itself
(`0x1253`: XOR 14 bytes with a key from `cs:[4] + cs:[6]`). Constants such as
`mov ax, 0xFFEB` therefore exist **nowhere** on disk or in a memory dump — only
for the instant they execute. Static search for them is futile; only an
instruction trace recovers the real code.

## 3. The service dispatcher

At engine offset `0x127A`, selected by `bp`:

| service | routine | what it does |
|---|---|---|
| 2 | `0x36FA` → `0x3A4F` | return a file's **start cluster** via an extended-FCB search |
| 4 | `0x2C10` | the `CONTROL.TMP` media test |
| 6 | `0x3D82` | **the cluster-slack key** — § 4 |
| 3, 5, 8, 9 | `0x2EFC`, `0x2AC8`, `0x4322`, `0x3517` | |

The engine reaches DOS through its own wrapper (`int 21h` at `0x13DA`, `neg ax`
on the error path), so there are **no `CD 21` bytes to find statically**.

## 4. Check one — the slack key (error 152)

Service 6, at `0x3D82`:

```
03D90  al = cs:[0x39C4 + n]        expected fill byte, from an 8-entry table
03DA5  4300 get attributes ; 4301 clear them
03DD4  3D02 open read/write ; 4202 seek END -> size
03DF9  ah=36 free space -> cluster size ; size modulo cluster = slack, capped at 0x200
03E63  4200 seek EOF+len ; 4000 write 0     -> EXTEND into the slack
03E8C  3F00 read the slack into cs:0x37B2
03EA4  4200 seek size ; 40 write 0          -> truncate back
03EBD  3E close ; 4301 restore attributes
03EEC  al = cs:[0x39C3] ; repe scasb
03EFD  jcxz -> ax = 0                        every byte matched: PASS
03EFF  mov ax, 0xFF68                        otherwise: error -152
```

Live values, read out of the running engine:

```
cs:[0x39C3] expected fill = 5A      table at 0x39C4 = FF E5 1A F6 20 5A A5 FF
[0x39BB] size = 0x43C3 = 17347      -> PP2000.CCC
[0x39BF] slack offset = 32256       [0x39C1] slack length = 512
[0x39B9] DOS error flag = 0000      -> purely a content mismatch
```

**Fix:** fill the last 512 bytes of `PP2000.CCC`'s cluster with `0x5A`. Nothing
in any file changes — slack lies past EOF. Error went **152 → 021**.

## 5. Checks two and three — start clusters (errors 21, 22)

Service 2 (`0x3A4F`) does an extended-FCB find-first (attribute `0x06`) and
returns the directory entry's **start cluster** field:

```
03AA3  mov ah,0x11 ; dx=0x36FD    extended FCB: attr 06, drive 3, "PP2000  CCC"
03AB9  cmp al,0 ; mov ax,0xFFC4   -60 if not found
03AC0  mov ax, cs:[0x37D4]        else the START CLUSTER (DTA+34)
03ACA  mov es:[bx], ax            handed back to the caller
```

The comparison, recovered from the instruction trace (it is encrypted at rest):

```
22B5  2E A1 5F 06      mov  ax, cs:[065F]      ; the file's actual cluster
22B9  2E 3B 06 10 06   cmp  ax, cs:[0610]      ; expected, engine copy
22BE  74 1B            je   22DB               ; pass
22C0  2E C4 3E 29 11   les  di, cs:[1129]      ; -> the stub's control block
22C5  26 3B 85 C2 00   cmp  ax, es:[di+00C2]   ; expected, stub copy
22CA  B8 EB FF         mov  ax, 0FFEBh         ; error -21
22CD  75 46            jnz  2315
```

Expected values, read live: `cs:[0x0610]` = 0, stub `[0x2D7+0xC2]` = **0xD4FB =
54523**. Either copy satisfies it.

**Fix:** relocate the files to the clusters `CCMOVE` recorded.

| file | image had it at | licence wants | source of the value |
|---|---|---|---|
| `CCONTROL.SYS` | 11004 | **54523** | stub block `+0xC2`, live |
| `PP2000.CCC` | 11007 | **54525** | decrypted licence `+0x22` |

Error went **021 → 022 → the game runs**. The slack key must travel with the
file: the check reads whatever the file's *current* last cluster is.

## 6. What the traces show

A complete launch, once the layout is right:

```
\EXE\MINE.EXE                    204 sectors
\EXE\PP2000.081\CCONTROL.SYS     read
\EXE\PP2000.081\PP2000.CCC       read in full
W \EXE\PP2000.081                directory only - attribute clear, size extend
R  the slack sector              the key
W \EXE\PP2000.081  x2            truncate back, attributes restored
```

No file **data** cluster is ever written, no sector outside the filesystem is
ever read, and the only ATA `IDENTIFY` is at POST. Nothing is physically
uncopyable — the trick is entirely in *where things sit*.

## 7. Reading the error numbers

The stub prints the number when `shell+0x2F4` has bit 7 set (`test [bx+0x1d],0x80`
at `0x139F`); it ships `0x00`, so only the text shows. Setting it to `0x80` in a
test copy turns every failure into a labelled signpost, and is how this phase was
navigated: **152 → 021 → 022 → running**. The printer negates, so a displayed
`152` means `ax = 0xFF68`.

## 8. What PeepeeBox does about it

`pp_check_copycontrol()` in `src/photoplay.c` runs once, before the machine
starts. It parses the image's FAT16, and if `EXE\PP2000.081\` holds both
`CCONTROL.SYS` and `PP2000.CCC` it is a 2.0 disk. The `.CCC` header is plaintext
and identifies the install — CopyControl serial at `0x04`, product serial at
`0x06` — which keys a table of the values that install expects. If the files are
off their clusters, or the slack pattern is missing, it puts them back and tells
the user with a message box.

```c
{ 0x0821, 0x0014, 54523, 54525, 0x5A, "PP20NL" }
```

Only two directory entries, two FAT entries and slack space are ever written. No
game file is modified, and no other release has that directory, so nothing else
is touched. An install with no table row is reported in the log and left alone
rather than guessed at.

**The table has one row because the licence cipher is unbroken.** Its keystream
has no period and 255 distinct byte values over 1,536 bytes — a real stream
cipher keyed per install. Recovering another image's values means tracing it the
way this one was traced. Breaking the cipher would make the repair fully general
and is the obvious next piece of work.

Unrelated packaging note: the NVRAM shipped in Release 1.5 describes a 1.65 GB
disk, the size of every other cabinet image. A 2 GB 2.0 image boots to a BIOS
that reports **1655 MB** and then fails. A 2.0 rig needs its own `nvr/` or a
one-time IDE auto-detect in BIOS setup.

## 9. Method note

Five hypotheses in this phase came from real evidence in real code and were all
wrong: the recorded `D:` path, the system date, an absent floppy drive, the
`PP2000.CCC` start cluster considered on its own, and a `mov ax` constant that
turned out not to exist at rest. What worked, every time, was measurement — the
sector trace, the ATA trace, the DOS-call trace, the instruction trace, and
reading live values out of the running engine. The `0x5A` key was not deduced; it
was read from `cs:[0x39C3]` while the check executed.

Two traps worth recording:

* **A test that cannot change the observable is not a test.** A second drive was
  added while the slack check was still failing, so both runs reported 152 and
  the result was read as a refutation. It was a null result.
* **Verify the emulator is closed before editing its image.** Several patch
  experiments were read as "the patch had no effect" when the running emulator
  had simply loaded the file first, or held it locked so the readback was empty.
