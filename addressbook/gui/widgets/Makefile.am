INCLUDES =										\
	-DG_LOG_DOMAIN=\"eab-widgets\"			\
	-DEVOLUTION_ETSPECDIR=\""$(etspecdir)"\"	\
	-DEVOLUTION_IMAGESDIR=\""$(imagesdir)"\"	\
	-DEVOLUTION_GALVIEWSDIR=\""$(viewsdir)"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/addressbook			\
	-I$(top_srcdir)/addressbook/backend		\
	-I$(top_builddir)/addressbook/backend		\
	-I$(top_srcdir)/addressbook/gui/contact-editor	\
	-I$(top_srcdir)/addressbook/gui/merging		\
	-I$(top_srcdir)/addressbook/gui/component	\
	-I$(top_srcdir)/widgets/misc			\
	-I$(top_builddir)/shell				\
	-DG_DISABLE_DEPRECATED				\
	-DGTK_DISABLE_DEPRECATED			\
	-DLIBGNOME_DISABLE_DEPRECATED			\
	-DLIBGNOMEUI_DISABLE_DEPRECATED			\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

CORBA_COMPOSER_SOURCE_H	=			\
	Evolution-Composer.h
CORBA_COMPOSER_SOURCE_C =			\
	Evolution-Composer-common.c		\
	Evolution-Composer-skels.c		\
	Evolution-Composer-stubs.c
CORBA_COMPOSER_IDL = $(srcdir)/../../../composer/Evolution-Composer.idl

$(CORBA_COMPOSER_SOURCE_H): $(CORBA_COMPOSER_IDL)
	$(ORBIT_IDL) -I $(srcdir) $(IDL_INCLUDES) $(CORBA_COMPOSER_IDL)

$(CORBA_COMPOSER_SOURCE_C): $(CORBA_COMPOSER_SOURCE_H)

CORBA_SOURCE_H = $(CORBA_COMPOSER_SOURCE_H)
CORBA_SOURCE_C = $(CORBA_COMPOSER_SOURCE_C)
CORBA_SOURCE = $(CORBA_SOURCE_H) $(CORBA_SOURCE_C)

noinst_LTLIBRARIES =				\
	libeabwidgets.la

libeabwidgets_la_SOURCES =			\
	$(CORBA_SOURCE)				\
	eab-contact-display.c			\
	eab-contact-display.h			\
	eab-gui-util.c				\
	eab-gui-util.h				\
	eab-popup-control.c			\
	eab-popup-control.h			\
	eab-vcard-control.c			\
	eab-vcard-control.h			\
	e-addressbook-marshal.c			\
	e-addressbook-table-adapter.c		\
	e-addressbook-table-adapter.h		\
	e-addressbook-model.c			\
	e-addressbook-model.h			\
	e-addressbook-view.c			\
	e-addressbook-view.h

#TREEVIEW_SOURCES=	e-addressbook-treeview-adapter.c	\
#			e-addressbook-treeview-adapter.h	\
#			gal-view-factory-treeview.c		\
#			gal-view-factory-treeview.h		\
#			gal-view-treeview.c			\
#			gal-view-treeview.h

# GLib marshalling cruft

e-addressbook-marshal.h: e-addressbook-marshal.list
	( @GLIB_GENMARSHAL@ --prefix=eab_marshal e-addressbook-marshal.list --header > e-addressbook-marshal.tmp \
	&& mv e-addressbook-marshal.tmp e-addressbook-marshal.h ) \
	|| ( rm -f e-addressbook-marshal.tmp && exit 1 )

e-addressbook-marshal.c: e-addressbook-marshal.h
	( @GLIB_GENMARSHAL@ --prefix=eab_marshal e-addressbook-marshal.list --body > e-addressbook-marshal.tmp \
	&& mv e-addressbook-marshal.tmp e-addressbook-marshal.c ) \
	|| ( rm -f e-addressbook-marshal.tmp && exit 1 )

MARSHAL_GENERATED = e-addressbook-marshal.c e-addressbook-marshal.h

BUILT_SOURCES = $(CORBA_SOURCE) $(MARSHAL_GENERATED)
CLEANFILES = $(BUILT_SOURCES)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

etspec_DATA= e-addressbook-view.etspec

EXTRA_DIST = 			\
	$(etspec_DATA)		\
	e-addressbook-marshal.list
