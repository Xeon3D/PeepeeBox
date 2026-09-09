PeepeeBox nvram
===============

`4dps.nvr` is the Zida Tomato 4DPS's CMOS and `4dps.bin` its flash, both taken
from a cabinet that boots. They ship because a blank CMOS is not a neutral
starting point on this board: the BIOS finds no drive configured and stops on a
hard disk error before PTS-DOS is ever reached. Seeding them makes the first
boot the same as the hundredth.

The drive parameters in there describe the 1.6 GB disk every Photo Play and
I.G.O. image is, so an image of that size is found without the BIOS being asked
anything.

`dpu414.nvr` is the printer's pack, holding a full one. A missing pack file is
a new pack, so this one only saves the first customer a surprise.

These are written back as the machine runs, and they are yours from then on --
delete the folder and it comes back seeded from the ROM instead, which is the
hard disk error again.
