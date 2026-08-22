local function decode(value)
    local code = value:match("^U%+([0-9A-Fa-f]+)$") or value:match("^0x([0-9A-Fa-f]+)$")

    if code ~= nil then
        code = tonumber(code, 16)
        if code == nil or code > 0x10ffff or (code >= 0xd800 and code <= 0xdfff) then
            return nil, "Invalid Unicode code point."
        end
        return utf8.char(code)
    end

    local escapes = { ["\\n"] = "\n", ["\\r"] = "\r", ["\\t"] = "\t",
                      ["\\\\"] = "\\" }
    return escapes[value] or value
end

local function insert_or_replace(editor, value)
    local selection, selection_error = editor:selection()

    if selection == nil then return nil, selection_error end
    if selection.kind == "none" then return editor:insert(value) end
    return editor:replace_selection(value)
end

mc.macro {
    id = "insert-literal",
    area = "editor",
    key = "Ctrl-q",
    description = "Insert a literal character or Unicode code point",
    menu = {
        path = "Format",
        label = "Insert literal...",
        position = 10,
    },
    action = function(ev)
        local result, err = mc.ui.dialog {
            title = "Insert literal",
            width = 52,
            controls = {
                { type = "label", text = "Character, escape (\\n) or code point (U+2026):" },
                { id = "value", type = "input", value = "", width = 24 },
                { type = "hbox", expand_x = true, controls = {
                    { type = "spacer", expand_x = true },
                    { id = "insert", type = "button", label = "&Insert", default = true },
                    { id = "cancel", type = "button", label = "&Cancel", cancel = true },
                } },
            },
        }
        if result == nil then
            if err ~= "cancelled" then mc.ui.message("Insert literal", err) end
            return mc.CONSUME
        end

        local value, decode_error = decode(result.values.value)
        if value == nil then
            mc.ui.message("Insert literal", decode_error)
        else
            local ok, insert_error = insert_or_replace(ev.editor, value)
            if not ok then mc.ui.message("Insert literal", insert_error) end
        end
        return mc.CONSUME
    end,
}
