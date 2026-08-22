local function insert_or_replace(editor, value)
    local selection, selection_error = editor:selection()

    if selection == nil then return nil, selection_error end
    if selection.kind == "none" then return editor:insert(value) end
    return editor:replace_selection(value)
end

mc.macro {
    id = "insert-datetime",
    area = "editor",
    key = "Ctrl-d",
    description = "Insert formatted date and time",
    menu = {
        path = "Format",
        label = "Insert date/time",
        position = 20,
    },
    action = function(ev)
        local result, err = mc.ui.dialog {
            title = "Insert date/time",
            width = 58,
            controls = {
                { id = "format", type = "select", label = "Format:", value = "local",
                  options = {
                      { id = "local", label = "Local date and time" },
                      { id = "iso", label = "ISO 8601" },
                      { id = "date", label = "Date only" },
                      { id = "time", label = "Time only" },
                      { id = "custom", label = "Custom strftime format" },
                  } },
                { id = "custom", type = "input", value = "%Y-%m-%d %H:%M:%S", width = 32 },
                { type = "hbox", expand_x = true, controls = {
                    { type = "spacer", expand_x = true },
                    { id = "insert", type = "button", label = "&Insert", default = true },
                    { id = "cancel", type = "button", label = "&Cancel", cancel = true },
                } },
            },
        }
        if result == nil then
            if err ~= "cancelled" then mc.ui.message("Insert date/time", err) end
            return mc.CONSUME
        end

        local formats = { ["local"] = "%c", iso = "%Y-%m-%dT%H:%M:%S%z",
                          date = "%Y-%m-%d", time = "%H:%M:%S" }
        local format = formats[result.values.format] or result.values.custom
        local value = os.date(format)
        local ok, insert_error = insert_or_replace(ev.editor, value)
        if not ok then mc.ui.message("Insert date/time", insert_error) end
        return mc.CONSUME
    end,
}
