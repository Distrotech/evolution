
INCLUDES =						\
	-DPREFIX=\"$(prefix)\"				\
	-DSYSCONFDIR=\"$(sysconfdir)\"			\
	-DDATADIR=\"$(datadir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DG_LOG_DOMAIN=\"evolution-a11y\"		\
	-I$(top_srcdir) 				\
	-DG_DISABLE_DEPRECATED				\
	-DLIBGNOME_DISABLE_DEPRECATED			\
	$(A11Y_CFLAGS)

privlib_LTLIBRARIES = libevolution-a11y.la 

libevolution_a11y_la_SOURCES =				\
	init.c

libevolution_a11y_la_LIBADD =				\
	$(A11Y_LIBS)
