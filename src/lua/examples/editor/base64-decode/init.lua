-- Select Base64 text in mcedit and press F11 to display its UTF-8 decoding.

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

mc.macro {
    id = "decode-selection",
    area = "editor",
    key = "F11",
    description = "Decode Base64 selection",
    action = function(ev)
        local selected, selection_error = ev.editor:selected_text()
        local decoded, decode_error

        if selected == nil then
            mc.ui.message("Base64", selection_error == "no_selection"
                and "Select Base64 text first."
                or "A normal text selection is required.")
            return mc.CONSUME
        end

        decoded, decode_error = decode_base64(selected)
        if decoded == nil then
            mc.ui.message("Base64", decode_error)
        else
            mc.ui.message("Base64 decoded", decoded)
        end

        return mc.CONSUME
    end,
}
