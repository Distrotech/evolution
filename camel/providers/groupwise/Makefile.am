## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamelgroupwise.la
camel_provider_DATA = libcamelgroupwise.urls

INCLUDES = -I.. \
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/camel/providers/imap    \
        -I$(top_srcdir)/libedataserver          \
	-I$(top_builddir)/servers/groupwise	\
	-I$(top_srcdir)/servers/groupwise       \
	$(CAMEL_CFLAGS)				\
	$(E_DATA_SERVER_CFLAGS)			\
	$(GNOME_INCLUDEDIR)			\
        $(SOUP_CFLAGS)                          \
	-DG_LOG_DOMAIN=\"camel-groupwise-provider\"

libcamelgroupwise_la_SOURCES = 			\
	camel-groupwise-provider.c		\
	camel-groupwise-folder.c                \
        camel-groupwise-store.c                 \
        camel-groupwise-summary.c               \
	camel-groupwise-store-summary.c         \
	camel-groupwise-message-cache.c         \
	camel-groupwise-transport.c            	


libcamelgroupwise_la_LDFLAGS = -avoid-version -module
libcamelgroupwise_la_LIBADD =                                            \
        $(top_builddir)/servers/groupwise/libegroupwise-1.2.la           \
	$(top_builddir)/libedataserver/libedataserver-1.2.la             \
        $(SOUP_LIBS)

EXTRA_DIST = libcamelgroupwise.urls
