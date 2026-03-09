# markdown_table.awk -- format a markdown table for nroff-style view
#
# Requires: markdown_common.awk (boldify, trim, rep)
#
# Input:  raw markdown table rows (header + separator + data)
# Output: formatted table with bold header, column alignment, word wrap
#
# Variables (pass via -v):
#   WRAP_WIDTH  -- max cell width before wrapping (default 28)
#   MAX_ROW_LEN -- max total row length before wrapping (default 75)

BEGIN {
    if (WRAP_WIDTH == 0) WRAP_WIDTH = 28
    if (MAX_ROW_LEN == 0) MAX_ROW_LEN = 75
}

function push_seg(r, c, s,    k) {
    k = ++cnt[r, c]
    seg[r, c, k] = s
    if (length(s) > colw[c])
        colw[c] = length(s)
}

function add_wrapped_text(r, c, txt, w,    s, n, i, word, cur, chunk) {
    s = trim(txt)
    if (s == "") {
        push_seg(r, c, "")
        return
    }

    n = split(s, a, /[[:space:]]+/)
    cur = ""
    for (i = 1; i <= n; i++) {
        word = a[i]
        if (word == "")
            continue

        # break words longer than wrap width
        while (length(word) > w) {
            if (cur != "") {
                push_seg(r, c, cur)
                cur = ""
            }
            chunk = substr(word, 1, w)
            push_seg(r, c, chunk)
            word = substr(word, w + 1)
        }

        if (cur == "")
            cur = word
        else if (length(cur) + 1 + length(word) <= w)
            cur = cur " " word
        else {
            push_seg(r, c, cur)
            cur = word
        }
    }

    if (cur != "")
        push_seg(r, c, cur)
}

function is_sep_row(fields, nf,    i, t) {
    if (nf < 2)
        return 0
    for (i = 1; i <= nf; i++) {
        t = trim(fields[i])
        if (t !~ /^:?-+:?$/)
            return 0
    }
    return 1
}

# --- parse input ---
{
    line = $0
    sub(/^[[:space:]]*\|/, "", line)
    sub(/\|[[:space:]]*$/, "", line)

    nf = split(line, f, /\|/)
    if (is_sep_row(f, nf))
        next

    rows++
    if (nf > maxc)
        maxc = nf

    for (c = 1; c <= nf; c++)
        raw[rows, c] = trim(f[c])
}

# --- format output ---
END {
    if (rows == 0)
        exit

    # decide which rows need wrapping
    for (row = 1; row <= rows; row++) {
        row_len = 0
        need_wrap[row] = 0
        for (c = 1; c <= maxc; c++) {
            cell_len = length(raw[row, c])
            row_len += cell_len
            if (cell_len > WRAP_WIDTH)
                need_wrap[row] = 1
        }
        row_len += (maxc > 0 ? (maxc - 1) * 3 : 0)
        if (row_len > MAX_ROW_LEN)
            need_wrap[row] = 1
    }

    # break cells into segments
    for (row = 1; row <= rows; row++)
        for (c = 1; c <= maxc; c++)
            if (need_wrap[row])
                add_wrapped_text(row, c, raw[row, c], WRAP_WIDTH)
            else
                push_seg(row, c, raw[row, c])

    # render rows
    for (row = 1; row <= rows; row++) {
        rowh = 1
        for (c = 1; c <= maxc; c++)
            if (cnt[row, c] > rowh)
                rowh = cnt[row, c]

        for (k = 1; k <= rowh; k++) {
            out = ""
            for (c = 1; c <= maxc; c++) {
                cell = seg[row, c, k]
                if (cell == "")
                    cell = ""
                pad = colw[c] - length(cell)
                if (row == 1)
                    cell = boldify(cell)
                out = out (c == 1 ? "" : " | ") cell rep(" ", pad)
            }
            print out
        }

        sep = ""
        for (c = 1; c <= maxc; c++)
            sep = sep (c == 1 ? "" : "-+-") rep("-", colw[c])
        print sep
    }
}
