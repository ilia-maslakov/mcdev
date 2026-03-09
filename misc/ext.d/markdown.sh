#!/bin/sh

# Render markdown files for mc internal viewer (F3).
#
# Headings displayed as bold, inline `code` as underlined,
# tables formatted with column alignment and word wrap.
#
# $1 - action
# $2 - type of file

action=$1
filetype=$2

SCRIPT_DIR=$(dirname "$0")

do_view() {
    # Phase 1: render markdown to nroff-style text,
    #          tables bracketed by __MC_TABLE_BEGIN__/__MC_TABLE_END__
    # Phase 2: pipe through shell to format tables via separate awk program

    awk -f "$SCRIPT_DIR/markdown_common.awk" \
        -f "$SCRIPT_DIR/markdown_render.awk" \
        "$MC_EXT_FILENAME" | {

        in_table=0
        table_tmp=""

        while IFS= read -r line; do
            if [ "$line" = "__MC_TABLE_BEGIN__" ]; then
                in_table=1
                table_tmp=$(mktemp /tmp/mc-md-table.XXXXXX) || exit 1
                continue
            fi
            if [ "$line" = "__MC_TABLE_END__" ]; then
                in_table=0
                awk -f "$SCRIPT_DIR/markdown_common.awk" \
                    -f "$SCRIPT_DIR/markdown_table.awk" \
                    "$table_tmp"
                rm -f "$table_tmp"
                table_tmp=""
                continue
            fi

            if [ "$in_table" -eq 1 ]; then
                printf '%s\n' "$line" >> "$table_tmp"
            else
                printf '%s\n' "$line"
            fi
        done
    }
}

case "${action}" in
view)
    do_view
    ;;
*)
    ;;
esac
