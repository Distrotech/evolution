changelogs =			\
	ChangeLog

appicondir = $(datadir)/pixmaps
appicon_DATA = evolution.png

applicationdir = $(datadir)/gnome/apps/Applications
application_DATA = evolution.desktop

EXTRA_DIST =			\
	AUTHORS			\
	$(changelogs)		\
	README			\
	HACKING			\
	MAINTAINERS		\
	NEWS			\
	evolution.spec.in	\
	$(appicon_DATA)		\
	$(application_DATA)

SUBDIRS =			\
	intl			\
	macros			\
	camel			\
	composer		\
	po			\
	doc

# Things that I don't care about
#	data			\
#	e-util			\
#	widgets			\
#	shell			\
#	libibex			\
#	libical			\
#	libversit		\
#	addressbook		\
#	calendar		\
#	wombat			\
#	art			\
#	default_user		\
#	tools			\

# Things that need to be fixed to work with camel
#	filter			\
#	mail			\

dist-hook: evolution.spec
	cp evolution.spec $(distdir)
