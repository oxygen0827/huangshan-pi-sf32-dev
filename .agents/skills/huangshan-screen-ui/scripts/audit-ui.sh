#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <ui-file>..." >&2
    exit 2
fi

status=0

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "$file: error: file not found" >&2
        status=1
        continue
    fi

    case "$file" in
        *.c|*.h|*.lua|*.json) ;;
        *)
            echo "$file: skipped: unsupported file type"
            continue
            ;;
    esac

    echo "auditing $file"

    if rg -n 'lv_obj_set_(pos|x|y)\([^,]+,\s*(0|[1-9]|1[0-9]|2[0-9]|3[0-9]|36[0-9]|37[0-9]|38[0-9])([,)]|\s)' "$file"; then
        echo "  warning: inspect absolute positions near a rounded screen edge"
        status=1
    fi

    if rg -n 'lv_obj_set_size\([^,]+,\s*([1-9]|[1-3][0-9]|4[0-3])\s*,|lv_obj_set_size\([^,]+,[^,]+,\s*([1-9]|[1-3][0-9]|4[0-3])\s*\)' "$file"; then
        echo "  warning: inspect controls smaller than the 44 px touch minimum"
        status=1
    fi

    if rg -n 'lv_obj_set_size\([^,]+,\s*390\s*,\s*450\s*\)' "$file"; then
        echo "  warning: prefer LV_HOR_RES_MAX/LV_VER_RES_MAX for full-screen objects"
        status=1
    fi

    if rg -n 'LV_ALIGN_(TOP|BOTTOM)_(LEFT|RIGHT).*,\s*-?([0-9]|1[0-9]|2[0-9])\s*,\s*-?([0-9]|1[0-9]|2[0-9])\s*\)' "$file"; then
        echo "  warning: inspect corner-aligned content outside the safe area"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "audit found items requiring review; warnings are not automatic proof of a bug" >&2
fi

exit "$status"
