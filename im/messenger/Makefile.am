SUBDIRS = . tests

INCLUDES = 								\
	-I$(top_srcdir)/im						\
	-I$(top_srcdir)/widgets						\
	-I$(top_srcdir)/widgets/misc					\
	-I$(top_srcdir)							\
	$(BONOBO_GNOME_CFLAGS)						\
	$(EXTRA_GNOME_CFLAGS)						\
	-DEVOLUTION_IMAGES=\""$(datadir)/images/evolution"\"		\
	-DEVOLUTION_VERSION=\""$(VERSION)"\"				\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"   		\
	-DEVOLUTION_DATADIR=\""$(datadir)"\"            		\
	-DEVOLUTION_GLADEDIR=\"$(datadir)/evolution/glade\"		\
	-DG_LOG_DOMAIN=\"evolution-messenger\"

noinst_LIBRARIES =		\
	libemessenger.a

libemessenger_a_SOURCES =	\
	e-messenger.c		\
	e-messenger.h		\
	e-messenger-types.h	\
	e-messenger-listener.c  \
	e-messenger-listener.h  \
	e-messenger-identity.c  \
	e-messenger-identity.h
