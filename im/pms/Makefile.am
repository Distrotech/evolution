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

noinst_LIBRARIES =			\
	libemessengerbackend.a		\
	libtocbackend.a

libemessengerbackend_a_SOURCES =	\
	e-messenger-backend.c		\
	e-messenger-backend.h

libtocbackend_a_SOURCES =		\
	aol-toc-backend.c		\
	aol-toc-backend.h		\
	libtoc.c

bin_PROGRAMS = pms 

pms_SOURCES = 	\
	e-messenger-backend-dispatcher.c	\
	e-messenger-backend-dispatcher.h	\
	e-messenger-backend-factory.c

pms_LDADD =				\
	../libMessenger.a		\
	libemessengerbackend.a		\
	libtocbackend.a			\
	$(BONOBO_GNOME_LIBS)

noinst_PROGRAMS = test-backend test-pms

test_backend_SOURCES =		\
	test-backend.c

test_backend_LDADD =		\
	libtocbackend.a		\
	$(BONOBO_GNOME_LIBS)

test_pms_SOURCES =		\
	test-pms.c

test_pms_LDADD =		\
	-L../ -L../messenger	\
	-lemessenger		\
	-lMessenger		\
	$(BONOBO_GNOME_LIBS)

oafdir = $(datadir)/oaf
oaf_DATA = 						\
	GNOME_Evolution_Messenger_PMS.oaf

# EXTRA_DIST = $(IDLS) $(oaf_DATA)
