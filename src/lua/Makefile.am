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

lua_notify_editor_savedir = $(lua_scripts_dir)/editor/notify-editor-save
dist_lua_notify_editor_save_DATA = \
	examples/editor/notify-editor-save/lua.ini \
	examples/editor/notify-editor-save/init.lua

lua_base64_decodedir = $(lua_scripts_dir)/editor/base64-decode
dist_lua_base64_decode_DATA = \
	examples/editor/base64-decode/lua.ini \
	examples/editor/base64-decode/init.lua
