# markdown_render.awk -- convert markdown to nroff-style view
#
# Requires: markdown_common.awk (boldify)
#
# - Headings (# ...) rendered as bold
# - Inline `code` rendered as underlined
# - Inline $LaTeX$ rendered with Unicode symbol substitution
# - Tables delimited by __MC_TABLE_BEGIN__/__MC_TABLE_END__ markers
#   for post-processing by markdown_table.awk

BEGIN {
    # Greek letters
    latex["alpha"]   = "α"; latex["beta"]    = "β"
    latex["gamma"]   = "γ"; latex["delta"]   = "δ"
    latex["epsilon"] = "ε"; latex["zeta"]    = "ζ"
    latex["eta"]     = "η"; latex["theta"]   = "θ"
    latex["iota"]    = "ι"; latex["kappa"]   = "κ"
    latex["lambda"]  = "λ"; latex["mu"]      = "μ"
    latex["nu"]      = "ν"; latex["xi"]      = "ξ"
    latex["pi"]      = "π"; latex["rho"]     = "ρ"
    latex["sigma"]   = "σ"; latex["tau"]     = "τ"
    latex["upsilon"] = "υ"; latex["phi"]     = "φ"
    latex["chi"]     = "χ"; latex["psi"]     = "ψ"
    latex["omega"]   = "ω"
    # Uppercase Greek
    latex["Gamma"]   = "Γ"; latex["Delta"]   = "Δ"
    latex["Theta"]   = "Θ"; latex["Lambda"]  = "Λ"
    latex["Pi"]      = "Π"; latex["Sigma"]   = "Σ"
    latex["Phi"]     = "Φ"; latex["Psi"]     = "Ψ"
    latex["Omega"]   = "Ω"
    # Math operators
    latex["sum"]     = "∑"; latex["prod"]    = "∏"
    latex["int"]     = "∫"; latex["infty"]   = "∞"
    latex["partial"] = "∂"; latex["nabla"]   = "∇"
    latex["pm"]      = "±"; latex["times"]   = "×"
    latex["div"]     = "÷"; latex["cdot"]    = "·"
    # Relations
    latex["leq"]     = "≤"; latex["geq"]     = "≥"
    latex["neq"]     = "≠"; latex["approx"]  = "≈"
    latex["equiv"]   = "≡"; latex["subset"]  = "⊂"
    latex["supset"]  = "⊃"; latex["in"]      = "∈"
    latex["forall"]  = "∀"; latex["exists"]  = "∃"
    # Arrows
    latex["to"]         = "→"; latex["leftarrow"]  = "←"
    latex["rightarrow"] = "→"; latex["Rightarrow"] = "⇒"
    latex["Leftarrow"]  = "⇐"
    # Misc
    latex["sqrt"] = "√"; latex["lim"] = "lim"

    # Subscript digits
    _sub[0] = "₀"; _sub[1] = "₁"; _sub[2] = "₂"
    _sub[3] = "₃"; _sub[4] = "₄"; _sub[5] = "₅"
    _sub[6] = "₆"; _sub[7] = "₇"; _sub[8] = "₈"
    _sub[9] = "₉"
    # Superscript digits
    _sup[0] = "⁰"; _sup[1] = "¹"; _sup[2] = "²"
    _sup[3] = "³"; _sup[4] = "⁴"; _sup[5] = "⁵"
    _sup[6] = "⁶"; _sup[7] = "⁷"; _sup[8] = "⁸"
    _sup[9] = "⁹"
}

# Replace \command with Unicode symbol
function latex_replace_commands(s,    out, cmd, rest, i, ch) {
    out = ""
    while (match(s, /\\[a-zA-Z]+/)) {
        out = out substr(s, 1, RSTART - 1)
        cmd = substr(s, RSTART + 1, RLENGTH - 1)
        s = substr(s, RSTART + RLENGTH)
        if (cmd in latex)
            out = out latex[cmd]
        else
            out = out "\\" cmd
    }
    return out s
}

# Extract balanced {}-group starting at position pos in s.
# Returns content between braces (without outer braces).
# Sets global _brace_end to position after closing '}'.
function extract_brace(s, pos,    depth, i, ch, content) {
    if (substr(s, pos, 1) != "{")
        return ""
    depth = 0
    for (i = pos; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == "{") depth++
        else if (ch == "}") {
            depth--
            if (depth == 0) {
                _brace_end = i + 1
                return substr(s, pos + 1, i - pos - 1)
            }
        }
    }
    _brace_end = length(s) + 1
    return substr(s, pos + 1)
}

# Replace \frac{a}{b} with (a)/(b), handles nested braces
function latex_replace_frac(s,    out, num, den, pos) {
    while (match(s, /\\frac\{/)) {
        out = out substr(s, 1, RSTART - 1)
        pos = RSTART + 5  # position of '{'
        num = extract_brace(s, pos)
        pos = _brace_end
        den = extract_brace(s, pos)
        s = substr(s, _brace_end)
        # Skip parens for simple single-word content
        if (num ~ /^[a-zA-Z0-9]+$/)
            out = out num
        else
            out = out "(" num ")"
        out = out "/"
        if (den ~ /^[a-zA-Z0-9]+$/)
            out = out den
        else
            out = out "(" den ")"
    }
    return out s
}

# Replace \sqrt{x} with sqrt(x), \sqrt[n]{x} with n-root(x)
function latex_replace_sqrt(s,    out, sym, idx, body, pos) {
    sym = latex["sqrt"]
    while (match(s, /\\sqrt[\[{]/)) {
        out = out substr(s, 1, RSTART - 1)
        pos = RSTART + 5  # after "\sqrt"
        if (substr(s, pos, 1) == "[") {
            # \sqrt[n]{x}
            match(substr(s, pos), /\[[^\]]*\]/)
            idx = substr(s, pos + 1, RLENGTH - 2)
            pos = pos + RLENGTH
            body = extract_brace(s, pos)
            s = substr(s, _brace_end)
            out = out idx sym "(" body ")"
        } else {
            # \sqrt{x}
            body = extract_brace(s, pos)
            s = substr(s, _brace_end)
            out = out sym "(" body ")"
        }
    }
    return out s
}

# Replace _{...} subscript and ^{...} superscript with Unicode where possible
function latex_replace_scripts(s,    d) {
    for (d = 0; d <= 9; d++) {
        gsub("_\\{" d "\\}", _sub[d], s)
        gsub("\\^\\{" d "\\}", _sup[d], s)
    }
    for (d = 0; d <= 9; d++) {
        gsub("_" d, _sub[d], s)
        gsub("\\^" d, _sup[d], s)
    }
    # Multi-char subscript/superscript: _{xy} -> _(xy), ^{xy} -> ^(xy)
    while (match(s, /[_^]{/)) {
        prefix = substr(s, 1, RSTART)
        pos = RSTART + 2
        body = extract_brace(s, RSTART + 1)
        s = prefix "(" body ")" substr(s, _brace_end)
    }
    return s
}

# Remove remaining braces (e.g. from {x} grouping)
function latex_strip_braces(s) {
    gsub(/[{}]/, "", s)
    return s
}

# Process $...$ inline math
function replace_latex(s,    out, pre, math, rest, pos) {
    out = ""
    while (match(s, /\$[^$]+\$/)) {
        out = out substr(s, 1, RSTART - 1)
        math = substr(s, RSTART + 1, RLENGTH - 2)
        s = substr(s, RSTART + RLENGTH)
        math = latex_replace_frac(math)
        math = latex_replace_sqrt(math)
        math = latex_replace_commands(math)
        math = latex_replace_scripts(math)
        math = latex_strip_braces(math)
        out = out math
    }
    return out s
}

function is_table_sep(s) {
    return (s ~ /^[[:space:]]*\|?[[:space:]]*:?-+:?[[:space:]]*(\|[[:space:]]*:?-+:?[[:space:]]*)+\|?[[:space:]]*$/)
}

# Apply nroff bold to a string: ch\bch for each non-space char
function nroff_bold(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == " ") out = out ch
        else out = out ch "\b" ch
    }
    return out
}

# Apply nroff heading to a string: ch\bch\bch for each non-space char
function nroff_heading(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == " ") out = out ch
        else out = out ch "\b" ch "\b" ch
    }
    return out
}

# Apply nroff bold+underline to a string: _\bch\bch for each non-space char
function nroff_bold_underline(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == " ") out = out ch
        else out = out "_\b" ch "\b" ch
    }
    return out
}

# Apply nroff underline to a string: _\bch for each non-space char
function nroff_underline(s,    out, i, ch) {
    out = ""
    for (i = 1; i <= length(s); i++) {
        ch = substr(s, i, 1)
        if (ch == " ") out = out ch
        else out = out "_\b" ch
    }
    return out
}

# Apply nroff style to plain text: 0=none, 1=bold, 2=italic, 3=bold+italic
function apply_style(s, style) {
    if (style == 1) return nroff_bold(s)
    if (style == 2) return nroff_underline(s)
    if (style == 3) return nroff_bold_underline(s)
    return s
}

# Format inline markdown: **bold**, __bold__, *italic*, _italic_, `code`
# style: 0=none, 1=bold, 2=italic, 3=bold+italic (accumulated from parent)
function format_inline(s, style,    out, best_s, best_e, best_t, ms, me, inner, new_style) {
    out = ""
    while (length(s) > 0) {
        best_s = 0; best_e = 0; best_t = ""

        # Find earliest match among all patterns
        if (match(s, /`[^`]+`/)) {
            best_s = RSTART; best_e = RSTART + RLENGTH; best_t = "code"
        }
        if (match(s, /\*\*[^*]+\*\*/)) {
            if (best_s == 0 || RSTART < best_s) {
                best_s = RSTART; best_e = RSTART + RLENGTH; best_t = "bold2"
            }
        }
        if (match(s, /__[^_]+__/)) {
            if (best_s == 0 || RSTART < best_s) {
                best_s = RSTART; best_e = RSTART + RLENGTH; best_t = "bold_"
            }
        }
        if (match(s, /\*[^*]+\*/)) {
            ms = RSTART; me = RSTART + RLENGTH
            if (substr(s, ms, 2) != "**") {
                if (best_s == 0 || ms < best_s) {
                    best_s = ms; best_e = me; best_t = "ital2"
                }
            }
        }
        if (match(s, /_[^_]+_/)) {
            ms = RSTART; me = RSTART + RLENGTH
            if (substr(s, ms, 2) != "__") {
                if (best_s == 0 || ms < best_s) {
                    best_s = ms; best_e = me; best_t = "ital_"
                }
            }
        }

        if (best_s == 0) break

        # Apply current style to plain text before match
        out = out apply_style(substr(s, 1, best_s - 1), style)

        if (best_t == "code") {
            inner = substr(s, best_s + 1, best_e - best_s - 2)
            out = out nroff_underline(inner)
        } else if (best_t == "bold2" || best_t == "bold_") {
            inner = substr(s, best_s + 2, best_e - best_s - 4)
            # Add bold: set bit 1
            new_style = (style >= 2) ? 3 : 1
            out = out format_inline(inner, new_style)
        } else if (best_t == "ital2" || best_t == "ital_") {
            inner = substr(s, best_s + 1, best_e - best_s - 2)
            # Add italic: set bit 2
            new_style = (style == 1 || style == 3) ? 3 : 2
            out = out format_inline(inner, new_style)
        }
        s = substr(s, best_e)
    }
    return out apply_style(s, style)
}

# collect all lines first so we can look ahead for table separators
{ lines[++n] = $0 }

END {
    # Join multi-line display math: $ ... $ on separate lines -> single line
    nn = 0
    for (i = 1; i <= n; i++) {
        if (trim(lines[i]) == "$" && !in_math) {
            in_math = 1
            math_buf = "$"
            continue
        }
        if (in_math) {
            if (trim(lines[i]) == "$") {
                in_math = 0
                lines2[++nn] = math_buf " $"
            } else {
                math_buf = math_buf " " trim(lines[i])
            }
            continue
        }
        lines2[++nn] = lines[i]
    }
    # swap
    for (i = 1; i <= nn; i++)
        lines[i] = lines2[i]
    n = nn

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

        line = replace_latex(lines[i])
        if (match(line, /^#+[ \t]*/)) {
            rest = substr(line, RLENGTH + 1)
            print nroff_heading(rest)
        } else {
            print format_inline(line)
        }
    }
}
