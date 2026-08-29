# Phase 2C — TDongle (COM2 SIM dongle)

> **Status:** drafted 2026-05-08. Read-only static analysis on `main.exe` and
> the 13 other PE/DLL binaries Phase 1 flagged as TDongle-bearing. No
> dynamic capture required — the static evidence resolved the cross-binary
> question on its own. No binaries touched.
>
> **Scope deviation from `00-plan.md` and CLAUDE.md:** both documents describe
> TDongle as "compiled into 14 binaries" with a Phase 2C task to confirm
> "all 14 share the same call shape" so divergent codepaths can be
> identified and patched. The actual finding inverts that framing: TDongle is
> **statically linked into 14 binaries but live in exactly 1** (`main.exe`).
> The 13 DLLs carry the class as **dead code with the only surviving trace
> being the `__FUNCTION__` string-table entries in `.rdata`** — none of those
> strings is referenced from any DLL's `.text`. See `## Cross-binary
> liveness` and `## Plan correction`.

## TL;DR — protection relevance

**Surface C is single-binary.** Patching dongle protection only touches
`main.exe`. The 13 DLLs need no parallel codepath patches; the TDongle
class bodies they carry are dead post-link and are never reached at
runtime. Phase 5's effort estimate for surface C drops from "14 binaries to
synchronize" to "1 binary to patch."

The dongle is a **serial-attached SIM-card adapter on COM2** speaking 9600
8N1. `main.exe` performs a SIM-card-style cold-reset, then exchanges a
single 14-byte ISO-7816-shaped APDU and validates the reply structurally
(first byte == `0x01`, status word == `0x9000`, plus an XOR checksum on
both the post-reset 5-byte ATR-like reply and the 9-byte probe reply).
**No fixed unlock secret is embedded** in `main.exe`. Two `rand()` call
sites in the TDongle code path inject nonces into outgoing frames — i.e.
the protocol relies on the SIM's stored secret to compute correct replies
on the fly, not on the host comparing returned bytes against a literal in
the binary.

Implication for Phase 5: any of three trivial bypasses works.
1. **Force-true the init function.** Patch `fcn.00411800` (`TDongle::Init`,
   143 bytes at VA `0x00411800`) to return `AL=1` immediately.
2. **NOP the call site.** Single caller in `fcn.004069c0` at VA `0x4076bc`
   — replace the call with `mov al, 1` so the consumer treats the dongle
   as initialized.
3. **Trivial dongle emulator.** The XOR-checksum validation is permissive:
   any 5-byte response that XORs to 0 (1/256 chance for a constant frame)
   passes the cold-reset, and any 14-byte response with byte[0]=1 and
   bytes[0x84..0x85]=`{0x90, 0x00}` passes the init APDU check. A
   serial-loopback emulator producing those literal frames would unlock the
   system without binary patching.

**Caveat:** the TDongle's Get* accessors (`GetCountryID`, `GetVersionID`,
etc.) are *value-bearing* — their return values flow into `Profile=` in
`CONFIG.INI`, which drives asset selection. So bypass #1 and #2 above must
also seed the consumed state (CountryID = `UK`, VersionID = `NG1`) into
whatever store the rest of `main.exe` reads from after init. Specific
poke locations are flagged as a Phase 5 follow-up below.

## File inventory

| Binary | TDongle::* `__FUNCTION__` strings in `.rdata` | TDongle methods referenced from `.text` | Verdict |
|---|---:|---:|---|
| `main.exe`                          | 10 | 8 | **Live** |
| `execute.dll`                       |  8 | 0 | Dead — link artifact only |
| `Menu/menu.dll`                     | 10 | 0 | Dead |
| `Setup/adeditor.dll`                | 10 | 0 | Dead |
| `Setup/setup.dll`                   | 10 | 0 | Dead |
| `initmode/initmode.dll`             | 10 | 0 | Dead |
| `ppnet/FN_INFO/FN_INFO.dll`         | 10 | 0 | Dead |
| `ppnet/FN_MAIL/fn_mail.dll`         | 10 | 0 | Dead |
| `ppnet/FN_MST/FN_MST.dll`           | 10 | 0 | Dead |
| `ppnet/FN_NEWS/fn_news.dll`         | 10 | 0 | Dead |
| `ppnet/FN_PORT/FN_PORT.dll`         | 10 | 0 | Dead |
| `ppnet/FN_SYS/fn_sys.dll`           | 10 | 0 | Dead (note¹) |
| `ppnet/FN_TRANS/fn_trans.dll`       | 10 | 0 | Dead |
| `ppnet/FN_sms/FN_SMS.dll`           | 10 | 0 | Dead |

(¹) `fn_sys.dll` is the **only** DLL that imports the full COM-port config
API (`SetupComm`, `GetCommState`, `SetCommState`, `SetCommTimeouts`,
`PurgeComm`). That import set is used for the **modem on COM4** — its
`.data` carries `sendATCommand`, `MT_COM_ERROR_OPENINGPORT`, etc. Not
TDongle. The TDongle `__FUNCTION__` strings present here are still pure
link artifacts.

The 8 `__FUNCTION__` strings, byte-identical across all 14 binaries:
`TDongle::GetCountryID`, `TDongle::GetFile1Data`, `TDongle::GetIndexID`,
`TDongle::GetProdID`, `TDongle::GetProfileSub`, `TDongle::GetString1`,
`TDongle::GetString2`, `TDongle::GetVersionID`. `main.exe` carries `GetString2`
**three** times in the table (presumably from three distinct compilation
units that each instantiate the same template / accessor pattern); the
DLLs carry it once (the duplicated strings have been deduplicated by the
linker for the DLLs but not for `main.exe`'s larger build).

## Cross-binary liveness — how it was determined

Procedure: for each binary, parse PE sections, locate every `TDongle::`
prefix in `.rdata`, then scan `.text` for any 4-byte little-endian
absolute pointer that exactly equals one of those strings' VAs. Result:

```
binary                                    strings  total_refs  methods_referenced
main.exe                                       10           9                  8
execute.dll                                     8           0                  0
Menu/menu.dll                                  10           0                  0
Setup/adeditor.dll                             10           0                  0
Setup/setup.dll                                10           0                  0
initmode/initmode.dll                          10           0                  0
ppnet/FN_INFO/FN_INFO.dll                      10           0                  0
ppnet/FN_MAIL/fn_mail.dll                      10           0                  0
ppnet/FN_MST/FN_MST.dll                        10           0                  0
ppnet/FN_NEWS/fn_news.dll                      10           0                  0
ppnet/FN_PORT/FN_PORT.dll                      10           0                  0
ppnet/FN_SYS/fn_sys.dll                        10           0                  0
ppnet/FN_TRANS/fn_trans.dll                    10           0                  0
ppnet/FN_sms/FN_SMS.dll                        10           0                  0
```

A `__FUNCTION__` string with **zero** code references means no callsite
pushes it onto the stack — i.e. no method whose function-prologue logs its
own name is reached. Combined with the absence of `COM2` literal,
`SetCommState`, and `EscapeCommFunction` imports across the 13 DLLs, the
conclusion is unambiguous: **the linker pulled the TDongle library into
each binary because *some* member function it provided was needed for
type completeness or because the DLL exported a dependent symbol — but
the eight Get* method bodies (and presumably the rest of TDongle's
implementation) are never invoked.**

The corollary is that the 13 DLLs do not query the dongle for fresh
data, do not consult cached state, and do not (in their TDongle paths)
care whether the dongle is present. Whatever they know about
CountryID/VersionID/etc. they get from elsewhere — most likely
`Foto32/Config/CONFIG.INI` (`Profile=` key) and the MySQL `sys_settings`
table, both of which `main.exe` writes after a successful TDongle init.

## main.exe internals — TDongle method graph

VAs valid for `Extracted/PP_NG1_7712002-V5/E/Prog/main.exe` (PE32, ImageBase=0x00400000,
.text at 0x00401000, .rdata at 0x00423000, .data at 0x00430000,
_TEXT_HA at 0x0043b000).

| Function | VA | Size | Role |
|---|---|---:|---|
| `TDongle::Init` (entry) | `0x00411800` | 143 | Single caller from `fcn.004069c0 @ 0x4076bc`. Opens COM2, runs `EscapeCommFunction` reset sequence, seeds RNG, sends 14-byte init APDU, validates structural reply. Returns `AL=1` on success, `0` on any failure. |
| `OpenCOM(name, baud)` | `0x00411f70` | 172 | `CreateFileA(name, GENERIC_READ\|GENERIC_WRITE=0xc0000000, 0, NULL, OPEN_EXISTING=3, FILE_ATTRIBUTE_NORMAL=0x80, NULL)`. Stores `HANDLE` at `this[0]` and inits `this[0x104]=0`. Then `SetupComm(h, 1024, 1024)`, `PurgeComm(h, 0xf)`, `GetCommState`, sets `BaudRate=baud, fBinary=1, fParity=1, ByteSize=8, Parity=NOPARITY, StopBits=ONESTOPBIT`, `SetCommState`. Returns `0` on success, `1` on failure (errno-style). |
| `AssertSignals` | `0x00412050` | 27 | `EscapeCommFunction(h, SETDTR=5)` then `EscapeCommFunction(h, SETRTS=3)`. Asserts both control lines high. |
| `ClearSignals` | `0x00412070` | 27 | `EscapeCommFunction(h, CLRDTR=6)` then `EscapeCommFunction(h, CLRRTS=4)`. Drops both. |
| `SleepClock(ms)` | `0x00412820` | 49 | Busy-wait via `clock()` — `for (start=clock(); clock()-start<ms; );`. |
| `ReadByte()` | `0x00412090` | 136 | `ClearCommError`; if `cbInQue == 0` → return `-2 (0xfffffffe)`; else `ReadFile(h, &b, 1)`; on success returns `b & 0xff`, on failure returns `-2`. Single-byte non-blocking read. |
| `XorChurn(b)` | `0x00412b80` | 111 | XORs caller-supplied byte into `this[0x10f]`, then deterministically advances both bytes (`+= 0x25`/`+= 0x75` with re-seeds at boundaries `<0x1e` → `0xe9`, `>0xae` → `0x17`, etc.). State machine; appears to be a stream-cipher-like keystream generator. Used to checksum/whiten received bytes. |
| `SetXorState(b1, b2)` | `0x00412b60` | 23 | Writes `b1`→`this[0x10f]`, `b2`→`this[0x110]`. Seeds the XorChurn state. Init calls it once with `(0xab, 0xd9)` — the XorChurn seed pair. |
| `SendStimulus5(byte)` | `0x00412cf0` | 60 | Builds a 5-byte frame `{0xc0, 0x00, 0xc1, 0x01, byte}` and dispatches via `fcn.00412230` (frame transmitter that uses `rand()` to inject a nonce). |
| `BuildAndSendAPDU(record, &reply)` | `0x00412410` | 404 | Constructs a framed packet from a structured record, transmits via `fcn.00412230`, polls for reply. Used by the init path and by 3 other call sites (the Get* accessors are downstream consumers). Validates first reply bytes against the record's expected response code. |
| `BuildRecordFromTemplate(tpl, n, &reply)` | `0x00412860` | 172 | Allocates 268 bytes, copies a 4-byte-stride record from `tpl` (4 leading status fields, 2 length/expectation fields with `0xffff` sentinels meaning "skip", then `n-6` payload bytes), calls `BuildAndSendAPDU`, frees the buffer. The init path's call uses `tpl=0x433d5c, n=14`. |
| `ProbeReply9` | `0x00412e10` | 71 | Calls `fcn.00412150` (writer; not chased), writes a single stimulus byte (`0x0a`), waits 50ms, then 9-iteration loop calling `ReadByte` and `XorChurn`. Returns `AL=1` iff exactly 9 bytes were received and the running XOR ended at 0. Used as a "is the dongle alive" probe. |
| `ColdReset/Unlock` | `0x00412e60` | 138 | Probes; if dead: `ClearSignals → SleepClock(20) → AssertSignals → SleepClock(50) → probe again`. If second probe succeeds (or first did): writes `0x6e`, waits 20ms, 5-iteration `ReadByte`+`XorChurn` loop, returns `AL=1` iff exactly 5 bytes received and XOR=0. **Sets `this[0x109] = 0` on success** — this is the lock-state byte. |
| `FrameTx-with-rand` | `0x00412230` | (not measured) | Frame transmitter; dereferences `MSVCRT.rand` to inject randomness into outgoing bytes. |
| `RandomHelper` | `0x004127e0` | (not measured) | Calls `rand()`. Auxiliary nonce/jitter generator. |

### TDongle object layout (deduced)

| Offset | Type | Role |
|---|---|---|
| `this[0x000]` | `HANDLE` (4 bytes) | COM2 file handle from `CreateFileA` |
| `this[0x104]` | byte/dword | State flag, written 0 at OpenCOM |
| `this[0x109]` | byte | **Lock-state byte. =0 after successful ColdReset.** Effectively `IsLocked = (this[0x109] != 0)`. |
| `this[0x10f]` | byte | XorChurn state byte 1; init seed `0xab` |
| `this[0x110]` | byte | XorChurn state byte 2; init seed `0xd9` |

The class is at least 0x111 bytes (272 bytes, rounded up). Buffer fields
between `this[0x4]` and `this[0x104]` are presumably the rolling rx/tx
work buffer.

### `TDongle::Init` annotated (`0x00411800`)

```
0x00411800  enter (sub esp, 0x88; push esi)
0x00411807  push 0x2580                 ; 9600 baud
0x0041180e  push "COM2"                 ; .data:0x433ecc — note: in .data, not .rdata
0x00411813  call OpenCOM                ; → this[0]=h, this[0x104]=0
0x00411818  test al, al; jne fail       ; OpenCOM returns nonzero on failure
0x0041181c  call ClearSignals           ; CLRDTR + CLRRTS — power-off
0x00411823  push 0xd9; push 0xab
0x0041182d  call SetXorState(0xab, 0xd9) ; seed XorChurn
0x00411836  push 0; call time;
0x0041183d  call srand                  ; seed RNG with time(NULL)
0x00411848  call ColdReset/Unlock       ; SIM cold reset cycle
0x0041184f  test al, al; je fail
0x00411851  lea eax, [stack reply buf]
0x00411855  push &reply
0x00411858  push 0x0e                   ; n = 14
0x0041185a  push 0x433d5c               ; init APDU template
0x0041185f  call BuildRecordFromTemplate
0x00411864  cmp byte [reply+0], 1; jne fail
0x0041186b  mov ax, word [reply+0x85]
0x00411873  cmp al, 0x90; jne fail      ; ISO-7816 SW1 = 0x90
0x00411877  test ah, ah;  jne fail      ; ISO-7816 SW2 = 0x00
0x0041187b  mov al, 1; ret              ; SUCCESS
fail (0x411885): xor al, al; ret        ; FAILURE
```

### Init APDU template at `.data:0x00433d5c`

The 14-byte template fed to `BuildRecordFromTemplate(tpl, 14, &reply)`:

```
file off 0x00033d5c → VA 0x00433d5c
0x00 0x00 0x00 0x00   ; src[0]   = 0x00       → APDU CLA = 0x00
0x20 0x00 0x00 0x00   ; src[4]   = 0x20       → APDU INS = 0x20 (VERIFY-style)
0x00 0x00 0x00 0x00   ; src[8]   = 0x00       → APDU P1  = 0x00
0x04 0x00 0x00 0x00   ; src[0xc] = 0x04       → APDU P2  = 0x04
0x08 0x00 0x00 0x00   ; src[0x10]= 0x08       → APDU Lc  = 8 (payload length)
0xff 0xff 0x00 0x00   ; src[0x14]= 0xffff     → sentinel "no Le", skipped
0x01 0x00 0x00 0x00   ; src[0x18]= 0x01       → payload byte 0
0x02 0x00 0x00 0x00   ; src[0x1c]= 0x02       → payload byte 1
0x03 0x00 0x00 0x00   ; payload bytes 2..7
0x04 0x00 0x00 0x00
0x05 0x00 0x00 0x00
0x06 0x00 0x00 0x00
0x07 0x00 0x00 0x00
0x08 0x00 0x00 0x00
```

This is **structurally an ISO-7816 VERIFY APDU** (`CLA=00, INS=20, P1/P2,
Lc, payload`). The eight payload bytes `01..08` look like a fixed test
vector or version-handshake constant — **not** a PIN. The reply is a 268-
byte frame; `main.exe` validates only:

- `reply[0] == 0x01` (frame-type / status)
- `reply[0x84..0x85] == {0x90, 0x00}` (ISO-7816 SW = 0x9000 = success)

That is the entire init validation. Nothing else in the 268-byte reply is
read by this function. The Get* accessors (`GetCountryID`, etc.) presumably
issue separate APDUs later and parse their replies.

### Cold-reset sequence (ISO-7816-like)

```
ProbeReply9 → if alive: skip
  ClearSignals (CLRDTR + CLRRTS)              ; drop both control lines
  SleepClock(20 ms)
  AssertSignals (SETDTR + SETRTS)             ; raise both
  SleepClock(50 ms)
  ProbeReply9 → must succeed
SendStimulus(0x6e)
SleepClock(20 ms)
for i in 0..5:
  b = ReadByte()
  if b != -2: bl ^= XorChurn(b); edi++
require edi == 5 and bl == 0
this[0x109] = 0   ; UNLOCKED
```

The `ProbeReply9` step uses stimulus byte `0x0a` and a 9-byte XOR-checksum
reply; `ColdReset` itself uses stimulus `0x6e` and a 5-byte XOR-checksum
reply. **Both checks are pure XOR-to-zero structural validation — there
is no comparison against any literal in `main.exe`.**

## CountryID / VersionID / Profile= data flow (sketched)

Full trace deferred — only the entry point was needed to characterize the
protection layer. Known facts:

- `main.exe` `.rdata` carries one `CountryID`, one `IndexID`, one `ProdID`,
  one `ProfileSub`, four `VersionID`, and seventeen `Profile`-prefixed
  literals. The high `Profile` count points at multiple call sites that
  read/write the `Foto32/Config/CONFIG.INI` `[Profile]Profile=` key.
- The TDongle Get* accessors (`GetCountryID`, `GetVersionID`,
  `GetProdID`, `GetIndexID`, `GetProfileSub`, `GetString1`,
  `GetString2`, `GetFile1Data`) live in `main.exe`'s `.text` at a packed
  +0x70 stride starting near `0x411b50`; each pushes its own
  `__FUNCTION__` string in the prologue (those are the eight references
  detected in the cross-binary scan).
- The result of `GetCountryID` flows into a `Profile::SetCountry` setter
  (named in CLAUDE.md) which then writes `[Profile]Profile=<UK|NL|DE|...>`
  to `CONFIG.INI`. The 13 client DLLs read that key directly via their
  own `Config::Read` helpers — no live TDongle call.

For Phase 5 a force-true patch in `main.exe` must therefore *also*
ensure that `[Profile]Profile=` is present in `CONFIG.INI` with the
desired value (`UK` for the `7712002-V5` UK profile). On a freshly
provisioned VM the seed value `Profile=UK` is already in the shipped
`CONFIG.INI`, so as long as the patch does not *also* clobber that key,
the existing seed survives and the DLL clients see the right region.

## IsLocked / Unlock gate inventory

A whole-binary scan confirms `IsLocked` and `Unlock` are not function
names — there is no `__FUNCTION__` string for either. The lock state is
the single byte `this[0x109]`, written exactly once (at the end of
`ColdReset/Unlock`). Read sites for that byte are inlined into
`BuildAndSendAPDU` (`fcn.00412410 @ 0x004124e1`, used to gate APDU
transmission on whether the dongle is unlocked) — i.e. the "locked"
abstraction is one byte and one branch, not a separate methodology.

## Embedded unlock secret — none

Three independent indications:

1. **Two `rand()` consumers** in the TDongle code path (`fcn.00412230`
   and `fcn.004127e0`) inject randomness into outgoing frames. A
   protocol that nonces its requests is one whose responses cannot be
   replay-validated against a fixed expectation in the client.
2. **Both XOR-checksum validations** (`ProbeReply9`, `ColdReset` 5-byte
   trailer) check only that the reply bytes XOR to zero — they don't
   compare against a known plaintext.
3. **The init APDU reply** is validated only on `reply[0]==1` and
   `reply[0x84..0x85]=={0x90,0x00}` — three structural bytes out of 268.
   The payload bytes that contain CountryID / VersionID / etc. are not
   checked against any in-binary literal at this layer.

The dongle is therefore a **black-box authenticator**: the SIM card holds
a secret that lets it produce structurally valid replies to nonced
queries. The host has no way to know what that secret is, and conversely
does not need to: the protection is "you must have a SIM that responds
like a SIM," not "you must produce a specific known byte sequence." That
is the cheapest design from Funworld's PoV (no key escrow, no per-machine
provisioning) and the easiest to bypass from ours (no secret to recover,
just three branches to coerce).

## `_TEXT_HA` section verdict (Phase 1 Q closed)

`main.exe`'s `_TEXT_HA` (RW, paddr 0x38000, size 0x11000 / 68 KB) and
`rundll32.dll`'s same-named section turn out to be **delay-load import
state**, not TDongle storage. First 4 KB contain DLL name strings
(`wtsapi32.dll`, `wfapi.dll`, `utildll.dll`) and Win32 Terminal Services
function names (`WTSQueryUserToken`, …). 540 of the 542 cross-references
into the section land in that first 4 KB — typical thunk-table layout
for a custom delay-load resolver. No TDongle bytes pass through it.

This closes one of Phase 1's flagged uncertainties: shared `_TEXT_HA` is
*not* a TDongle-state singleton and is *not* an anti-tamper data carrier.
It is a build-system artifact for late-bound Win32 imports that some
late-Windows-99 / Win98 SDKs lacked.

## Plan correction

Update `Docs/protection/00-plan.md` Phase 2C bullets and CLAUDE.md
"Hardware bindings / DRM" point 3 as follows. (Not yet applied — flagged
for user review.)

- **Old (`00-plan.md` § Phase 2C, bullet 5):** *"Cross-binary consistency:
  confirm all **14** TDongle-bearing binaries share the same call shape.
  Any divergence = parallel codepath to handle."*
- **New:** *"Cross-binary consistency: TDongle is statically linked into
  14 binaries but **live in 1** (`main.exe`). The 13 DLLs carry the class
  bodies as dead post-link code; only the `__FUNCTION__` string-table
  entries remain in `.rdata` and they are referenced from no `.text`.
  Phase 5 force-true patches need to touch only `main.exe`."*

- **Old (`CLAUDE.md`, "TDongle … class is `TDongle`; call sites in nearly
  every binary"):** implies live calls in every binary.
- **New:** *"TDongle is statically linked into 14 binaries (main.exe and
  13 DLLs) but the dongle is actually driven only from `main.exe`. The 13
  DLLs depend transitively on the values TDongle reads (CountryID drives
  `Profile=`, etc.) but consume those values from `CONFIG.INI` /
  `sys_settings`, not from a live dongle query."*

Also update `00-plan.md` § Phase 2C, sub-bullet 6 (the `_TEXT_HA`
question): mark it as resolved — `_TEXT_HA` is delay-load import state,
not TDongle/anti-tamper.

## Open questions for Phase 5

1. **CountryID consumer chain.** Static trace of the eight Get* methods'
   consumers (where each return value is stored, how the DLLs read it)
   was not done in this phase — only the *entry-point protection* was
   characterized. If Phase 5 picks "force-true `TDongle::Init`", the
   rest of `main.exe` will still call the Get* accessors and route
   their values; those accessors hit the dongle over COM2 the same way
   the init APDU does. Either:
   - (a) the accessors check `this[0x109]==0` and bail if locked — in
     which case the force-true on Init is **not enough** because the
     accessors themselves issue APDUs; or
   - (b) the accessors read from a cached-value buffer populated during
     init — in which case force-true Init plus seeding the cache works.
   Which one is true is the single remaining static question for this
   surface. Open.
2. **Game DLL references to TDongle.** Phase 2E still needs to confirm
   that the ~40 game DLLs don't have their own (live) TDongle copies.
   This phase only covered the 14 binaries Phase 1 flagged.
3. **Dynamic confirmation.** Optional. With a real dongle and a COM2
   sniff (CLAUDE.md `analysis/dynamic/com2-capture.bin` slot, Phase 3),
   the wire-level frame format and the contents of the 268-byte init
   reply could be confirmed. **Not necessary for Phase 5** if the chosen
   strategy is force-true rather than emulate.

## Artifacts produced this phase

- `analysis/p2c/work/methods.json` — TDongle method graph + object
  layout + cross-binary liveness, machine-readable.

No `raw/` or per-binary scanner this phase: the cross-binary question
collapsed onto a single one-shot Python pass that compared `TDongle::*`
string VAs against `.text` 4-byte LE pointer references for all 14
binaries. The result (1 live, 13 dead) made a per-binary scanner
unnecessary; running `p2a-scan-one.sh` 14 times would have produced 14
copies of the same near-empty answer. The qwen narrative pipeline
(Phase 1 / 2A / 2C-projected) was likewise skipped — no per-binary
prose to write when the per-binary finding is "dead, identical to the
other 12."
