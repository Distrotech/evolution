INCLUDES = 								\
	-I$(top_srcdir)/im						\
	-I$(top_srcdir)/im/messenger					\
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

noinst_PROGRAMS = test-e-messenger-identity

test_e_messenger_identity_SOURCES = \
	test-e-messenger-identity.c

test_e_messenger_identity_LDADD = \
	../libemessenger.a	  \
	$(BONOBO_GNOME_LIBS)

