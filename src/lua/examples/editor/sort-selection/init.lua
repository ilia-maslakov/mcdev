local function split_lines(text)
    local trailing_newline = text:sub(-1) == "\n"
    local lines = {}
    for line in (text .. "\n"):gmatch("(.-)\n") do lines[#lines + 1] = line end
    if trailing_newline then table.remove(lines) end
    return lines, trailing_newline
end

mc.macro {
    id = "sort-selection",
    area = "editor",
    key = "Alt-t",
    description = "Sort selected lines",
    menu = {
        path = "Format",
        label = "Sort...",
        position = 40,
    },
    action = function(ev)
        local selection, selection_error = ev.editor:selection()
        if selection == nil or selection.kind == "none" then
            mc.ui.message("Sort", selection_error or "Select lines to sort first.")
            return mc.CONSUME
        end
        if selection.kind ~= "linear" or selection.text == nil then
            mc.ui.message("Sort", "A non-truncated linear selection is required.")
            return mc.CONSUME
        end

        local result, err = mc.ui.dialog {
            title = "Sort selection", width = 52,
            controls = {
                { id = "mode", type = "select", label = "Comparison:", value = "text",
                  options = {
                      { id = "text", label = "Text" },
                      { id = "number", label = "Numeric" },
                  } },
                { id = "case", type = "checkbox", label = "Ignore case", value = false },
                { id = "descending", type = "checkbox", label = "Descending", value = false },
                { id = "unique", type = "checkbox", label = "Remove duplicate lines", value = false },
                { type = "hbox", expand_x = true, controls = {
                    { type = "spacer", expand_x = true },
                    { id = "sort", type = "button", label = "&Sort", default = true },
                    { id = "cancel", type = "button", label = "&Cancel", cancel = true },
                } },
            },
        }
        if result == nil then
            if err ~= "cancelled" then mc.ui.message("Sort", err) end
            return mc.CONSUME
        end

        local lines, trailing_newline = split_lines(selection.text)
        local function key(line)
            if result.values.mode == "number" then return tonumber(line) or math.huge end
            return result.values.case and string.lower(line) or line
        end
        table.sort(lines, function(a, b)
            local ka, kb = key(a), key(b)
            if ka == kb then return a < b end
            if result.values.descending then return ka > kb end
            return ka < kb
        end)
        if result.values.unique then
            local unique = {}
            for _, line in ipairs(lines) do
                if #unique == 0 or line ~= unique[#unique] then unique[#unique + 1] = line end
            end
            lines = unique
        end

        local output = table.concat(lines, "\n") .. (trailing_newline and "\n" or "")
        local ok, replace_error = ev.editor:replace_selection(output)
        if not ok then mc.ui.message("Sort", replace_error) end
        return mc.CONSUME
    end,
}
