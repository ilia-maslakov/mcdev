mc.on("editor.save", function (ev)
    local previous = ev.previous_path or ev.path
    mc.log.info("Saved " .. ev.path .. " (previous: " .. previous .. ")")
end)
