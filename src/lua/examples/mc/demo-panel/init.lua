local text_viewer = assert(mc.viewer_source.define {
    id = "demo.text",
    open = function (identity)
        return { id = identity.id }
    end,
    prepare = function (session, params)
        return {
            source = mc.source.bytes("Lua viewer source for " .. session.id .. "\n"),
            title = "Lua entity: " .. session.id,
        }
    end,
    close = function (_) end,
})

local ok, err = mc.panel_provider.register {
    id = "lua-demo",
    title = "Lua demo panel",
    prefix = "lua-demo:",

    open = function (_, path)
        return { path = path ~= "" and path or "/", revision = 1 }
    end,

    close = function (_) end,

    connections = function ()
        return {
            { id = "root", title = "Lua demo: root", location = "/" },
            { id = "details", title = "Lua demo: details", location = "/details" },
        }
    end,

    actions = {
        {
            id = "refresh",
            title = "Refresh Lua demo",
            targets = "view",
            menu = { path = "Command", label = "Refresh Lua demo" },
        },
    },

    list = function (instance)
        local entries
        if instance.path == "/details" then
            entries = {
                { id = "detail:abi", name = "panel-abi.txt", kind = "file", size = 920 },
                { id = "detail:viewer", name = "viewer-source-abi.txt", kind = "file", size = 586 },
            }
        else
            entries = {
                { id = "section:details", name = "details", kind = "directory", role = "section" },
                { id = "entity:runtime", name = "lua-runtime", kind = "file", role = "runtime" },
            }
        end
        return {
            revision = instance.revision,
            location = instance.path,
            title = "Lua demo: " .. instance.path,
            footer = "Read-only Lua provider",
            entries = entries,
        }
    end,

    navigate = function (instance, request)
        if request.kind == "entry" and request.entry_id == "section:details" then
            instance.path = "/details"
        elseif request.kind == "parent" then
            instance.path = "/"
        elseif request.kind == "history" then
            instance.path = request.location
        else
            return { status = "This entity has no child view" }
        end
        instance.revision = instance.revision + 1
        return { refresh = true, location = instance.path }
    end,

    invoke_action = function (instance, action_id, selection)
        if action_id ~= "refresh" then return nil end
        instance.revision = instance.revision + 1
        return { refresh = true, status = "Lua demo refreshed" }
    end,

    view = function (instance, entry_id, request)
        local controller = assert(text_viewer:create({ id = entry_id }, {}))
        return mc.ui.open_viewer { controller = controller }
    end,
}

if not ok then
    error("cannot register demo panel: " .. tostring(err))
end
