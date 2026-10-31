#!/bin/sh

# shellcheck source=/dev/null
. /usr/lib/rauc/cmdline.sh

# RAUC hook script for devices booted through the Android bootloader.
# Meant to be used as a RAUC bootloader-custom-backend script.
#
# The boot image of each slot is flashed to the boot_a/boot_b partitions, which
# fix the system slot with rauc.slot on their kernel command line. Switching
# the active slot is not implemented yet, so only the booted slot is reported
# and installing an update to the other slot fails. The Android boot slot is
# marked successful by qbootctl.service.

booted_slot() {
    get_value rauc.slot "$(cat /proc/cmdline)"
}

case "$1" in
    get-primary)
        echo "android get-primary" >&2
        booted_slot
        ;;
    set-primary)
        # Marking the booted slot as active is a no-op
        if [ "$2" = "$(booted_slot)" ]; then
            echo "android set-primary $2: already the booted slot" >&2
            exit 0
        fi
        echo "android set-primary $2: switching slots is not supported yet" >&2
        exit 1
        ;;
    get-state)
        echo "android get-state $2" >&2
        echo "good"
        ;;
    set-state)
        echo "android set-state $2 $3" >&2
        ;;
    *)
        echo "Unknown command: $1" >&2
        exit 1
        ;;
esac
