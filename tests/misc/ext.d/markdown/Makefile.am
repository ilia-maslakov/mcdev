PACKAGE_STRING = "/misc/ext.d/markdown"

check_SCRIPTS = run

TESTS = run

CLEANFILES = run

data_files = \
	data/headings.md \
	data/inline.md \
	data/latex.md \
	data/table.md \
	data/mixed.md

EXTRA_DIST = test_markdown.sh $(data_files)

run:
	@echo '#!/bin/sh' > $@
	@echo '"$(abs_srcdir)/test_markdown.sh" \' >> $@
	@echo '  --data-dir "$(abs_srcdir)/data" \' >> $@
	@echo '  --scripts-dir "$(abs_top_srcdir)/misc/ext.d"' >> $@
	@chmod +x $@

run: Makefile
