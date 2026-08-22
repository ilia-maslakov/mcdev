local active_style = nil
local indicator_id = "box-drawing"

local LEFT, RIGHT, UP, DOWN = 1, 2, 3, 4

local glyph_for, connections = {}, {}

local function state_key(state)
    return table.concat(state, ",")
end

local function define(glyph, left, right, up, down)
    local state = { left, right, up, down }
    glyph_for[state_key(state)] = glyph
    connections[glyph] = state
end

define(" ", 0, 0, 0, 0)

-- Single lines.
define("╴", 1, 0, 0, 0); define("╶", 0, 1, 0, 0)
define("╵", 0, 0, 1, 0); define("╷", 0, 0, 0, 1)
define("─", 1, 1, 0, 0); define("│", 0, 0, 1, 1)
define("┌", 0, 1, 0, 1); define("┐", 1, 0, 0, 1)
define("└", 0, 1, 1, 0); define("┘", 1, 0, 1, 0)
define("├", 0, 1, 1, 1); define("┤", 1, 0, 1, 1)
define("┬", 1, 1, 0, 1); define("┴", 1, 1, 1, 0)
define("┼", 1, 1, 1, 1)

-- Double lines. Heavy half-lines are used only while an endpoint is open.
define("╸", 2, 0, 0, 0); define("╺", 0, 2, 0, 0)
define("╹", 0, 0, 2, 0); define("╻", 0, 0, 0, 2)
define("═", 2, 2, 0, 0); define("║", 0, 0, 2, 2)
define("╔", 0, 2, 0, 2); define("╗", 2, 0, 0, 2)
define("╚", 0, 2, 2, 0); define("╝", 2, 0, 2, 0)
define("╠", 0, 2, 2, 2); define("╣", 2, 0, 2, 2)
define("╦", 2, 2, 0, 2); define("╩", 2, 2, 2, 0)
define("╬", 2, 2, 2, 2)

-- Mixed single/double junctions from the Unicode box-drawing block.
define("╒", 0, 2, 0, 1); define("╓", 0, 1, 0, 2)
define("╕", 2, 0, 0, 1); define("╖", 1, 0, 0, 2)
define("╘", 0, 2, 1, 0); define("╙", 0, 1, 2, 0)
define("╛", 2, 0, 1, 0); define("╜", 1, 0, 2, 0)
define("╞", 0, 2, 1, 1); define("╟", 0, 1, 2, 2)
define("╡", 2, 0, 1, 1); define("╢", 1, 0, 2, 2)
define("╤", 2, 2, 0, 1); define("╥", 1, 1, 0, 2)
define("╧", 2, 2, 1, 0); define("╨", 1, 1, 2, 0)
define("╪", 2, 2, 1, 1); define("╫", 1, 1, 2, 2)

local function render(state)
    local glyph = glyph_for[state_key(state)]
    if glyph ~= nil then return glyph end

    -- Unicode has no single/double glyph for different weights on opposite
    -- halves of one axis. Promote that axis to the stronger weight.
    local horizontal = math.max(state[LEFT], state[RIGHT])
    local vertical = math.max(state[UP], state[DOWN])
    local normalized = {
        state[LEFT] > 0 and horizontal or 0,
        state[RIGHT] > 0 and horizontal or 0,
        state[UP] > 0 and vertical or 0,
        state[DOWN] > 0 and vertical or 0,
    }
    return glyph_for[state_key(normalized)] or "┼"
end

local directions = {
    left = { line = 0, column = -1, here = LEFT, there = RIGHT },
    right = { line = 0, column = 1, here = RIGHT, there = LEFT },
    up = { line = -1, column = 0, here = UP, there = DOWN },
    down = { line = 1, column = 0, here = DOWN, there = UP },
}

local function split_document(text)
    local lines, starts = {}, {}
    local start = 1

    while true do
        starts[#starts + 1] = start - 1
        local newline = text:find("\n", start, true)
        if newline == nil then
            lines[#lines + 1] = text:sub(start)
            break
        end
        lines[#lines + 1] = text:sub(start, newline - 1)
        start = newline + 1
    end
    return lines, starts
end

local function characters(text, tab_width, through_column)
    local result = {}
    local visual_column = 1
    local failure
    local ok = pcall(function()
        for _, codepoint in utf8.codes(text) do
            local character = utf8.char(codepoint)
            if character == "\t" and visual_column <= through_column then
                local spaces = tab_width - ((visual_column - 1) % tab_width)
                for _ = 1, spaces do result[#result + 1] = " " end
                visual_column = visual_column + spaces
            else
                local character_width, width_error = mc.ui.text_width(character)
                if character_width == nil then
                    failure = width_error
                    error(width_error)
                elseif character_width == 0 and #result > 0 then
                    local index = #result
                    while index > 1 and result[index] == "" do index = index - 1 end
                    result[index] = result[index] .. character
                else
                    result[#result + 1] = character
                    for _ = 2, math.max(character_width, 1) do result[#result + 1] = "" end
                    visual_column = visual_column + character_width
                end
            end
        end
    end)
    return ok and result or nil, failure
end

local function ensure_column(line, column)
    while #line < column do line[#line + 1] = " " end
end

local function add_connection(line, column, connection, style)
    ensure_column(line, column)
    if line[column] == "" then return false end
    local previous = connections[line[column]] or connections[" "]
    local state = { previous[1], previous[2], previous[3], previous[4] }
    state[connection] = style == "double" and 2 or 1
    line[column] = render(state)
    return true
end

local function draw(editor, direction_name)
    if active_style == nil then return mc.PASS end

    local direction = directions[direction_name]
    local style = active_style
    local line, column = editor:cursor()
    if line == nil then
        mc.ui.status("Box drawing: cannot read cursor")
        return mc.CONSUME
    end
    local tab_width, tab_error = editor:tab_width()
    if tab_width == nil then
        mc.ui.status("Box drawing: " .. tab_error)
        return mc.CONSUME
    end

    local target_line = line + direction.line
    local target_column = column + direction.column
    if target_line < 1 or target_column < 1 then return mc.CONSUME end

    local info, info_error = editor:info()
    if info == nil then
        mc.ui.status("Box drawing: " .. info_error)
        return mc.CONSUME
    end

    local text = ""
    if info.byte_length > 0 then
        text, info_error = editor:text {
            from = 0, to = info.byte_length, revision = info.revision,
        }
        if text == nil then
            mc.ui.status("Box drawing: " .. info_error)
            return mc.CONSUME
        end
    end

    local source_lines, starts = split_document(text)
    local original_count = #source_lines
    while #source_lines < target_line do source_lines[#source_lines + 1] = "" end

    local first_line = math.min(line, target_line)
    local last_line = math.max(line, target_line)
    local through_column = math.max(column, target_column)
    local editable = {}
    for index = first_line, last_line do
        local character_error
        editable[index], character_error = characters(
            source_lines[index], tab_width, through_column
        )
        if editable[index] == nil then
            mc.ui.status("Box drawing: " .. (character_error or "invalid UTF-8 text"))
            return mc.CONSUME
        end
    end

    if not add_connection(editable[line], column, direction.here, style)
        or not add_connection(editable[target_line], target_column, direction.there, style) then
        mc.ui.status("Box drawing: cursor is inside a wide character")
        return mc.CONSUME
    end

    local replacement_lines = {}
    for index = first_line, last_line do
        replacement_lines[#replacement_lines + 1] = table.concat(editable[index])
    end

    local from = starts[first_line] or #text
    local to = last_line <= original_count
        and starts[last_line] + #source_lines[last_line]
        or #text
    local replacement = table.concat(replacement_lines, "\n")
    local edit_result, replace_error = editor:replace({
        from = from, to = to, revision = info.revision,
    }, replacement)
    if edit_result == nil then
        mc.ui.status("Box drawing: " .. replace_error)
        return mc.CONSUME
    end

    local ok, cursor_error = editor:set_cursor(target_line, target_column)
    if not ok then mc.ui.status("Box drawing: " .. cursor_error) end
    return mc.CONSUME
end

local function register_mode_action(id, description, menu_label, position, style)
    mc.macro {
        id = id,
        area = "editor",
        description = description,
        menu = {
            path = "Drawing",
            label = menu_label,
            position = position,
        },
        action = function()
            active_style = style
            if active_style == "single" then
                mc.ui.indicator {
                    id = indicator_id,
                    area = "editor",
                    text = "┌─┐",
                    priority = 100,
                }
            elseif active_style == "double" then
                mc.ui.indicator {
                    id = indicator_id,
                    area = "editor",
                    text = "╔═╗",
                    priority = 100,
                }
            else
                mc.ui.indicator_clear(indicator_id)
            end
            mc.ui.status(description)
            return mc.CONSUME
        end,
    }
end

register_mode_action(
    "box-drawing-single-enable", "Enable single box drawing", "Draw single line", 10,
    "single"
)
register_mode_action(
    "box-drawing-double-enable", "Enable double box drawing", "Draw double line", 20,
    "double"
)
register_mode_action(
    "box-drawing-stop", "Disable box drawing", "Stop line drawing", 30,
    nil
)

local bindings = {
    { "shift-left", "Shift-Left", "left" },
    { "shift-right", "Shift-Right", "right" },
    { "shift-up", "Shift-Up", "up" },
    { "shift-down", "Shift-Down", "down" },
}

for _, binding in ipairs(bindings) do
    local id, key, direction = table.unpack(binding)
    mc.macro {
        id = "box-drawing-" .. id,
        area = "editor",
        key = key,
        description = "Draw active box line " .. direction,
        listed = false,
        action = function(ev) return draw(ev.editor, direction) end,
    }
end
