MODEMTEST -- drive the COM4 modem the way the cabinet does
==========================================================

What this is for
----------------
`src/char/char_modem.c` emulates the Diamond SupraExpress 56e PRO that the
fun.net cabinets had on COM4.  Whether it is *right* is not a matter of taste:
the disk image carries funworld's own modem database in
`\FN_SYS\DATABASE\NETWORK\`, and FN_SYS.EXE identifies the part by sending the
commands in `MD_INFOS.CSV` and looking for the substrings in it.  Get a
substring wrong and the cabinet reports the wrong modem, or `Standard-Modem`,
or `ERROR: can't talk to modem`.

So this drives the device directly -- no emulated machine, no disk image -- with
the exact byte sequences FN_SYS sends, and checks:

  * the wake-up (`\r\r`, `AT`, `ATE0`) is answered
  * `ATI3` contains `SupraExpress`, so MD_NAME.CSV row 3 matches
  * nothing it answers matches any of the other twenty-nine rows
  * `ATI7`, whose answer is filed as MODEMFMW, comes back
  * every init string the tables hold for row 3 is accepted, including the one
    a real cabinet's captured NET.CFG carries
  * dialling into a dead line fails the way a real modem fails, and honours S7
    and the ATX level while doing it

See docs/research/33-modem.md for where each of those expectations comes from.

How to run
----------
    build.cmd

It needs the same mingw64 toolchain the emulator is built with, and nothing
else.  Exit status is 0 when every check passes.

Why it is not in the CMake build
--------------------------------
It stubs `char_attach`, `device_get_config_*`, `plat_get_ticks` and the socket
layer so the AT engine can be exercised on its own.  Those stubs must not end up
anywhere near a release binary.
