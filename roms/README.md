# ROMs

PeepeeBox emulates exactly one machine, so it needs exactly two ROM images.
Both are reproduced here from the upstream
[86Box ROM repository](https://github.com/86Box/roms), unmodified:

| File | What it is |
|---|---|
| `machines/4dps/4DPS172G.BIN` | System BIOS for the Zida Tomato 4DPS (SiS 496), the motherboard used in the Photo Play cabinets |
| `video/cirruslogic/clgd5480.rom` | Video BIOS for the Cirrus Logic CL-GD5480 |

Nothing else is needed and nothing else is shipped. The upstream set also
carries icon artwork under `icons/`; that is consumed by WinBox and the legacy
manager, neither of which exists in the Qt build, so it is not included.

`LICENSE` is the upstream licence notice and applies unchanged: these images are
the property of their respective copyright owners and are reproduced here in the
interest of preservation.
