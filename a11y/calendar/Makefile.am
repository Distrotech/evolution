
noinst_LTLIBRARIES = libevolution-calendar-a11y.la

INCLUDES =						\
	-DG_LOG_DOMAIN=\"evolution-a11y\"		\
	-I$(top_srcdir)/shell				\
	-I$(top_srcdir)/calendar			\
	-I$(top_srcdir)/calendar/cal-client		\
	-I$(top_srcdir)/libical/src/libical		\
	-I$(top_srcdir)/calendar/gui			\
	-I$(top_srcdir)/widgets				\
	-I$(top_srcdir)/a11y				\
	-DEVOLUTION_DATADIR=\""$(datadir)"\"		\
	-DEVOLUTION_GLADEDIR=\""$(gladedir)"\"		\
	-DEVOLUTION_ETSPECDIR=\""$(etspecdir)"\"	\
	-DEVOLUTION_IMAGESDIR=\""$(imagesdir)"\"	\
	-DEVOLUTION_GALVIEWSDIR=\""$(viewsdir)"\"	\
	-DEVOLUTION_UIDIR=\""$(evolutionuidir)"\"	\
	-DG_DISABLE_DEPRECATED				\
	-DPREFIX=\""$(prefix)"\"			\
	$(A11Y_CFLAGS)					\
	$(EVOLUTION_CALENDAR_CFLAGS)

libevolution_calendar_a11y_la_SOURCES =		\
	ea-calendar.c				\
	ea-day-view.c				\
	ea-day-view-event.c				\
	ea-day-view-event-factory.c				\
	ea-gnome-calendar.c
