FNLIC -- fun.net licence numbers
================================

What this is for
----------------
The operator setup's fun.net registration page will not move on until it is given
a licence number that passes its own check -- `NL-893-VISS-461OFK` and the like.
The check is entirely local: three letters at the end computed from the twelve in
front of them. Nothing about the machine goes into it, so one algorithm covers
every cabinet and every release from 2001 to I.G.O. 6, byte for byte.

This checks numbers, and makes them.

    python fnlic.py selftest
    python fnlic.py check NL-893-VISS-461OFK --explain
    python fnlic.py gen --country NL -n 5
    python fnlic.py gen --body PP2001NL0001        # complete a body you chose

Dashes are optional on input, and `--plain` drops them on output.

What it does not do
-------------------
It does not register a cabinet. Passing this check only lets the page accept the
form; the machine licence itself (`machlic`) comes back from a fun.net server on
the next data transmission, and fun.net is gone. See the last section of
docs/research/34-funnet-licence.md.

Where the algorithm comes from
------------------------------
`\FN_SYS\FN_SYS.EXE`, the 458-byte routine at image offset 0x626E, reached from
the registration page at 0x6739. docs/research/34-funnet-licence.md has the
disassembly, the derivation, and the check against the one known-good sample --
the licence a real registered 2001 cabinet still had in its SETTINGS.TAB.

Requirements
------------
Python 3.8 or later. No third-party modules.
