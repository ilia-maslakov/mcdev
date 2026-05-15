EXTRA_DIST =

if USE_INTERNAL_EDIT
noinst_LTLIBRARIES = libedit.la
else
noinst_LTLIBRARIES =
endif

libedit_la_SOURCES = \
	bookmark.c \
	edit-impl.h \
	edit.c edit.h \
	editcomplete.c editcomplete.h \
	editbuffer.c editbuffer.h \
	editcmd.c \
	editdraw.c \
	fold.c \
	editmacros.c editmacros.h \
	editmenu.c \
	editoptions.c \
	editsearch.c editsearch.h \
	editwidget.c editwidget.h \
	../editor-plugins/etags/etags.c \
	../editor-plugins/etags/etags.h \
	../editor-plugins/spell/spell.c \
	../editor-plugins/spell/spell.h \
	../editor-plugins/ctags/ctags-parser.c \
	../editor-plugins/ctags/ctags-parser.h \
	../editor-plugins/ctags/ctags-index.c \
	../editor-plugins/ctags/ctags-index.h \
	../editor-plugins/ctags/ctags-repository.c \
	../editor-plugins/ctags/ctags-repository.h \
	../editor-plugins/ctags/ctags-config.c \
	../editor-plugins/ctags/ctags-config.h \
	../editor-plugins/ctags/ctags-fuzzy.c \
	../editor-plugins/ctags/ctags-fuzzy.h \
	../editor-plugins/ctags/ctags-history.h \
	../editor-plugins/ctags/ctags-ui.c \
	../editor-plugins/ctags/ctags-ui.h \
	../editor-plugins/ctags/ctags-plugin.c \
	../editor-plugins/ctags/ctags-plugin.h \
	format.c \
	syntax.c

AM_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir)
