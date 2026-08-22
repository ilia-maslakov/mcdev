local function width(value)
    return utf8.len(value) or #value
end

local function indentation(line)
    return line:match("^([ \t]*)") or ""
end

local function split_lines(text)
    local lines = {}
    local start = 1

    while true do
        local newline = text:find("\n", start, true)
        if newline == nil then
            lines[#lines + 1] = {
                text = text:sub(start),
                from = start - 1,
                to = #text,
            }
            break
        end
        lines[#lines + 1] = {
            text = text:sub(start, newline - 1),
            from = start - 1,
            to = newline - 1,
        }
        start = newline + 1
    end
    return lines
end

local function current_paragraph(editor, selection)
    local info, info_error = editor:info()
    if info == nil then return nil, info_error end

    local document = ""
    if info.byte_length > 0 then
        document, info_error = editor:text {
            from = 0, to = info.byte_length, revision = info.revision,
        }
        if document == nil then return nil, info_error end
    end

    local lines = split_lines(document)
    local cursor_offset = selection.cursor.offset
    local current = #lines
    for index, line in ipairs(lines) do
        if cursor_offset <= line.to then
            current = index
            break
        end
    end
    if lines[current].text:match("^%s*$") then
        return nil, "Place the cursor inside a paragraph or select text."
    end

    local first, last = current, current
    while first > 1 and not lines[first - 1].text:match("^%s*$") do first = first - 1 end
    while last < #lines and not lines[last + 1].text:match("^%s*$") do last = last + 1 end

    local paragraph = {}
    for index = first, last do paragraph[#paragraph + 1] = lines[index].text end
    return {
        text = table.concat(paragraph, "\n"),
        from = lines[first].from,
        to = lines[last].to,
        revision = info.revision,
        selected = false,
    }
end

local function formatting_scope(editor)
    local selection, selection_error = editor:selection()
    if selection == nil then return nil, selection_error end
    if selection.kind == "none" then return current_paragraph(editor, selection) end
    if selection.kind ~= "linear" or selection.text == nil then
        return nil, "A non-truncated linear selection is required."
    end
    return {
        text = selection.text,
        revision = selection.revision,
        selected = true,
    }
end

local function parse_integer(value, minimum, maximum, label)
    local number = tonumber(value)
    if number == nil or number % 1 ~= 0 or number < minimum or number > maximum then
        return nil, string.format("%s must be an integer from %d to %d.", label, minimum, maximum)
    end
    return number
end

local function justify(words, available)
    if #words < 2 then return table.concat(words, " ") end

    local letters = 0
    for _, word in ipairs(words) do letters = letters + width(word) end
    local spaces = math.max(available - letters, #words - 1)
    local base = math.floor(spaces / (#words - 1))
    local extra = spaces % (#words - 1)
    local result = { words[1] }
    for index = 2, #words do
        local count = base + ((index - 1) <= extra and 1 or 0)
        result[#result + 1] = string.rep(" ", count) .. words[index]
    end
    return table.concat(result)
end

local function reflow(text, target_width, alignment, first_indent, body_indent)
    local words = {}
    local trailing_newline = text:sub(-1) == "\n"
    for word in text:gmatch("%S+") do words[#words + 1] = word end
    if #words == 0 then return text end

    local rows = {}
    local current = {}
    local current_width = 0
    local indent = first_indent

    for _, word in ipairs(words) do
        local word_width = width(word)
        local required = current_width + (#current > 0 and 1 or 0) + word_width
        local available = math.max(target_width - width(indent), 1)
        if #current > 0 and required > available then
            rows[#rows + 1] = { words = current, indent = indent, available = available }
            current = { word }
            current_width = word_width
            indent = body_indent
        else
            current[#current + 1] = word
            current_width = required
        end
    end
    rows[#rows + 1] = {
        words = current,
        indent = indent,
        available = math.max(target_width - width(indent), 1),
    }

    local output = {}
    for index, row in ipairs(rows) do
        local contents
        if alignment == "justify" and index < #rows then
            contents = justify(row.words, row.available)
        else
            contents = table.concat(row.words, " ")
        end
        output[index] = row.indent .. contents
    end
    return table.concat(output, "\n") .. (trailing_newline and "\n" or "")
end

mc.macro {
    id = "format-paragraph",
    area = "editor",
    key = "Alt-p",
    description = "Format paragraph",
    menu = {
        path = "Format",
        label = "Format paragraph",
        position = 30,
    },
    action = function(ev)
        local scope, scope_error = formatting_scope(ev.editor)
        if scope == nil then
            mc.ui.message("Format paragraph", scope_error)
            return mc.CONSUME
        end

        local source_lines = split_lines(scope.text)
        local preserved_first = indentation(source_lines[1].text)
        local preserved_body = #source_lines > 1 and indentation(source_lines[2].text)
            or preserved_first

        local result, dialog_error = mc.ui.dialog {
            title = "Format paragraph",
            width = 58,
            controls = {
                { type = "label", text = "Target line width:" },
                { id = "width", type = "input", value = "72", width = 8 },
                { id = "alignment", type = "select", label = "Alignment:", value = "left",
                  options = {
                      { id = "left", label = "Left" },
                      { id = "justify", label = "Justified" },
                  } },
                { id = "indentation", type = "select", label = "Indentation:", value = "preserve",
                  options = {
                      { id = "preserve", label = "Preserve existing" },
                      { id = "none", label = "Remove indentation" },
                      { id = "custom", label = "Custom spaces" },
                  } },
                { id = "first_indent", type = "input", value = "0", width = 8 },
                { type = "label", text = "Custom first-line indent" },
                { id = "body_indent", type = "input", value = "0", width = 8 },
                { type = "label", text = "Custom following-line indent" },
                { type = "hbox", expand_x = true, controls = {
                    { type = "spacer", expand_x = true },
                    { id = "format", type = "button", label = "&Format", default = true },
                    { id = "cancel", type = "button", label = "&Cancel", cancel = true },
                } },
            },
        }
        if result == nil then
            if dialog_error ~= "cancelled" then mc.ui.message("Format paragraph", dialog_error) end
            return mc.CONSUME
        end

        local target_width, value_error = parse_integer(result.values.width, 10, 1000, "Width")
        if target_width == nil then
            mc.ui.message("Format paragraph", value_error)
            return mc.CONSUME
        end

        local first_indent, body_indent = preserved_first, preserved_body
        if result.values.indentation == "none" then
            first_indent, body_indent = "", ""
        elseif result.values.indentation == "custom" then
            local first_count
            first_count, value_error = parse_integer(
                result.values.first_indent, 0, target_width - 1, "First-line indent"
            )
            if first_count == nil then
                mc.ui.message("Format paragraph", value_error)
                return mc.CONSUME
            end
            local body_count
            body_count, value_error = parse_integer(
                result.values.body_indent, 0, target_width - 1, "Following-line indent"
            )
            if body_count == nil then
                mc.ui.message("Format paragraph", value_error)
                return mc.CONSUME
            end
            first_indent = string.rep(" ", first_count)
            body_indent = string.rep(" ", body_count)
        end

        local output = reflow(
            scope.text, target_width, result.values.alignment, first_indent, body_indent
        )
        local edit_result, replace_error
        if scope.selected then
            edit_result, replace_error = ev.editor:replace_selection(output)
        else
            edit_result, replace_error = ev.editor:replace({
                from = scope.from, to = scope.to, revision = scope.revision,
            }, output)
        end
        if edit_result == nil then mc.ui.message("Format paragraph", replace_error) end
        return mc.CONSUME
    end,
}
