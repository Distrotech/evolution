plugin_LTLIBRARIES = liborg-gnome-evolution-dbus-ipc-events.la

@EVO_PLUGIN_RULE@

plugin_DATA = org-gnome-evolution-dbus-ipc-events.eplug

liborg_gnome_evolution_dbus_ipc_events_la_CPPFLAGS =	\
	$(AM_CPPFLAGS)					\
	-I .						\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/widgets				\
	-I$(top_builddir)/shell				\
	$(GNOME_PLATFORM_CFLAGS)			\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(EVOLUTION_MAIL_CFLAGS)

liborg_gnome_evolution_dbus_ipc_events_la_SOURCES = 	\
	dbus-handler.c					

liborg_gnome_evolution_dbus_ipc_events_la_LIBADD =			\
	$(top_builddir)/e-util/libeutil.la				\
	$(top_builddir)/shell/libeshell.la				\
	$(top_builddir)/widgets/e-timezone-dialog/libetimezonedialog.la	\
	$(top_builddir)/widgets/misc/libemiscwidgets.la			\
	$(top_builddir)/calendar/gui/libevolution-calendar.la		\
	$(top_builddir)/mail/libevolution-mail.la			\
	$(top_builddir)/capplet/settings/libevolution-mail-settings.la		\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(EVOLUTION_MAIL_LIBS)						\
	$(GNOME_PLATFORM_LIBS)

liborg_gnome_evolution_dbus_ipc_events_la_LDFLAGS = -module -avoid-version $(NO_UNDEFINED)

EXTRA_DIST = org-gnome-evolution-dbus-ipc-events.eplug.xml

BUILT_SOURCES = $(plugin_DATA)
CLEANFILES = $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
