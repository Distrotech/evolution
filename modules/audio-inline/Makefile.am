module_LTLIBRARIES = module-audio-inline.la

module_audio_inline_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-DEVOLUTION_PRIVDATADIR=\""$(privdatadir)"\"			\
	-DG_LOG_DOMAIN=\"evolution-module-audio-inline\"		\
	$(EVOLUTION_DATA_SERVER_CFLAGS)					\
	$(GNOME_PLATFORM_CFLAGS)					\
	$(GTKHTML_CFLAGS)						\
	$(GSTREAMER_CFLAGS)

module_audio_inline_la_SOURCES =				\
	e-mail-formatter-audio.c					\
	e-mail-formatter-audio.h					\
	e-mail-parser-audio.c						\
	e-mail-parser-audio.h						\
	e-mail-part-audio.c						\
	e-mail-part-audio.h						\
	evolution-module-audio-inline.c

module_audio_inline_la_LIBADD =				\
	$(top_builddir)/e-util/libevolution-util.la			\
	$(top_builddir)/mail/libevolution-mail.la			\
	$(top_builddir)/em-format/libevolution-mail-formatter.la	\
	$(EVOLUTION_DATA_SERVER_LIBS)					\
	$(GNOME_PLATFORM_LIBS)						\
	$(GTKHTML_LIBS)							\
	$(GSTREAMER_LIBS)

module_audio_inline_la_LDFLAGS =				\
	-avoid-version -module $(NO_UNDEFINED)

-include $(top_srcdir)/git.mk
