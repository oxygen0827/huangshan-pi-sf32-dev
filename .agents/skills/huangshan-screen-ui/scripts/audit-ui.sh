#!/bin/sh
set -eu

direct_font_pattern='&lv_font_montserrat_'
small_font_pattern='lv_ext_(set_local_font|lable_set_fixed_font|label_set_indicated_font)\([^,]+,\s*([0-9]|1[0-5])\s*,'

if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
    printf '%s\n' 'lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);' |
        rg -q "$direct_font_pattern" || {
            echo "self-test failed: direct bitmap font was not detected" >&2
            exit 1
        }
    printf '%s\n' 'lv_ext_set_local_font(label, 12, color);' |
        rg -q "$small_font_pattern" || {
            echo "self-test failed: sub-16 px font was not detected" >&2
            exit 1
        }
    if printf '%s\n' 'lv_ext_set_local_font(label, FONT_SMALL, color);' |
        rg -q "$small_font_pattern"; then
        echo "self-test failed: FONT_SMALL was incorrectly rejected" >&2
        exit 1
    fi
    echo "audit-ui self-test passed"
    exit 0
fi

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <ui-file>... | --self-test" >&2
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

    if rg -n "$direct_font_pattern" "$file"; then
        echo "  warning: direct Montserrat bitmap font bypasses the SiFli managed-font path"
        echo "           prefer lv_ext_set_local_font(..., FONT_*, ...) unless target evidence justifies it"
        status=1
    fi

    if rg -n "$small_font_pattern" "$file"; then
        echo "  warning: inspect information text below the verified 16 px readability floor"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "audit found items requiring review; warnings are not automatic proof of a bug" >&2
fi

exit "$status"
