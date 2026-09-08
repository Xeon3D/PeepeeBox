# The served tree

This is the FTP root. The cabinet's `CLIENT.EXE` walks it with absolute paths,
so `/master/...` below means `site/master/...` here.

Everything is empty on purpose. A file that is not here is answered `550`,
which the client treats as "nothing for me today" and moves on; a file that is
here is served. Nothing is invented, because the format of these files is not
known — see `docs/research/35-funnet.md`.

| path | what fun.net put here | saved on the cabinet as |
|---|---|---|
| `master/outgoing/country/` | the script for every cabinet in this territory | `\FN_SYS\DFU\TMP\SCRIPT.DL` |
| `master/outgoing/machine/` | the script for this cabinet alone, almost certainly named on its `machlic` | `\FN_SYS\DFU\TMP\SCRIPT.DL` |
| `master/outgoing/password/` | the operator-password check | `\FN_SYS\DFU\TMP\PWD` |
| `master/outgoing/newspage/` | fun.news pages, and `newspagefile` for their pictures | `\FN_SYS\NPFILE\%ld.npf` |
| `master/outgoing/masters/` | tournament results, per id | the `MST*` and `HIS*` tables |
| `master/outgoing/reward/` | rewards, per id | the `REW*` tables |
| `master/data/update/` | updates, run on the next boot | `\FN_SYS\UPDATE\%ld.exe` |
| `master/data/funmail/` | fun.mail card artwork | `\FN_MAIL\FMCARDS\%07lds.pcx` |
| `master/incoming/` | **uploads land here** — the cabinet's `SCRIPT.UL`, via `tmpfile.1` and a rename | |
| `master/trans/` | **uploads land here** — book-keeping exports, named `<prefix><name>_YYYYMMDDhhmmss` | |

The two `incoming` rows are the ones that will have something in them after the
first real session, and a dated copy of each also goes to `../captures/`.

Downloads under `newspage`, `update` and `funmail` are encrypted — the client
logs `download finished - decrypting:` and then `success` or `failed` — so
dropping arbitrary files in those directories will not produce anything useful
until that cipher is understood.
