INCLUDES =						\
	-DPREFIX=\"$(prefix)\"				\
	-DSYSCONFDIR=\"$(sysconfdir)\"			\
	-DDATADIR=\"$(datadir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DG_LOG_DOMAIN=\"EBook\"			\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/camel				\
	-I$(top_srcdir)/addressbook/backend		\
	-I$(top_srcdir)/addressbook/ename		\
	-I$(top_builddir)/addressbook/backend		\
	-I$(top_builddir)/addressbook/ename		\
	-I$(top_builddir)/shell				\
	-I$(top_srcdir)/shell				\
	-DG_DISABLE_DEPRECATED				\
	-DLIBGNOME_DISABLE_DEPRECATED			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_LTLIBRARIES = libeabutil.la

libeabutil_la_SOURCES =					\
	eab-marshal.c					\
	eab-destination.c				\
	eab-destination.h				\
	eab-book-util.c					\
	eab-book-util.h

libeabutil_la_LIBADD =					      \
	$(top_builddir)/addressbook/backend/ebook/libebook.la \
	$(top_builddir)/camel/libcamel.la		      \
	$(top_builddir)/e-util/ename/libename.la	      \
	$(top_builddir)/e-util/libeutil.la

# GLib marshalling cruft

eab-marshal.h: eab-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=eab_marshal eab-marshal.list --header > eab-marshal.h.tmp \
	&& mv eab-marshal.h.tmp eab-marshal.h ) \
	|| ( rm -f eab-marshal.h.tmp && exit 1 )

eab-marshal.c: eab-marshal.h
	( @GLIB_GENMARSHAL@ --prefix=eab_marshal eab-marshal.list --body > eab-marshal.c.tmp \
	&& mv eab-marshal.c.tmp eab-marshal.c ) \
	|| ( rm -f eab-marshal.c.tmp && exit 1 )

MARSHAL_GENERATED = eab-marshal.c eab-marshal.h

BUILT_SOURCES = $(MARSHAL_GENERATED)
CLEANFILES    = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)
