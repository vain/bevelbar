__NAME__ = bevelbar
__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

CFLAGS += -Wall -Wextra -O3
CFLAGS += -I/usr/include/freetype2
LDFLAGS += -lm -lX11 -lXrandr -lXft

INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
man1dir = $(mandir)/man1


.PHONY: clean install installdirs

$(__NAME__): $(__NAME__).c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-o $@ $<

clean:
	rm -f $(__NAME__)

install: $(__NAME__) installdirs
	$(INSTALL_PROGRAM) $(__NAME__) $(DESTDIR)$(bindir)/$(__NAME__)
	$(INSTALL_DATA) $(__NAME__).1 $(DESTDIR)$(man1dir)/$(__NAME__).1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)
