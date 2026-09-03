DONGTEST -- read the Photo Play / I.G.O. dongle's answers off real hardware
===========================================================================

What this is for
----------------
We can now decrypt the picture archives, but only where we already know the
plaintext. The one thing still missing is what the dongle *replies* -- it is a
five-bit-in, one-bit-out part with internal state, and nothing in the game
binaries gives that up. This reads it directly.

Two builds, use whichever runs
------------------------------
DONGTEST.EXE   Windows, 32-bit, built for XP (subsystem 5.01, no C runtime,
               imports only KERNEL32). **Run it as Administrator.**
DONGTEST.COM   plain DOS, 302 bytes. Use this if the EXE refuses, or from a
               DOS boot disk. Runs under 32-bit XP's DOS box.

Both write DONGTEST.BIN into the current directory. Send that file back.

How to run
----------
1. Plug the dongle into the parallel port. Nothing else on that port.
2. Open a Command Prompt **as Administrator** (right-click, Run as
   administrator), cd to the folder, and run:

       DONGTEST.EXE

   If your LPT is not at 0x378, pass the base in hex:

       DONGTEST.EXE 278
       DONGTEST.EXE 3BC

3. It prints "Done -- DONGTEST.BIN written." and takes a second or two.
4. Send DONGTEST.BIN back.

If it says "Could not get I/O privilege"
----------------------------------------
That means the process was refused direct port access. Either it is not
running as Administrator, or Windows is 64-bit (the mechanism it uses only
exists on 32-bit Windows). In that case use DONGTEST.COM instead, from a
command prompt or a DOS boot disk.

How you know it is finished
---------------------------
It beeps and waits on "Press Enter to close.", so the window stays up even if you
double-clicked it. The failure paths do the same.

It is safe
----------
It only writes to the parallel port's data register and reads its status
register, which is exactly what the game does. It does not write to the
dongle's memory, does not install anything, and does not touch the disk apart
from creating DONGTEST.BIN.

What is in DONGTEST.BIN
-----------------------
  0x0000   256 bytes  every DATA value 0..255 written, STATUS read back after
                      each -- shows whether the part answers at all and what
                      idle looks like
  0x0100  2048 bytes  for each of the 256 possible preamble seed bytes: open a
                      round with it, then ask query 0 sixty-four times, packed
                      eight answers to a byte. We could not pin the seed
                      constant from the binary, so all 256 are swept
  0x0900   512 bytes  for the two candidate seeds (0x61, 0x8B), every query
                      0..31, sixty-four answers each
  0x0B00    64 bytes  the same 256-query run twice over, to show whether the
                      preamble really resets the part between rounds

Total 2880 bytes.

The protocol it speaks, for the record
--------------------------------------
Read out of I.G.O. 2's FINDIT.EXE:

  DATA bit 7  always 1
  DATA bit 4  clock for a query byte
  DATA bit 0  clock for a command byte
  DATA bits 1,2,3,5,6  the five payload bits
  STATUS bit 5  the answer

  a command byte b : write (b & 0xFE)|0x80, then b|0x81, then (b & 0xFE)|0x80
  a query q        : payload = ((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80
                     write payload, payload|0x10, payload, then read STATUS
  a round opens    : command(seed), command(0x4E), write 0x84
