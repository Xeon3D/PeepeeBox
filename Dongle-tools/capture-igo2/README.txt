CAPTURE -- I.G.O. 2   (dongle passwords 68BB/1329)
======================================

Run this on the XP laptop with the **I.G.O. 2 dongle** attached.

    1. Right-click Command Prompt -> Run as administrator
    2. cd to this folder
    3. DONGCAP.EXE            (or  DONGCAP.EXE 278  if LPT is not at 0x378)
    4. send DONGCAP.BIN back

It takes a while: 25280 buffers, 50560 keyed rounds, roughly 8089600 parallel-port
transactions. Expect several minutes. It prints a dot every 512 buffers.

What it is doing
----------------
Every 4 KB of an enciphered picture archive needs two dwords that only the dongle can
produce. DONGCAP.LST lists, for each buffer, the input to the first of those rounds --
worked out from the encrypted archive alone, no plaintext needed. The program asks the
part for the answer, derives the second input from it, asks again, and writes both.

With that table PeepeeBox decrypts these pictures itself, with the disk image left
completely alone.

Before capturing it calibrates: the round's opening seed byte never resolved from the
game binaries, so the list carries a few inputs whose correct answers we already know,
and the program tries all 256 seeds until one reproduces them. That also proves the part
is really answering before it spends minutes recording.

If it says no seed reproduces the known answers
-----------------------------------------------
It writes DONGCAP.DIAG instead -- every seed against the first inputs. Send that back.
It is not a hardware fault and not something to retry; it means the part answers
differently from what we predicted, and the DIAG file is exactly what is needed to work
out why.

Windows XP: use DONGCAP.COM, not DONGCAP.EXE
--------------------------------------------
DONGCAP.EXE cannot work on a stock XP and never could.  It asks for port access with
NtSetInformationProcess(ProcessUserModeIOPL), and that call needs SeTcbPrivilege --
"Act as part of the operating system" -- which XP grants to nobody by default, not even
to Administrators.  "Run as administrator" does not supply it.  That is the
"Could not get I/O privilege" message, and no amount of re-running changes it.

DONGCAP.COM is the same capture in real mode, where there is no such gate:

    DONGCAP.COM             (or  DONGCAP.COM 278  if LPT is not at 0x378)

It writes the same DONGCAP.BIN.  Run it in any of these, best first:

    * on a cabinet, or any DOS machine, with the dongle on its parallel port
    * from a FreeDOS stick booted on the XP laptop
    * at the XP command prompt -- NTVDM may hand the port straight through, and if it
      does this is the easiest route.  Try it: if it prints a seed and starts capturing,
      it is working.  If it calibrates through all 256 seeds and writes DONGCAP.DIA,
      NTVDM is in the way and one of the two above is needed.

If no seed calibrates it writes DONGCAP.DIA rather than nothing -- send that back, it
says what the part actually answered.
