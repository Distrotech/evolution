SUBDIRS = . pms

INCLUDES = 								\
	-I$(top_srcdir)/widgets						\
	-I$(top_srcdir)/widgets/misc					\
	-I$(top_srcdir)							\
	$(BONOBO_GNOME_CFLAGS)						\
	$(EXTRA_GNOME_CFLAGS)						\
	-DEVOLUTION_IMAGES=\""$(datadir)/images/evolution"\"		\
	-DEVOLUTION_VERSION=\""$(VERSION)"\"				\
	-DEVOLUTION_LOCALEDIR=\""$(localedir)"\"   			\
	-DEVOLUTION_DATADIR=\""$(datadir)"\"            		\
	-DEVOLUTION_GLADEDIR=\"$(datadir)/evolution/glade\"		\
	-DG_LOG_DOMAIN=\"evolution-shell\"

CLEANFILES = $(IDL_GENERATED)

# CORBA stuff
IDLS = \
	Messenger.idl

IDL_GENERATED = \
	Messenger.h		\
	Messenger-common.c	\
	Messenger-skels.c	\
	Messenger-stubs.c

Messenger-impl.o: Messenger.h

$(IDL_GENERATED): $(IDLS)
	$(ORBIT_IDL) -I $(srcdir) -I $(datadir)/idl			\
	`$(GNOME_CONFIG) --cflags idl` $(srcdir)/Messenger.idl

noinst_LIBRARIES =		\
	libMessenger.a

libMessenger_a_SOURCES =	\
	$(IDL_GENERATED)

# IDL install

idldir = $(datadir)/idl

idl_DATA = $(IDLS)

