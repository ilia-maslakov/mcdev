# markdown_common.awk -- shared nroff-style formatting helpers
#
# boldify(s)  -- make text bold via ch\bch overstriking
# trim(s)     -- strip leading/trailing whitespace
# rep(ch, n)  -- repeat character ch n times

function boldify(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == " ")
            out = out ch
        else
            out = out ch "\b" ch
    }
    return out
}

function trim(s) {
    sub(/^[[:space:]]+/, "", s)
    sub(/[[:space:]]+$/, "", s)
    return s
}

function rep(ch, n,    i, out) {
    out = ""
    for (i = 0; i < n; i++)
        out = out ch
    return out
}
