# Evidence

Data and helpers the research notes depend on, kept in the repository so they cannot go
missing again. All three lived outside it until now, and the folder holding two of them
was deleted; every committed script that imported them broke silently.

| file | what it is |
|---|---|
| `wad.py` | reads a GWAD archive (`FOTOPLAY.WAD`) out of a Photo Play disk image |
| `catalog-photoplay.py` | the FAT16 reader `wad.py` is built on — walks the image and pulls a file out by path |
| `findit_keys.txt` | the per-picture LCG key for all **1397** FIND IT pictures, one `NAME KEY` pair per line |

`findit_keys.txt` is what makes the 2001-generation picture cipher testable offline: the
2000 images hold the same 1397 pictures with plaintext bodies, so undoing the 128-byte
LCG header layer with these keys turns every picture into a known plaintext/ciphertext
pair for the block cipher (`docs/research/23`).

## Use

```python
import os, sys
EV = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                  'docs', 'research', 'evidence')
sys.path.insert(0, EV)
import wad

data = wad.read(r'F:\HDDImages\2001\2001DE-B4821_\HardDisk.img',
                '/FINDIT/PICS/FOTOPLAY.WAD')
for name, offset, size in wad.entries(data):
    ...
```

`wad.py` loads `catalog-photoplay.py` from beside itself, falling back to the old fixed
path only if it is missing. The scripts in `scratchpad/` resolve this directory relative
to their own location, so they work from any checkout.

Verified after vendoring: 1397 entries read out of the 2001 DE image, and all 1397 keys
name an entry in it.

## Where the disk images are

Not here — they are gigabytes each. They live in `F:\HDDImages\<release>\`, one folder per
territory and build, e.g. `F:\HDDImages\2001\2001DE-B4821_\HardDisk.img`. Always run an
emulator against a **copy**; it writes to the image.
