extdir = $(EXTHELPERSDIR)

SCRIPTS_IN = \
	misc.sh.in \
	text.sh.in

SCRIPTS_OUT = \
	misc.sh \
	text.sh

SCRIPTS_UNTOUCHED = \
	archive.sh \
	doc.sh \
	image.sh \
	markdown.sh \
	package.sh \
	sound.sh \
	video.sh \
	web.sh

AWK_FILES = \
	markdown_common.awk \
	markdown_render.awk \
	markdown_table.awk

ext_SCRIPTS = \
	$(SCRIPTS_UNTOUCHED) \
	$(SCRIPTS_OUT)

ext_DATA = \
	$(AWK_FILES)

EXTRA_DIST = \
	$(SCRIPTS_UNTOUCHED) \
	$(SCRIPTS_IN) \
	$(AWK_FILES)
