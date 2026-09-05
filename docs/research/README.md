# Research

The reverse-engineering notes the emulation is built on, in the order they were
written.

**They contradict each other, on purpose.** Each records what was believed at the
time, and later ones say plainly where earlier ones turned out to be wrong. Read
them as a trail, not as a specification — where two disagree, the later one wins,
and the corrections are usually the most interesting part.

## Photo Play / I.G.O. — the main sequence

| | |
|---|---|
| `00-plan.md` | Bootstrap: 20 disk images mapped, PTS-DOS/FAT16, root-free FAT reader |
| `01-hasp-protection.md` | The protection surface and a universal patcher. Calls the dongle a HASP — superseded by 13 |
| `02-86box-surface.md` | Hardware waits and watchdog resets: a second, non-dongle failure surface |
| `03-two-tokens-and-emulation.md` | **There are two tokens.** Emulating one alone changes nothing |
| `04-peepeebox-feasibility.md` | Feasibility of this fork. Its central premise (reverse Aladdin's library) is wrong — see 13 |
| `05-ds1982-protocol.md` | The DS1982 iButton link, fully recovered |
| `06-menu-mode-is-not-enough.md` | Falsifies 01/03's recommendation from a field report |
| `07-hasp-wire-protocol.md` | The wire protocol and command grammar, recovered and validated live |
| `08-settings-decryption.md` | `\FOTO\SETTINGS\*.SET` decrypted; the banner must equal `MAIN.SET["Version"]` |
| `09-ng-dongle.md` | The 2008 generation uses a third token, "NG" |
| `10-keyn.md` | `KEYN.COM`, the TSR that fakes the dongle in software |
| `11-ds1982-emulation.md` | The iButton half, implemented |
| `12-dongle-silicon.md` | **The dongle, from its own silicon.** Five units dumped, firmware disassembled and executed |
| `13-dongle-families.md` | **It is not a HASP.** Corrects the framing running through 01, 04, 07, 09, 10, 11 |
| `14-dongle-dwords-are-database-keys.md` | **The dwords are content keys.** Each photo game reads one to decrypt its picture database |
| `15-cdongle.md` | **The 2000 generation's second dongle, decoded and emulated.** Transport, licence constant and record read; that generation now boots and runs its games |
| `16-photo-games-checklist.md` | **Why a photo game shows no pictures.** The two mechanisms, the fixed-offset record rule, and the checklist -- read before diagnosing one again |
| `17-igo8-keyless-diff.md` | **A keyless IGO8 image, diffed against its original.** Three bytes per game, ten in the menu, and 160 databases turned from ciphertext to plaintext |
| `18-igo8-serial-dongle.md` | **The 2008 dongle is a serial smart-card reader, not a parallel-port device.** Transport, both keystreams, framing, the APDU set and the record -- corrects 09 |
| `19-hasp-passwords.md` | **The HASP passwords, per generation.** 2001 and IGO 2-7 are HASP4; three password pairs cover six generations, with the anchored method that finds them and the false trail that does not |
| `20-dongle-dumps.md` | **Nine dongles, dumped.** Passwords, record layouts and the per-customer crypto table -- the dongle side of the cipher that blocks 2001's pictures and IGO 3's boot |
| `21-igo2-igo6.md` | **I.G.O. 2 and I.G.O. 6 run, from fifteen untouched images.** Both generations identify themselves; and a one-line Microwire regression -- a lost `hd_ph = HD_READ` -- that had been measured and read as a wall in the library |
| `22-igo2-pictures.md` | **Which I.G.O. 2 archives fail, and why.** The record it is served is correct in every field; funworld shipped two picture schemes side by side and only one of them is a wall |
| `23-hasp4-transform.md` | **The picture cipher's keyed round, in software.** `batteryshark/dongle-lab`'s HASP4 core is line for line what `HANDOFF2001` § 20 reconstructed out of `FINDIT.EXE` -- four constants and both rotation schedules agree |
| `24-copycontrol.md` | **Photo Play 2.0 is Microcosm CopyControl, not a dongle.** The key is the disk's *physical layout*, which imaging destroys; 21 files in `\EXE\`, all three checks passing on unmodified originals, and `ppfix` repairing an affected image |
| `24-known-plaintext-corpus.md` | **72 MB of exact known plaintext against two keys.** I.G.O. 4 ships the same `FOTOPLAY.WAD` in the clear that 2 and 3 encrypt -- 1397 of 1397 entries agree on name, offset and size, so the three are byte-aligned |
| `25-igo2-block-function.md` | **E, read out of `FINDIT.EXE` rather than guessed at.** Five call sites, HASP service 61 over 4 KB, and the rule that a buffer of eight bytes or less is left alone -- after 24 refuted the assumed block function by measurement |
| `26-cipher-broken.md` | **The picture cipher, broken.** The two dwords the dongle contributes to a 4 KB buffer are recovered by search, and 300/300 entries then decrypt with no hardware at all |
| `27-io-card.md` | **The funworld I/O card: an 8255 at 0x210.** Where the coin acceptor and the two door buttons live, found by answering the card-detection sweep; the C120's six accept lines and its 100 ms pulse; A0 is the setup button |

Two documents are both numbered `24`: `24-copycontrol.md` and
`24-known-plaintext-corpus.md`, written for different releases on different
branches. Left as they are, because both numbers are cited elsewhere.

## Sibling investigation

Work by another instance on the NG1/NG2 machines — different dongle, same
company, overlapping game code. Kept because two of these solved a problem in the
1999 path that the main sequence had concluded was out of scope.

| | |
|---|---|
| `igo-02c-tdongle.md` | The I.G.O. T-dongle |
| `igo-09-decrypt.md` | Archive and asset decryption |
| `igo-10-softdongle.md` | A software dongle approach |
| `ng-10-dongle-code.md` | The NG dongle code path |
| `ng-11-pcx-cipher.md` | **Only the first 128 bytes of each PCX are encrypted**; body and palette are plaintext |
| `ng-12-dword-found.md` | **The per-picture key is a type-1 dongle query on the filename.** This is what made the photo games work |

## What these corrected in this repository

Three findings changed the code rather than just the documentation:

1. **The block layout was KEYN's, not the hardware's** (`14`) — the banner was
   padded to 30 bytes instead of 16, putting every dword 14 bytes late, so the
   photo games decrypted their databases to noise. Long misfiled here as a
   pre-existing defect unrelated to the dongle.
2. **Type 1 is the per-picture key** (`ng-12`), not the unused curiosity `12` and
   `14` took it for. The host sends `{01, NAME[8], nonce}`; the reply seeds the
   LCG that decrypts the picture's PCX header.
3. **It is not a HASP** (`13`). Aladdin is not involved in any generation.
4. **The 2000 generation has a second dongle** (`15`), sharing the parallel port
   with the 1999 one but nothing else. Emulating it is what made that
   generation's images boot and run their games. Read `15` for how often it
   corrects itself on the way — the record model in it is wrong, then right for a
   different reason, and the `D0` trailer goes from anomaly to the one thing the
   parser should have been built on.
5. **The dwords sit at a fixed offset, whatever the banner is** (`16`). The
   conditional layout `14` introduced worked for 1999's 15-character banner and
   silently broke the 2000 generation's 17-character one, costing FINDIT its
   level database. `16` is the checklist that stops this being re-derived a
   fourth time.
6. **The 2008 dongle is not on the parallel port** (`18`). It is a serial
   smart-card reader at `0x2F8` speaking ISO 7816 APDUs under two layers of
   obfuscation. `09` called this generation's token "NG" and looked for it in the
   wrong place entirely.
7. **In 2008 the six content keys are compile-time constants** (`17`), identical
   in all 61 executables and written into the banner buffer by the game itself.
   `14`'s "the dwords come from the dongle" holds for 1999 and 2000; in 2008 the
   dongle supplies only the banner.
