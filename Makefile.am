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
        intl                    \
        macros                  \
        data                    \
        e-util                  \
        widgets                 \
        shell                   \
        libibex                 \
        camel                   \
        filter                  \
        composer                \
        mail                    \
        libical                 \
        libversit               \
        addressbook             \
        wombat                  \
        art                     \
        default_user            \
        tools                   \
        po                      \
        doc

#missing file breaks build
#        calendar                \

dist-hook: evolution.spec
	cp evolution.spec $(distdir)


