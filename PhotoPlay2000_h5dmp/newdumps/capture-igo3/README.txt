CAPTURE -- I.G.O. 2 (passwords 68BB / 1329)
===========================================

What this does
--------------
Asks a real Photo Play / I.G.O. dongle for the one value PeepeeBox cannot compute: the
keyed round at the heart of the picture cipher.

It writes two files.

  DONGCAP.BIN   two keyed-round outputs per 4 KB buffer.  That is everything needed to
                decrypt that buffer, because the rest of it is keyless software the
                emulator already implements.  This is what makes I.G.O. 2's photos work.

  DONGCAP.BIT   the forty raw answers the part gives during each round -- one byte out,
                one bit back -- five bytes per round, MSB first.  This is the more
                valuable of the two.  33,892 keyed-round input/output pairs already exist
                for 2001 and the byte-to-bit function is still unsolved, so more of those
                will not settle it; these are direct observations of the oracle itself,
                under a security table we already hold.  Solving it there carries over to
                the 7477/7D57 and 6B91/24A3 parts, which are not to hand.

Send BOTH files back.

The work list covers FINDIT/PICS and AMORE/COMIX -- the only two archives in the release
behind this cipher.  23018 distinct buffers, 46036 keyed rounds.  All eight I.G.O. 2
territories and the PT cabinet's own disk produce byte-identical lists, so this one
capture covers every I.G.O. 2 image.

You need
--------
  * the I.G.O. 2 dongle -- passwords 68BB / 1329, the 2002 / 2006 / 2007 pair
  * a REAL parallel port.  A USB-to-parallel adapter cannot do this: it is a printer-class
    device with no bit-level control of DATA and STATUS, and the handshake needs both.

Nothing is written to the dongle.  It is only ever asked questions, which is what the
game does.  Nothing is installed, and nothing else on the disk is touched.

Run it -- DOS, on the cabinet itself
------------------------------------
This is the host we know works: the dongle is already talking to that port, so a failure
there says something about the protocol rather than about a modern box's port mode, base
address or timing.  Copy DONGCAP.COM and DONGCAP.LST into a directory on C: and run

    DONGCAP

It sweeps every parallel port the BIOS lists, so normally it needs no argument.  Give one
only to pin it to a single base the BIOS did not enumerate:

    DONGCAP 278
    DONGCAP 3BC

Expect well under a minute.  It writes DONGCAP.BIN (184 KB) and DONGCAP.BIT (230 KB)
into the current directory, so run it somewhere with half a megabyte free.

Run it -- Linux
---------------
    cc -O2 -o dongcap dongcap_linux.c
    sudo ./dongcap                 # LPT at 0x378
    sudo ./dongcap 278             # or wherever the port is

A PCIe card usually sits well above 0x3FF.  Find its base with

    grep -i parport /proc/ioports

and pass the first number of the range.  Root is needed for port access.

What you should see
-------------------
    BIOS parallel ports (base=STATUS):  0378=DF
    Calibrating -- every seed on every port
      port 0378
    Seed 0xNN reproduces every calibration pair at port 0378.
    Capturing .............................................
    Done -- DONGCAP.BIN and DONGCAP.BIT written. Send both back.

The port line is the first thing to read.  STATUS is only READ there, never written, so
that probe cannot disturb anything.  A port with nothing attached floats high and reads
FF -- if every port shows FF, the part is not answering and no seed will ever match.

It then tries every port the BIOS enumerated AND any of 03BC / 0378 / 0278 the BIOS left
out, so a dongle on a port the BIOS never registered is still found and you never have to
guess a base.  Nothing outside that set is touched -- the funworld I/O card is an 8255 at
a DIP-selected base and is left strictly alone.  MENU.EXE carries those same three bases
as immediates and probes them itself, so this is no more intrusive than booting the game.

Calibration is the part that matters most.  Before recording anything it hunts the
preamble seed by trying all 256 of them against two answers already known from offline
work:

    f(504EF2AE) = 32FC6611
    f(012C6137) = DF57708B

If a seed reproduces both, the part is answering and the model of the cipher is confirmed
on hardware for the first time.

If it says "No seed reproduces the known answers"
-------------------------------------------------
That is a result, not a failure, which is why it writes DONGCAP.DIA instead of giving up:
one section per port -- the base as a word, then 256 dwords, one per seed -- which says
what the part actually answered.  Send DONGCAP.DIA back.  Worth checking first, though, in this order:

  1. the LPT base -- try 378, 278, 3BC
  2. the port's mode in the BIOS.  SPP / "Normal" is what this wants; ECP or EPP can
     interfere.  Bidirectional is not needed.
  3. that the dongle is the I.G.O. 2 one.  A 2001 (7477/7D57) or 2003/2005 (6B91/24A3)
     part will answer, and will answer WRONGLY, because the calibration pairs are for
     68BB/1329.  From here that looks the same as a dead port.
  4. that nothing else has the port -- no printer driver, and on Linux unload lp/ppdev.

Files
-----
    DONGCAP.LST        the work list -- 23018 buffers, generated by ../mklist.py from
                       IGO2/IGO 2 PT Real Machine/PPIGO2PT.img
    DONGCAP.COM        the DOS program, 16384 bytes, assembled by ../mkdongcap_dos.py
    dongcap_linux.c    the Linux program -- same wire, same files
    DONGTEST.COM       a 302-byte fallback probe -- see ../../dongtest/README.txt.
                       Phase 1 writes every DATA value 0..255 and reads STATUS back after
                       each, which says whether the port responds to anything at all.
                       NOTE: it only ever looks at LPT1 from the BIOS table, so run
                       DONGCAP first and believe its port line over this one.
    DONGCAP.BIN        \  what a run produces.
    DONGCAP.BIT        /  Send both back.
    DONGTEST.BIN       what DONGTEST produces, if you get that far.

About DONGCAP.COM
-----------------
It is hand-assembled machine code -- there is no DOS compiler on the build machine -- and
it was originally verified by disassembly alone.  It has since run on the cabinet, and
that run validated it: with the part not answering, every one of the 256 seeds returned
BDC587AC, which is exactly what the software model produces when STATUS bit 5 reads 1 on
all forty consultations.  The arithmetic therefore matches the reference bit for bit.

The port sweep and the STATUS probe were added after that run and have NOT been executed.
The failure mode stays benign: a mistake shows up as calibration finding no seed, and it
refuses to write a table it cannot vouch for.
