# Lua runtime and scripts

Midnight Commander can load optional Lua 5.3+ support.  Lua is not linked into
the `mc` executable: `mc-lua.so` is a runtime extension loaded through
`dlopen` only when it is available and enabled.  That extension discovers and
runs Lua scripts.

Build it with:

```sh
./configure --enable-lua-plugin=yes --with-lua=PREFIX
make
```

Replace `PREFIX` with the Lua installation prefix, for example `/usr`.

`--enable-lua-plugin=auto` is the default.  In that mode the runtime is not
built when a compatible Lua development package is unavailable.

## Enabling and disabling

Lua is enabled by default in a Lua-enabled build.  Put this in
`~/.config/mc/ini` to disable it persistently:

```ini
[Lua]
enabled=false
mcedit_enabled=true
user_scripts_dir=
```

`user_scripts_dir` may override the user script directory, but must be an
absolute path.  `mc --no-lua` and `MC_NO_LUA=1 mc` prevent Lua code from being
loaded for one run.

**Manage Plugins** shows the Lua runtime as `core / lua-core` and its editor
bridge as `mcedit / lua-edidor`.  Clearing `lua-core` disables all Lua code on
the next start; clearing `lua-edidor` disables every Lua script in the
`mcedit` workspace on the next start.

Press Enter or F4 on `mcedit / lua-edidor` to list just the global and user Lua
scripts in `scripts/editor/`, the directory for the `mcedit` workspace.  Enter,
F4, or **Open directory** opens the selected script directory in the active
panel; use MC's normal F3/F4 on `init.lua` or another file there to view or edit
it.  Individual script choices are stored as `lua/<id>=true` in `[DisabledPlugins]` of
`~/.config/mc/plugins.ini`.  No Lua script is hot-reloaded during a session.

## Script layout

System scripts live in `${datadir}/mc/lua/scripts/`; user scripts live in
`${XDG_CONFIG_HOME:-~/.config}/mc/lua/scripts/`.  A script belongs to a fixed
workspace, determined by the directory immediately below `scripts/`:

```text
scripts/editor/base64-decode/lua.ini
scripts/editor/base64-decode/lib/format.lua       # optional
```

| Workspace | Directory below `scripts/` |
| --- | --- |
| `mc` | `mc/` |
| `mcedit` | `editor/` |
| `mcview` | `viewer/` |
| `mcterm` | `terminal/` |
| `mcdiff` | `diff/` |

The workspace is not a `lua.ini` property.  A top-level directory such as
`scripts/my-script/` is not discovered; place it under one of the directories
above instead.

The script manifest is named `lua.ini`.  It must contain:

```ini
[Lua]
id=my-script
api_version=1
name=My script
entry=init.lua
provides=events
```

The ID is limited to 64 characters from `A-Z`, `a-z`, `0-9`, `_`, `.` and
`-`, and must match its directory name.  IDs are global across all workspaces.
Scripts load in lexical ID order.  A user script with the same ID replaces the
whole system script.

`provides` describes the entry points declared by the script.  It is an
optional comma-separated list for compatibility with earlier scripts:
`events`, `macros`, or both.  The **Lua scripts** list displays this value, so
the user can distinguish a script that reacts to events from one that exposes
an action.  A script using `mc.macro()` must declare `provides=macros`.

Each script has its own Lua state.  `require("a.b")` searches the script's
`lib/a/b.lua` and then the shared library directory from the same origin.  C
modules are disabled for these lookups.

The shared directories are `${datadir}/mc/lua/lib/` for system scripts and
`${XDG_CONFIG_HOME:-~/.config}/mc/lua/lib/` for user scripts.  A system script
never searches the user shared directory.

## API

### Macros

A Lua macro is an action registered while its script is loaded, but executed
only when its key is pressed in the declared area.  This is distinct from an
event handler: loading the script registers the macro; it does not run its
action.

For now the supported area is `editor`.  The script must be in
`scripts/editor/` (the `mcedit` workspace) and declare `provides=macros`:

```lua
mc.macro {
    id = "decode-base64",
    area = "editor",
    key = "F11",
    description = "Decode Base64 selection",
    priority = 50, -- optional; 0 through 100
    action = function (ev)
        -- ev is an editor.key-style event snapshot
        return mc.CONSUME
    end,
}
```

Macro IDs are unique inside their script.  Key names are case-insensitive and
use the same spelling as `ev.key.name`, for example `F11` or `Ctrl-S`.
The matching macro with the greatest priority runs; equal priorities retain
script load and registration order.  A macro consumes its key by default.
Return `false` or `mc.PASS` to allow normal editor processing to continue.
After three action errors, only that macro is disabled for the session.

### Event handlers

Register callbacks with `mc.on()` and remove them with `mc.off()`:

```lua
local token = mc.on("panel.chdir", function (ev)
    mc.ui.status("Directory: " .. ev.new_path)
end, { priority = 10 })

mc.on("shutdown", function ()
    mc.off(token)
end)
```

Priorities range from `-100` to `100`; higher callbacks run first and equal
priorities keep registration order.  `mc.off()` is idempotent.  For
`editor.key`, returning `true` or `mc.CONSUME` stops normal editor processing;
all other event callbacks are notifications.  Prefer `mc.macro()` for a
user-visible key action; `editor.key` remains the low-level notification and
interception event.

Available event names are:

- `startup`, `shutdown`
- `panel.chdir`, `panel.selection_changed`, `panel.file_open`
- `editor.open`, `editor.save`, `editor.key`
- `viewer.open`

Every callback receives a fresh, copied snapshot.  The event-specific fields
are:

| Event | Fields in `ev` |
| --- | --- |
| `startup` | `run_mode`, `config_dir`, `data_dir` |
| `shutdown` | `reason` |
| `panel.chdir` | `panel`, `old_path`, `new_path`, `cause` |
| `panel.selection_changed` | `panel`, `current`, `selected`, `selected_count`, `selected_truncated` |
| `panel.file_open` | `panel`, `path`, `open_mode`, `is_dir` |
| `editor.open` | `editor`, `path`, `readonly`, `line`, `column` |
| `editor.save` | `editor`, `path`, `previous_path`, `save_as` |
| `editor.key` | `editor`, `key` (`name`, `code`, optional `text`, `modifiers`) |
| `viewer.open` | `viewer`, `path`, `source_kind`, `start_line` |

`current` and every element of `selected` are `mc.File` snapshots with
`name`, `path`, `is_dir`, `size`, `mtime`, and `marked`.  `selected` contains
at most 4096 items; `selected_count` remains the full count.

### Objects and commands

Panel, editor, and viewer references are opaque userdata, never C pointers.
They are valid only while their MC window is alive; a later call returns
`nil, "closed"` if it has gone away.

| Object | Creation and methods |
| --- | --- |
| Panel | `mc.panel.active()`, `mc.panel.passive()`; `:cwd()`, `:current()`, `:selected()`, `:refresh()`, `:chdir(path)` |
| Editor | `mc.editor.current()`; `:path()`, `:cursor()`, `:set_cursor(line, column)`, `:is_readonly()`, `:get_text(from, to)`, `:selected_text()`, `:insert(text)`, `:save()` |
| Viewer | `mc.viewer.current()`; `:path()`, `:position()`, `:mode()`, `:goto(offset)` |

`cursor()` and `set_cursor()` use one-based line and column numbers.
`get_text(from, to)` uses one-based inclusive byte positions.  Lua reserves
the word `goto`, so call the viewer method as
`viewer["goto"](viewer, offset)`.

`selected_text()` returns the current ordinary text selection, or
`nil, "no_selection"`.  Column selections return
`nil, "column_selection_not_supported"` rather than silently decoding a
different range.

Object and UI methods require an active MC event callback.  Outside one they
return `nil, "no active MC context"`; unavailable application modes return
`nil, "not_ready"`.  State-changing calls are rejected with
`nil, "forbidden_in_phase"` in `panel.file_open`.  That hook is a notification
only, not a way to intercept the file open operation.

`mc.ui.status(text)` updates the file-manager hint line and returns `true`.
`mc.ui.message(title, text)` displays a modal message.  Both return
`nil, "not_ready"` when no compatible UI is active; `message()` is also a
state-changing operation in `panel.file_open`.
`mc.log.debug/info/warn/error(text)` writes a message tagged with the Lua
script ID.

The installed `notify-editor-save` and `base64-decode` Lua scripts are small
working examples. Copy a script into the corresponding user workspace
directory before adapting it, so system updates do not overwrite local changes.

## Trust boundary

Lua scripts run with the permissions of the current MC process.  Install only
scripts you trust.  MC rejects a script or loaded Lua module if its directory
tree is symbolic-linked, owned by neither the current user nor root, or is
group/world writable.  A Lua callback error is isolated to that callback;
after three errors in one session the callback is disabled.
