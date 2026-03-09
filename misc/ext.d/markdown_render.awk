# markdown_render.awk -- convert markdown to nroff-style view
#
# Requires: markdown_common.awk (boldify)
#
# - Headings (# ...) rendered as bold
# - Inline `code` rendered as underlined
# - Tables delimited by __MC_TABLE_BEGIN__/__MC_TABLE_END__ markers
#   for post-processing by markdown_table.awk

function is_table_sep(s) {
    return (s ~ /^[[:space:]]*\|?[[:space:]]*:?-+:?[[:space:]]*(\|[[:space:]]*:?-+:?[[:space:]]*)+\|?[[:space:]]*$/)
}

function underline_code(s,    out, i, ch, in_code) {
    out = ""
    in_code = 0
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == "`") {
            in_code = !in_code
            continue
        }
        if (in_code && ch != " ")
            out = out "_\b" ch
        else
            out = out ch
    }
    return out
}

# collect all lines first so we can look ahead for table separators
{ lines[++n] = $0 }

END {
    for (i = 1; i <= n; i++) {
        # detect table: current line has "|" and next line is a separator row
        if (index(lines[i], "|") > 0 && i < n && is_table_sep(lines[i + 1])) {
            print "__MC_TABLE_BEGIN__"
            print lines[i]
            i++
            print lines[i]
            while (i < n && lines[i + 1] ~ /\|/ && lines[i + 1] !~ /^[[:space:]]*$/) {
                i++
                print lines[i]
            }
            print "__MC_TABLE_END__"
            continue
        }

        line = lines[i]
        if (match(line, /^#+[ \t]*/)) {
            rest = substr(line, RLENGTH + 1)
            print boldify(rest)
        } else {
            print underline_code(line)
        }
    }
}
