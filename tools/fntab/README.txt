FNTAB -- read the fun.net .TAB tables
=====================================

What this is for
----------------
Everything under \FN_SYS\DATABASE\ is one format: a flat array of fixed-size
records, one in-use flag byte then a payload. The payload size is not in the
file -- it is a constant in whichever binary opens the table -- so a reader has
to be told, and this one already knows for about forty tables.

    python fntab.py sizes                       every known record size
    python fntab.py info  PLAYER.TAB            size, slots, how many are live
    python fntab.py map   PLAYER.TAB            what kind of thing is at each offset
    python fntab.py read  PLAYER.TAB --limit 5  decode records

`map` and `read` work on any table; `read` decodes named fields for the ones
whose schema is worked out (PLAYER, LOGIN, TECHDATA, SETTINGS) and falls back to
a hexdump otherwise. Pass --record-size for a table not in the built-in list.

--mask
------
`read --mask` replaces every letter and digit with 'x', keeping punctuation and
field boundaries. That shows the layout -- that a field holds "xxxx xx" or
"x@x.xxx" -- without showing what is in it.

Several of these tables are a real pub's customer records: PLAYER and LOGIN hold
names, addresses, birthdates and phone numbers, FMMSGIN/FMMSGOUT hold private
messages between named people, and SMSDATA holds SMS text. The format work in
docs/research/35-fn-tables.md was done entirely with --mask and set comparisons,
and did not need the contents. Use --mask unless you have a reason not to.

Where the sizes come from
-------------------------
The open call in each binary:

    push  dword N          payload size
    push  word  bufsize
    push  word  path
    push  handle
    call  <db open>

Seven binaries were read and they agree on every shared table.
docs/research/35-fn-tables.md has the derivation, the full size table, and the
PLAYER schema.

Requirements
------------
Python 3.8 or later. No third-party modules.
