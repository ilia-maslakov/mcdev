local function insert_output(editor, output, destination)
    if destination == "insert" then return editor:insert(output) end

    local selection, selection_error = editor:selection()
    if selection == nil then return nil, selection_error end
    if selection.kind == "none" then return editor:insert(output) end
    return editor:replace_selection(output)
end

mc.macro {
    id = "insert-command-output",
    area = "editor",
    key = "Alt-u",
    description = "Insert command output",
    menu = {
        path = "Format",
        label = "Insert command output...",
        position = 50,
    },
    action = function(ev)
        local dialog, dialog_error = mc.ui.dialog {
            title = "Insert command output",
            width = 68,
            controls = {
                { type = "label", text = "Shell command:" },
                { id = "command", type = "input", value = "", width = 56,
                  history = "lua-insert-command-output",
                  complete_on_tab = true,
                  completion = {
                      "commands", "files", "variables", "users", "hosts", "cd", "shell",
                  } },
                { type = "label", text = "Tab: complete   Alt-H: history" },
                { id = "destination", type = "select", label = "Destination:", value = "smart",
                  options = {
                      { id = "smart", label = "Replace selection, otherwise insert" },
                      { id = "insert", label = "Always insert at cursor" },
                  } },
                { type = "hbox", expand_x = true, controls = {
                    { type = "spacer", expand_x = true },
                    { id = "run", type = "button", label = "&Run and insert", default = true },
                    { id = "cancel", type = "button", label = "&Cancel", cancel = true },
                } },
            },
        }
        if dialog == nil then
            if dialog_error ~= "cancelled" then
                mc.ui.message("Insert command output", dialog_error)
            end
            return mc.CONSUME
        end

        local command = dialog.values.command:match("^%s*(.-)%s*$")
        if command == "" then
            mc.ui.message("Insert command output", "Enter a shell command.")
            return mc.CONSUME
        end

        local result, process_error = mc.process.run {
            command = command,
            max_output = 8 * 1024 * 1024,
        }
        if result == nil then
            mc.ui.message("Insert command output", process_error)
            return mc.CONSUME
        end
        if result.stdout_truncated or result.stderr_truncated then
            mc.ui.message("Insert command output", "Command output exceeded the 8 MiB limit.")
            return mc.CONSUME
        end
        if result.exit_code ~= 0 then
            local reason = result.stderr ~= "" and result.stderr
                or (result.signal and ("Terminated by signal " .. result.signal)
                    or ("Command exited with status " .. tostring(result.exit_code)))
            mc.ui.message("Command failed", reason)
            return mc.CONSUME
        end

        local edit_result, insert_error = insert_output(
            ev.editor, result.stdout, dialog.values.destination
        )
        if edit_result == nil then mc.ui.message("Insert command output", insert_error) end
        return mc.CONSUME
    end,
}
