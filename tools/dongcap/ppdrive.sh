#!/bin/bash
# Drive the PeepeeBox window on the host's own X display, and see what it is showing.
#
# The touchscreen is a tablet in 86Box terms, so the host pointer maps straight onto the
# guest's screen -- moving the X pointer over the window and clicking is a touch.  That is
# what lets the games be reached without a physical panel.
#
#   ./ppdrive.sh shot [file]     capture the emulator window
#   ./ppdrive.sh geom            window id and geometry
#   ./ppdrive.sh click X Y       touch at guest coordinates (0,0 is top-left of the screen)
#   ./ppdrive.sh move X Y        move without clicking
#   ./ppdrive.sh key KEY         send a key (e.g. Return, Escape, F1)
#   ./ppdrive.sh raw X Y         click at raw screen coordinates
set -u

export DISPLAY="${DISPLAY:-:0}"
XA=$(ls /var/run/xauth/A:0-* 2>/dev/null | head -1)
[ -n "$XA" ] && export XAUTHORITY="$XA"

WID=$(wmctrl -l | grep -i peepeebox | head -1 | cut -d' ' -f1)
if [ -z "$WID" ]; then
    echo "no PeepeeBox window found" >&2
    exit 1
fi

# The guest's picture sits inside the window's frame; get the drawable's origin and size
# so guest coordinates can be mapped onto the screen.
eval "$(xdotool getwindowgeometry --shell "$WID")"

case "${1:-geom}" in
geom)
    echo "window $WID at ${X},${Y} size ${WIDTH}x${HEIGHT}"
    ;;
shot)
    OUT=${2:-/root/rig/screen.png}
    import -window "$WID" "$OUT" 2>/dev/null || xwd -id "$WID" | convert xwd:- "$OUT"
    ls -l "$OUT"
    ;;
move | click)
    GX=${2:-0}; GY=${3:-0}
    # guest is 640x480 scaled into WIDTHxHEIGHT
    SX=$(( X + GX * WIDTH / 640 ))
    SY=$(( Y + GY * HEIGHT / 480 ))
    xdotool mousemove "$SX" "$SY"
    if [ "$1" = "click" ]; then
        sleep 0.3
        xdotool click 1
        echo "touched guest ($GX,$GY) -> screen ($SX,$SY)"
    else
        echo "moved to guest ($GX,$GY) -> screen ($SX,$SY)"
    fi
    ;;
raw)
    xdotool mousemove "${2}" "${3}" click 1
    echo "clicked raw (${2},${3})"
    ;;
key)
    xdotool windowactivate "$WID" 2>/dev/null
    sleep 0.2
    xdotool key --window "$WID" "${2:-Return}"
    echo "sent key ${2:-Return}"
    ;;
*)
    echo "unknown command: $1" >&2
    exit 1
    ;;
esac
