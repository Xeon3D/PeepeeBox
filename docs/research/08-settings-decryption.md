# Phase 8 — `\FOTO\SETTINGS\*.SET` decrypted

The dongle banner is not a matter of taste. Every game does:

```c
strcmp( read_setting(main_set, "Version"), dongle_banner )   /* exact match */
```

so the banner must equal the `Version` value stored inside the **encrypted**
`\FOTO\SETTINGS\MAIN.SET`. Guessing it from the folder name is unreliable — several
images ship a different country's profile than their name suggests.

## The cipher

Recovered from the 1999 build (`analysis/p5/findit99.bin`):

| | |
|---|---|
| loader | `1978:019C` (file `0x1C51C`) |
| decrypting read | `1870:0857` → `0x1BCEC` |
| PRNG | `0x1BA13` |
| key | the constant `dword 0x00016295`, passed to every read |

```
PRNG:    seed = seed * 0x08088405 + 1          (32-bit, the Borland/Delphi LCG)
         rand(n) = (seed * n) >> 32
cipher:  dst[i] ^= rand(0x100)
```

The decryptor saves the global seed, sets it to the key on entry and restores it on exit,
so **every read restarts the keystream from `0x16295`** — each field is decrypted with the
same stream, not one continuous one.

## File layout

```
word count          )
word size1          )  three headers, each decrypted with a fresh keystream
word size2          )
size1 bytes  entries: NUL-terminated key, then a 16-bit offset into the pool
size2 bytes  the string pool
```

`read_setting` (`0x1C638`) walks the entries comparing keys, and returns
`pool_base + offset`; entries advance by `strlen(key) + 3`.

Implemented in `scripts/mainset.py`.

## What it yields

```
Profil        'AT_ATS'                    Currency      'Schilling'
Country       'AT'                        CurrencyKurz  'ATS'
Version       'Version 99 (AT)'           CoinsBills    '100,500,1000,2000,...'
Description   'Austria (Schilling)'       Bonus1        '500-50,1000-100,...'
Languages     'DEFI'                      Tokens        '0,0,0,0,100,500,...'
```

45 keys in the 1999 profile. Besides the banner this is the machine's whole
currency/territory configuration, which is useful well beyond the dongle work.

## Banners corrected as a result

Four images shipped a profile that disagreed with their folder name:

| image | folder implies | `MAIN.SET` actually says | profile |
|---|---|---|---|
| `PP1999DE-NSB 81519` | `(DE)` | **`Version 99 (AT)`** | `AT_ATS` |
| `PP2001DE-NSB A3735` | `(DE)` | **`Version 2001 (AT)`** | `AT_ATS` |
| `PP2001SP-EB711` | `(SP)` | **`Version 2001 (ES)`** | `ES_P_EU` |
| `PPIGO8ES-VM006` | `(ES)` | **`Version 2008 (PT)`** | `PT_EURO` |

This is what produced the reported `Wrong Version: >Version 99 (DE)<`. All 68 patched
images now carry the banner their own settings file demands
(`analysis/p5/readversions.py` verifies it).

**Rule going forward: read the banner from `MAIN.SET`, never derive it from a filename.**
