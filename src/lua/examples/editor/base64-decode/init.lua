-- Select text in mcedit and press F11 to encode or decode it as Base64.

local alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local value = {}

for index = 1, #alphabet do
    value[alphabet:sub(index, index)] = index - 1
end

local function decode_base64(input)
    local compact = input:gsub("%s+", ""):gsub("-", "+"):gsub("_", "/")
    local raw = compact:gsub("=", "")
    local padding = #compact - #raw
    local expected_padding
    local result = {}

    if compact == "" then
        return nil, "The selection is empty."
    end
    if compact:find("[^A-Za-z0-9+/=]")
        or not compact:match("^[A-Za-z0-9+/]*=*$")
        or padding > 2 then
        return nil, "The selection is not valid Base64."
    end
    if #raw % 4 == 1 then
        return nil, "The selection has an invalid Base64 length."
    end

    expected_padding = (4 - (#raw % 4)) % 4
    if padding ~= 0 and padding ~= expected_padding then
        return nil, "The Base64 padding is invalid."
    end

    for pos = 1, #raw, 4 do
        local a = value[raw:sub(pos, pos)]
        local b = value[raw:sub(pos + 1, pos + 1)]
        local c = value[raw:sub(pos + 2, pos + 2)]
        local d = value[raw:sub(pos + 3, pos + 3)]
        local bits

        if a == nil or b == nil then
            return nil, "The selection is not valid Base64."
        end

        bits = (((a * 64 + b) * 64 + (c or 0)) * 64 + (d or 0))
        result[#result + 1] = string.char((bits >> 16) & 0xff)
        if c ~= nil then
            result[#result + 1] = string.char((bits >> 8) & 0xff)
        end
        if d ~= nil then
            result[#result + 1] = string.char(bits & 0xff)
        end
    end

    result = table.concat(result)
    if result:find("\0", 1, true) or utf8.len(result) == nil then
        return nil, "The decoded data is not UTF-8 text."
    end

    return result
end

local function encode_base64(input, url_safe, omit_padding, line_width)
    local result = {}

    for pos = 1, #input, 3 do
        local a = input:byte(pos)
        local b = input:byte(pos + 1)
        local c = input:byte(pos + 2)
        local bits = ((a or 0) << 16) | ((b or 0) << 8) | (c or 0)

        result[#result + 1] = alphabet:sub(((bits >> 18) & 0x3f) + 1,
                                           ((bits >> 18) & 0x3f) + 1)
        result[#result + 1] = alphabet:sub(((bits >> 12) & 0x3f) + 1,
                                           ((bits >> 12) & 0x3f) + 1)
        result[#result + 1] = b ~= nil
            and alphabet:sub(((bits >> 6) & 0x3f) + 1, ((bits >> 6) & 0x3f) + 1)
            or "="
        result[#result + 1] = c ~= nil
            and alphabet:sub((bits & 0x3f) + 1, (bits & 0x3f) + 1)
            or "="
    end

    result = table.concat(result)
    if url_safe then
        result = result:gsub("%+", "-"):gsub("/", "_")
    end
    if omit_padding then
        result = result:gsub("=+$", "")
    end
    if line_width > 0 then
        local lines = {}
        for pos = 1, #result, line_width do
            lines[#lines + 1] = result:sub(pos, pos + line_width - 1)
        end
        result = table.concat(lines, "\n")
    end

    return result
end

local function insert_result(editor, result)
    local selection, selection_error = editor:selection()
    local ok, insert_error

    if selection == nil then
        ok, insert_error = nil, selection_error
    elseif selection.kind == "none" then
        ok, insert_error = editor:insert(result)
    else
        ok, insert_error = editor:replace_selection(result)
    end

    if not ok then
        mc.ui.message("Base64", "Cannot insert result: " .. insert_error)
    end
end

local function show_result_dialog(editor, output)
    local result, dialog_error = mc.ui.dialog {
        title = "Base64 result",
        width = 64,
        controls = {
            {
                type = "label",
                text = output,
                expand_x = true,
            },
            {
                type = "hbox",
                expand_x = true,
                controls = {
                    {
                        type = "spacer",
                        expand_x = true,
                    },
                    {
                        id = "insert",
                        type = "button",
                        label = "&Insert",
                        default = true,
                    },
                    {
                        id = "close",
                        type = "button",
                        label = "&Close",
                        cancel = true,
                    },
                },
            },
        },
    }

    if result == nil then
        if dialog_error ~= "cancelled" then
            mc.ui.message("Base64", "Cannot open dialog: " .. dialog_error)
        end
        return
    end

    if result.button == "insert" then
        insert_result(editor, output)
    end
end

local function show_options_dialog(editor, selected)
    local result, dialog_error = mc.ui.dialog {
        title = "Base64 tools",
        width = 72,
        controls = {
            {
                type = "label",
                text = string.format("Selected text: %d bytes", #selected),
            },
            {
                type = "separator",
                label = "Operation",
            },
            {
                id = "operation",
                type = "select",
                label = "What should be done?",
                value = "decode",
                options = {
                    { id = "decode", label = "&Decode Base64 to UTF-8" },
                    { id = "encode", label = "&Encode text as Base64" },
                },
            },
            {
                id = "alphabet",
                type = "select",
                label = "Encoding alphabet:",
                value = "standard",
                options = {
                    { id = "standard", label = "&Standard (+ and /)" },
                    { id = "url", label = "&URL-safe (- and _)" },
                },
            },
            {
                id = "line_width",
                type = "input",
                value = "0",
                width = 8,
            },
            {
                type = "label",
                text = "Line width (0 means no wrapping)",
            },
            {
                id = "omit_padding",
                type = "checkbox",
                label = "Omit trailing '=' padding when encoding",
                value = false,
            },
            {
                id = "insert_immediately",
                type = "checkbox",
                label = "Insert the result immediately",
                value = false,
            },
            {
                type = "hbox",
                expand_x = true,
                controls = {
                    { type = "spacer", expand_x = true },
                    {
                        id = "run",
                        type = "button",
                        label = "&Run",
                        default = true,
                    },
                    {
                        id = "cancel",
                        type = "button",
                        label = "&Cancel",
                        cancel = true,
                    },
                },
            },
        },
    }

    if result == nil then
        if dialog_error ~= "cancelled" then
            mc.ui.message("Base64", "Cannot open dialog: " .. dialog_error)
        end
        return
    end

    local line_width = tonumber(result.values.line_width)
    if line_width == nil or line_width % 1 ~= 0 or line_width < 0 or line_width > 512 then
        mc.ui.message("Base64", "Line width must be an integer between 0 and 512.")
        return
    end

    local output, operation_error
    if result.values.operation == "encode" then
        output = encode_base64(
            selected,
            result.values.alphabet == "url",
            result.values.omit_padding,
            line_width
        )
    else
        output, operation_error = decode_base64(selected)
    end

    if output == nil then
        mc.ui.message("Base64", operation_error)
    elseif result.values.insert_immediately then
        insert_result(editor, output)
    else
        show_result_dialog(editor, output)
    end
end

mc.macro {
    id = "decode-selection",
    area = "editor",
    key = "F11",
    description = "Encode or decode Base64 selection",
    action = function(ev)
        local selected, selection_error = ev.editor:selected_text()

        if selected == nil then
            mc.ui.message("Base64", selection_error == "no_selection"
                and "Select Base64 text first."
                or "A normal text selection is required.")
            return mc.CONSUME
        end

        show_options_dialog(ev.editor, selected)

        return mc.CONSUME
    end,
}
