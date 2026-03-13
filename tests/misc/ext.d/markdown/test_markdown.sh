#!/bin/sh

# Test markdown rendering scripts for stability and correctness.
#
# For each *.md file in the data directory:
#   1. Render it twice and verify the output is identical (stability).
#   2. Run basic sanity checks on the output (headings are bold, etc.).
#
# Usage:
#   test_markdown.sh [--data-dir DIR] [--scripts-dir DIR]
#
# --data-dir     Directory with *.md test files (default: ./data)
# --scripts-dir  Directory with markdown*.awk and markdown.sh (default: auto-detect)

data_dir=""
scripts_dir=""

while [ $# -gt 0 ]; do
    case "$1" in
        --data-dir)    data_dir="$2"; shift 2 ;;
        --scripts-dir) scripts_dir="$2"; shift 2 ;;
        *)             echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# Auto-detect directories relative to this script
self_dir=$(cd "$(dirname "$0")" && pwd)

if [ -z "$data_dir" ]; then
    data_dir="$self_dir/data"
fi

if [ -z "$scripts_dir" ]; then
    scripts_dir=$(cd "$self_dir/../../../../misc/ext.d" 2>/dev/null && pwd)
    if [ -z "$scripts_dir" ] || [ ! -f "$scripts_dir/markdown.sh" ]; then
        echo "Cannot find markdown scripts. Use --scripts-dir." >&2
        exit 1
    fi
fi

if [ ! -d "$data_dir" ]; then
    echo "Data directory not found: $data_dir" >&2
    exit 1
fi

render_markdown() {
    MC_EXT_FILENAME="$1" sh "$scripts_dir/markdown.sh" view ''
}

fail=0
count=0
pass=0

for input in "$data_dir"/*.md; do
    [ -f "$input" ] || continue
    base=$(basename "$input" .md)
    count=$((count + 1))
    errors=""

    # Test 1: stability -- two renders produce identical output
    out1=$(render_markdown "$input")
    out2=$(render_markdown "$input")

    if [ "$out1" != "$out2" ]; then
        errors="${errors}  output is not stable across runs\n"
    fi

    # Test 2: output is not empty
    if [ -z "$out1" ]; then
        errors="${errors}  output is empty\n"
    fi

    # Test 3: content-specific sanity checks
    case "$base" in
        headings)
            # Headings should produce nroff bold (backspace sequences)
            if ! printf '%s' "$out1" | grep -q "$(printf '\b')"; then
                errors="${errors}  no nroff backspace sequences in heading output\n"
            fi
            ;;
        inline)
            # Bold text should have backspace sequences
            if ! printf '%s' "$out1" | grep -q "$(printf '\b')"; then
                errors="${errors}  no nroff backspace sequences in inline output\n"
            fi
            ;;
        table)
            # Table output should not be empty and should not contain raw markers
            if printf '%s' "$out1" | grep -q "__MC_TABLE_"; then
                errors="${errors}  raw table markers leaked into output\n"
            fi
            ;;
    esac

    if [ -z "$errors" ]; then
        echo "PASS: $base"
        pass=$((pass + 1))
    else
        echo "FAIL: $base"
        printf '%b' "$errors"
        fail=1
    fi
done

echo ""
echo "$pass/$count tests passed."
exit $fail
