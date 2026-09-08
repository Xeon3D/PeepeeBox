# The fun.net `.TAB` tables

**One format, forty-odd tables, and the record size is in the code rather than
the file.** Everything under `\FN_SYS\DATABASE\` is a flat array of fixed-size
records with no header, no index and no free list:

```
record = 1 byte in-use flag  +  N bytes of payload
```

`N` is a constant compiled into whichever binary opens the table. `tools/fntab`
reads them.

Worked from `PP2001NL-MASTERS-NSB H9751 SR1`, a machine that was on fun.net for
years and whose tables are full.

## Where the record size lives

Every table is opened through one shared routine, and the call passes the payload
size:

```
013608  66 68 6c 01 00 00    push  dword 0x16c        ; payload size
01360e  68 00 20             push  word  0x2000       ; cache size
013611  68 fa 23             push  word  0x23fa       ; "\fn_sys\database\user\player.tab"
013614  8d 86 0a fc          lea   ax, [bp - 0x3f6]
013618  50                   push  ax                 ; the handle
013619  9a 0d 00 df 0d       lcall 0xddf, 0xd         ; open
01361e  83 c4 0a             add   sp, 0xa
```

`0x16C` is 364, and PLAYER.TAB's records are 365 bytes. The routine adds the
flag byte itself. The high word of the dword is a flag, set on a few tables
(`fmindex`, `monitor`, `CVdata`); the second argument is a 4/8/32 KB cache size,
which is also the granularity the file grows in -- so a file is *not* a whole
number of records, and the bytes past the last whole record are slack.

That reading is confirmed three ways:

* **Cross-binary agreement.** Seven binaries open the shared tables and all pass
  the same size: `player` 364, `login` 150, `settings` 127, `monitor` 25,
  `npitem` 534, `chgply` 365.
* **Against tables read by hand.** `settings.tab` (127 → 128) and `techdata.tab`
  (120 → 121) were laid out by eye before the open calls were found, and agree.
* **Against the files.** At the derived stride, the flag byte at every `k*R` is
  0 or 1 and nothing else, on every table tried. At a stride one byte out it is
  full of text.

## The sizes

| table | payload | record | | table | payload | record |
|---|---:|---:|---|---|---:|---:|
| `CVDATA` | 41 | 42 | | `MONITOR` | 25 | 26 |
| `CVSLAVE` | 111 | 112 | | `MSTCRED` | 8 | 9 |
| `CHGPLY` | 365 | 366 | | `MSTMAIN` | 21 | 22 |
| `FMCARDS` | 36 | 37 | | `MSTNAME` | 109 | 110 |
| `FMFREE` | 20 | 21 | | `MSTSCORE` | 33 | 34 |
| `FMHOME` | 19 | 20 | | `NPDOC` | 8 | 9 |
| `FMINDEX` | 19 | 20 | | `NPITEM` | 534 | 535 |
| `FMMSGIN` | 2270 | 2271 | | `NPMAIN` | 55 | 56 |
| `FMMSGOUT` | 2158 | 2159 | | `PLAYER` | 364 | 365 |
| `FMSYSBOX` | 109 | 110 | | `POTCOUNT` | 12 | 13 |
| `FNUPDATE` | 80 | 81 | | `POTDEF` | 48 | 49 |
| `FREEGAME` | 33 | 34 | | `SETTINGS` | 127 | 128 |
| `HIS_NODE` | 260 | 261 | | `SMSAVAIL` | 519 | 520 |
| `HIS_OPER` | 248 | 249 | | `SMSDATA` | 517 | 518 |
| `HIS_T100` | 132 | 133 | | `SMSGROUP` | 75 | 76 |
| `HISNAME` | 115 | 116 | | `SMSMEDIA` | 77 | 78 |
| `HISPARNT` | 12 | 13 | | `SMSMNEW` | 8 | 9 |
| `HISPRIZE` | 117 | 118 | | `SMSORDER` | 62 | 63 |
| `HISUPDW` | 16 | 17 | | `SMSPROV` | 106 | 107 |
| `JOINPUB` | 19 | 20 | | `SMSREG` | 63 | 64 |
| `LOGIN` | 150 | 151 | | `SMSSERV` | 586 | 587 |
| | | | | `TECHDATA` | 120 | 121 |

`.RVW` files are "review" snapshots and share their `.TAB`'s layout.

## Field conventions

* Strings are **fixed-width, NUL-terminated, cp437**, zero-padded to the end of
  their field. The declared field is usually much longer than anything stored in
  it -- `PLAYER`'s name fields are 61 bytes and nothing in this machine's 388
  records exceeds 30.
* Integers are little-endian.
* Dates are `uint16 year, uint8 day, uint8 month` -- four bytes, and *not* a
  32-bit integer. Reading `PLAYER+297` as a dword gives nonsense; reading it as
  the triple gives years 1900..2001, months 1..12, days 1..31, with 1900 as the
  unset value.

## PLAYER.TAB, solved

```
 +0    1   uint8    in-use flag (always 1)
 +1   14   char[]   member id -- "@@" + 10 characters, always exactly 12
 +15   4   char[]   language, 3 letters (DUT, ENG, ...)
 +19  61   char[]   first name
 +80  61   char[]   last name
+141  61   char[]   street and number
+202  31   char[]   postcode
+233  61   char[]   city
+294   3   char[]   country, 2 letters
+297   4   date     date of birth
+301  22   char[]   telephone
+323  42   char[]   email  (optional -- 247 of 388 records have one)
                                                          total 365
```

The field *boundaries* come from the byte classes: for each offset, is it always
zero, always text, or binary, across every live record. The runs are unambiguous
and the sizes sum to exactly 365.

The field *meanings* come from the program rather than from reading anybody's
records. `FN_MST.EXE` pushes the registration form's labels in order at
`0x014DA9`:

```
INP_firstname  INP_lastname  INP_street  INP_zip  INP_city
INP_birthdate  INP_gender    optional    INP_telefon  INP_email
```

which is the field order above, with `optional` sitting exactly where the only
partly-filled field is. The shapes confirm it without disclosing anything --
masking every letter and digit to `x`:

```
first_name   xxxxxx
street       xxxxxxxx xxx          street then number
postcode     xxxx xx               the Dutch "1234 AB" form
country      xx
birth        xxxx-xx-xx
email        xxxxxxxx@xxxxxxx.xxx
```

`INP_gender` has no home in the record and is still unaccounted for.

## PLAYER and LOGIN are joined by the member id

`LOGIN.TAB` (151-byte records) holds 1139 live entries on this machine, and its
first field is a login name of the same shape. Two populations:

| | count | shape |
|---|---:|---|
| 12 characters, `@@` prefix | 388 | registered NET.MEMBERs |
| 13 characters, letter first | 742 | everything else |

and **all 388 of PLAYER's member ids appear in LOGIN, with none missing**. So
`LOGIN` is the master list of identities and `PLAYER` carries the personal
details of the registered subset, keyed by member id. That matches the MASTERS
text, which distinguishes a `foreignplayer` -- "You are a guest player, since you
don't appear in any ranking of this Photo Play" -- from a NET.MEMBER.

The rest of `LOGIN`'s record is only partly worked out: a 3-letter language at
+15, a 4-byte binary at +19, a 47-byte string at +23 filled in 800 of 1139
records, a 4-character code at +70, and 20 bytes of binary at +95 that is
distinct in 1053 of 1139 records -- the shape of a hash, which is where the PIN
would live (`INP_pincode` is the other half of the login form). Not confirmed.

## The message tables

`FMMSGIN` (2271) and `FMMSGOUT` (2159) are the same idea: a fixed header, then
one large free-text body buffer, then a trailing `uint16`.

```
FMMSGIN     header  +0 .. ~+217        body from +218      u16 at +2267
FMMSGOUT    header  +0 .. ~+104        body from +105      u16 at +2154
```

The header carries several 13-14 character strings at +24, +69 and +173 -- login
names, by their shape and length -- plus 2 and 3 character codes. The body is
cp437 with embedded control bytes, which is why a per-column classifier
fragments it; it is one field.

The trailing `uint16` is **not** the body length: it matches on 9 of 101
`FMMSGOUT` records and 0 of 44 `FMMSGIN` ones. Its range (0..473 against 140
slots, 0..413 against 105) suggests an index or sequence number into another
table, probably `FMINDEX`. Not established.

`SMSDATA` (518) is simpler: 3 bytes of binary, then a single 464-byte text field
from +5, then padding.

## Not solved

* `INP_gender` -- collected by the form, not visibly in `PLAYER`.
* `LOGIN+19` (4 bytes), `LOGIN+95` (20 bytes), `LOGIN+146` (5 bytes).
* The message header fields beyond their shapes, and what the trailing `uint16`
  indexes.
* `HIS_NODE` / `HIS_OPER` / `HIS_T100` -- the ranking history, 261/249/133-byte
  records, sizes known and layouts not looked at.
* Whether any of this is the wire format too. The machine uploads these to
  fun.net through `CLIENT.EXE`, and `FN_SYS.EXE` has `/export`, `/event=`,
  `/setting=`, `/techdata=` and `/confupd=` switches that look like an export
  path. If the upload is these records verbatim, the wire format is already
  documented above.

## A note on the contents

These tables are a real pub's, and the personal ones -- `PLAYER`, `LOGIN`,
`FMMSGIN`/`FMMSGOUT` (private messages between named people) and `SMSDATA` --
hold real names, addresses, birthdates, telephone numbers, email addresses and
message text belonging to people who used a machine in 2001 and 2002.

Everything in this document was derived from **structure**: byte classes per
column, field lengths, set comparisons between tables, and the field order the
program itself uses. No record contents appear here, and none need to. `fntab`
has a `--mask` flag that prints the layout with every letter and digit replaced,
which is what produced the shape evidence above; that is the right way to work on
these files unless there is a specific reason to do otherwise.
