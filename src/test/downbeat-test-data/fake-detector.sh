#!/bin/sh
# A stand-in for a downbeat detector in the tests of the muxic fork.
#
# Usage: fake-detector.sh MODE INPUT [OUTPUT] [PIDFILE]
#
# MODE picks what the program does:
#   good      write a beat list at 128 beats per minute, first bar at beat 2
#   shifted   the same list, but the first bar is at beat 0
#   scatter   a downbeat every five beats, thus no bar phase fits
#   empty     write nothing
#   garbage   write lines that hold no time and no place
#   fail      write nothing and stop with the code 3
#   slow      sleep, thus the caller runs into its timeout. The sleep runs
#             as a child, and PIDFILE takes its number, thus a test can
#             see whether the kill reached the whole process group
#
# The program writes to OUTPUT when the caller names one, else to its
# standard output. The list does not depend on INPUT.

mode="$1"
output="$3"

if [ "$mode" = "slow" ]; then
    sleep 120 &
    child=$!
    if [ -n "$4" ]; then
        printf '%s\n' "$child" > "$4"
    fi
    wait "$child"
    exit 0
fi
if [ "$mode" = "fail" ]; then
    echo "the fake detector stopped" >&2
    exit 3
fi
if [ "$mode" = "empty" ]; then
    if [ -n "$output" ]; then
        : > "$output"
    fi
    exit 0
fi

write_list() {
    if [ "$mode" = "garbage" ]; then
        printf 'not a time\n'
        printf '\n'
        printf 'x\ty\n'
        printf '1.0\n'
        return
    fi
    # 128 beats per minute is 0.46875 seconds for each beat. 200 beats give
    # 50 bars, which is more than the significance test needs.
    awk -v mode="$mode" 'BEGIN {
        step = 0.46875
        pickup = (mode == "shifted") ? 0 : 2
        period = (mode == "scatter") ? 5 : 4
        for (beat = 0; beat < 200; beat++) {
            place = ((beat - pickup) % period + period) % period + 1
            printf "%.5f\t%d\n", beat * step, place
        }
    }'
}

if [ -n "$output" ]; then
    write_list > "$output"
else
    write_list
fi
exit 0
