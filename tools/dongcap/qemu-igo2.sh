#!/bin/bash
# Run the I.G.O. 2 cabinet image under QEMU with the REAL dongle passed through.
#
# The point is not emulation for its own sake.  Every hand-written probe has been silent
# even with the port proven live by a pin 9 -> pin 10 jumper, so this stops guessing the
# frame and lets the game -- which demonstrably works on the cabinet -- drive the part
# itself.  QEMU's -parallel /dev/parport0 backs the guest's LPT1 with the host's real port
# through ppdev.
#
# Two things come out of it:
#   1. does the game talk to the dongle at all through this path (watch the screen), and
#   2. the exact wire sequence, because every access becomes a ppdev ioctl that strace can
#      record.  That is the trace this project has never had.
#
# GEOMETRY.  The image's own partition table ends at head 143 / sector 63, so the disk was
# presented to DOS as 144 heads x 63 sectors -- not the 63/16 that PeepeeBox derives from
# file size for its own images.  Getting this wrong stops the boot dead at
# "Booting from Hard Disk..." with the MBR loaded and PTS-DOS never starting, which is
# exactly what the first attempt did.
#
#   ./qemu-igo2.sh fixbpb    patch the BPB head count to 64 (once, before the first boot)
#   ./qemu-igo2.sh start     boot it on the machine's own monitor, tracing the port
#   ./qemu-igo2.sh shot      screenshot to shot.png
#   ./qemu-igo2.sh trace     summarise what has hit the parallel port
#   ./qemu-igo2.sh stop      kill it

set -u
DIR=/root/dong
IMG=$DIR/ppigo2pt.img
MON=$DIR/qmon.sock
PID=$DIR/qemu.pid
TRACE=$DIR/pp.log

# GEOMETRY -- this took a while to pin down, so it is written out in full.
#
# The image's BPB says 63 sectors/track and 16 heads; 1655635968/512/(16*63) = 3208
# cylinders.  Booting with exactly that (bios-chs-trans=none) gets PTS-DOS as far as its
# funworld splash and then "Bad or missing command interpreter".  The reason is in
# CONFIG.PTS: `shell = \PTSDOS\command.com`, and \PTSDOS\COMMAND.COM lives at cluster
# 30681 -- about 1 GB in, which with 16 heads is cylinder 1948.  INT 13h CHS stops at
# cylinder 1023, so it is unreachable.  The kernel itself loads fine because it sits at
# cylinder 18.
#
# That is also why the MBR's end-CHS claims 144 heads: the original BIOS translated so the
# whole disk fitted inside 1024 cylinders.  QEMU's ide-hd refuses more than 16 heads, so
# SeaBIOS has to do the translating -- bios-chs-trans=large doubles heads until it fits,
# 3208/16 -> 1604/32 -> 802/64.
#
# The catch: the boot sector computes CHS from the BPB, so with the BIOS on 64 heads and
# the BPB on 16 they disagree and the VBR loads garbage -- which is exactly why plain
# "large" and "lba" died even earlier than "none" did.  So the BPB has to be patched to
# match.  `./qemu-igo2.sh fixbpb` does that, keeping a .bak.
# CPU: the cabinet is a 486 DX4, but the only boot that has ever got past the logo did
# so under -cpu pentium; every -cpu 486 run has stalled with a program spinning near
# the BIOS keyboard entry.  Override with CPU=486 when testing that specifically.
CYLS=3208
HEADS=16
SECS=63
TRANS=large
BPB_HEADS_OFF=$((32256 + 0x1A))

mon() { printf '%s\n' "$1" | socat - UNIX-CONNECT:$MON 2>/dev/null; }

case "${1:-start}" in
fixbpb)
  # set the BPB's head count to 64 so it agrees with what SeaBIOS reports under "large"
  rd() { dd if="$IMG" bs=1 skip=$BPB_HEADS_OFF count=2 status=none | od -An -tx1 | tr -d ' 
'; }
  echo "BPB heads before: $(rd)"
  if [ "$(rd)" != "4000" ]; then
    cp "$IMG" "$IMG.bak"
    printf '\x40\x00' | dd of="$IMG" bs=1 seek=$BPB_HEADS_OFF conv=notrunc status=none
  fi
  echo "BPB heads after : $(rd)   (4000 = 64, little-endian)"
  ;;
start)
  [ -f "$IMG" ] || { echo "no image at $IMG"; exit 1; }
  pkill -F $PID 2>/dev/null
  pkill -f 'strace .*-e trace=ioctl' 2>/dev/null
  rm -f $MON
  sleep 1

  # Show the guest on the machine's own monitor.  Root needs the running X server's
  # cookie, which the display manager leaves in /var/run/xauth.
  export DISPLAY="${DISPLAY:-:0}"
  XA=$(ls /var/run/xauth/A${DISPLAY}-* 2>/dev/null | head -1)
  [ -n "$XA" ] && export XAUTHORITY="$XA"
  if xdpyinfo >/dev/null 2>&1; then
    DISP="-display gtk"
    echo "window on $DISPLAY (xauth ${XA:-none})"
  else
    DISP="-display none -vnc :0"
    echo "no usable X on $DISPLAY -- falling back to VNC :0"
  fi

  # Log the parallel port by interposing ioctl() rather than with strace: strace -p
  # stops the process on every ioctl including the display's, which on this Atom
  # slows the guest to a crawl.  Build it once if it is missing.
  [ -f $DIR/pptrace.so ] || cc -O2 -shared -fPIC -o $DIR/pptrace.so $DIR/pptrace.c -ldl
  # The cabinet has a 3.5 inch floppy as drive A: (docs/hardware.md), and UPDATE/CHECK.BAT
  # opens with 'checkupd a:'.  With no drive at all DOS sits in a floppy timeout behind
  # the logo and never reaches MENU/MAIN.COM, so give it an empty formatted disk.
  [ -f $DIR/blank144.img ] || { dd if=/dev/zero of=$DIR/blank144.img bs=512 count=2880 status=none; mkfs.fat -F12 $DIR/blank144.img >/dev/null 2>&1; }
  export PPTRACE_LOG=$DIR/pp.log
  export LD_PRELOAD=$DIR/pptrace.so

  qemu-system-i386 \
    -machine pc,accel=kvm:tcg \
    -cpu ${CPU:-pentium} \
    -m 64 \
    -drive file=$IMG,format=raw,if=none,id=hd0 \
    -device ide-hd,drive=hd0,bus=ide.0,unit=0,cyls=$CYLS,heads=$HEADS,secs=$SECS,bios-chs-trans=$TRANS \
    -fda $DIR/blank144.img \
    -parallel /dev/parport0 \
    -vga std \
    $DISP \
    -monitor unix:$MON,server,nowait \
    -boot c \
    -pidfile $PID \
    -name igo2 &

  sleep 3
  QPID=$(cat $PID 2>/dev/null)
  if [ -z "$QPID" ]; then echo "qemu did not start"; exit 1; fi
  echo "qemu pid $QPID   geometry ${CYLS}/${HEADS}/${SECS}"
  echo "parallel-port log: $DIR/pp.log"
  ;;
shot)
  mon "screendump $DIR/shot.ppm" >/dev/null
  sleep 1
  convert $DIR/shot.ppm $DIR/shot.png 2>/dev/null
  ls -l $DIR/shot.png 2>/dev/null || echo "no screenshot"
  ;;
trace)
  [ -f $TRACE ] || { echo "no trace yet"; exit 1; }
  echo "lines: $(wc -l < $TRACE)"
  echo "--- ioctls by kind ---"
  grep -o 'PP[A-Z]*' $TRACE | sort | uniq -c | sort -rn
  echo "--- first 40 port accesses ---"
  head -40 $TRACE
  ;;
stop)
  pkill -F $PID 2>/dev/null
  pkill -f 'strace .*-e trace=ioctl' 2>/dev/null
  echo stopped
  ;;
esac
