#!/bin/sh

# shellcheck source=/dev/null
. /usr/lib/rauc/cmdline.sh

# RAUC hook script for devices booted through the Android bootloader.
# Meant to be used as a RAUC bootloader-custom-backend script.
#
# The bootloader cannot switch the system slot reliably, so the initramfs does
# it: the first sector of the bootstate partition holds a text line like
#   JSLT1 primary=A tries_a=3 good_a=0 tries_b=0 good_b=1
# The initramfs boots the primary slot while it is confirmed good or has tries
# left (each boot of an unconfirmed slot uses one), and falls back to the other
# slot otherwise. A blank or invalid block means a fresh install: slot A, good.

STATE_DEV=/dev/disk/by-partuuid/33236519-7f32-4dff-8002-3390b62c309d
TRIES=3

PRIMARY=A
TRIES_A=0
GOOD_A=1
TRIES_B=0
GOOD_B=0

load_state() {
    line=$(dd if="$STATE_DEV" bs=512 count=1 2>/dev/null | tr -d '\000')
    case "$line" in
        "JSLT1 primary="*)
            PRIMARY=$(get_value primary "$line")
            TRIES_A=$(get_value tries_a "$line")
            GOOD_A=$(get_value good_a "$line")
            TRIES_B=$(get_value tries_b "$line")
            GOOD_B=$(get_value good_b "$line")
            ;;
    esac
}

save_state() {
    printf 'JSLT1 primary=%s tries_a=%s good_a=%s tries_b=%s good_b=%s\n' \
        "$PRIMARY" "$TRIES_A" "$GOOD_A" "$TRIES_B" "$GOOD_B" |
        dd of="$STATE_DEV" bs=512 count=1 conv=sync,fsync 2>/dev/null
}

slot_ok() {
    case "$1" in
        A | B) ;;
        *)
            echo "android: unknown slot '$1'" >&2
            exit 1
            ;;
    esac
}

load_state

case "$1" in
    get-primary)
        echo "android get-primary: $PRIMARY" >&2
        echo "$PRIMARY"
        ;;
    set-primary)
        slot_ok "$2"
        echo "android set-primary $2" >&2
        PRIMARY=$2
        # An unconfirmed slot gets a few boots to prove itself
        if [ "$2" = A ]; then
            TRIES_A=$TRIES
        else
            TRIES_B=$TRIES
        fi
        save_state
        ;;
    get-state)
        slot_ok "$2"
        if [ "$2" = A ]; then
            good=$GOOD_A
        else
            good=$GOOD_B
        fi
        if [ "$good" = 1 ]; then
            echo good
        else
            echo bad
        fi
        ;;
    set-state)
        slot_ok "$2"
        case "$3" in
            good) good=1 ;;
            bad) good=0 ;;
            *)
                echo "android set-state: unknown state '$3'" >&2
                exit 1
                ;;
        esac
        echo "android set-state $2 $3" >&2
        if [ "$2" = A ]; then
            GOOD_A=$good
            [ "$good" = 1 ] || TRIES_A=0
        else
            GOOD_B=$good
            [ "$good" = 1 ] || TRIES_B=0
        fi
        save_state
        ;;
    *)
        echo "Unknown command: $1" >&2
        exit 1
        ;;
esac
