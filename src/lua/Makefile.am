runtime_pluginsdir = $(runtime_plugins_dir)
runtime_plugins_LTLIBRARIES = mc-lua.la

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(GLIB_CFLAGS) \
	$(LUA_CFLAGS) \
	-DMC_LUA_SYSTEM_SCRIPTS_DIR=\"$(lua_scripts_dir)\" \
	-DMC_LUA_SYSTEM_MODULES_DIR=\"$(lua_modules_dir)\"

mc_lua_la_SOURCES = \
	mc-lua.c
mc_lua_la_LDFLAGS = -module -avoid-version
mc_lua_la_LIBADD = $(GLIB_LIBS) $(LUA_LIBS)

lua_demo_paneldir = $(lua_scripts_dir)/mc/demo-panel
dist_lua_demo_panel_DATA = \
	examples/mc/demo-panel/lua.ini \
	examples/mc/demo-panel/init.lua

lua_notify_editor_savedir = $(lua_scripts_dir)/editor/notify-editor-save
dist_lua_notify_editor_save_DATA = \
	examples/editor/notify-editor-save/lua.ini \
	examples/editor/notify-editor-save/init.lua

lua_base64_decodedir = $(lua_scripts_dir)/editor/base64-decode
dist_lua_base64_decode_DATA = \
	examples/editor/base64-decode/lua.ini \
	examples/editor/base64-decode/init.lua

lua_insert_literaldir = $(lua_scripts_dir)/editor/insert-literal
dist_lua_insert_literal_DATA = \
	examples/editor/insert-literal/lua.ini \
	examples/editor/insert-literal/init.lua

lua_insert_datetimedir = $(lua_scripts_dir)/editor/insert-datetime
dist_lua_insert_datetime_DATA = \
	examples/editor/insert-datetime/lua.ini \
	examples/editor/insert-datetime/init.lua

lua_format_paragraphdir = $(lua_scripts_dir)/editor/format-paragraph
dist_lua_format_paragraph_DATA = \
	examples/editor/format-paragraph/lua.ini \
	examples/editor/format-paragraph/init.lua

lua_sort_selectiondir = $(lua_scripts_dir)/editor/sort-selection
dist_lua_sort_selection_DATA = \
	examples/editor/sort-selection/lua.ini \
	examples/editor/sort-selection/init.lua

lua_insert_command_outputdir = $(lua_scripts_dir)/editor/insert-command-output
dist_lua_insert_command_output_DATA = \
	examples/editor/insert-command-output/lua.ini \
	examples/editor/insert-command-output/init.lua

lua_draw_tabledir = $(lua_scripts_dir)/editor/draw-table
dist_lua_draw_table_DATA = \
	examples/editor/draw-table/lua.ini \
	examples/editor/draw-table/init.lua
