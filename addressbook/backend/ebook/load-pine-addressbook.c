/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <config.h>
#include <ctype.h>
#include <stdio.h>
#include <glib.h>
#include <gtk/gtkmain.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <liboaf/liboaf.h>

#include "e-book.h"

static CORBA_Environment ev;

static void
init_bonobo (int argc, char **argv)
{
	if (bonobo_init (CORBA_OBJECT_NIL, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE)
		g_error (_("Could not initialize Bonobo"));
}

static void
add_card_cb (EBook *book, EBookStatus status, const gchar *id, gpointer closure)
{
	ECard *card = E_CARD(closure);
	char *vcard = e_card_get_vcard_assume_utf8(card);
	g_print ("Saved card: %s\n", vcard);
	g_free(vcard);
	gtk_object_unref(GTK_OBJECT(card));
}

static void
parse_line (EBook *book, char *line)
{
	char **strings;
	ECardName *name;
	ECard *card;
	EList *list;

	card = e_card_new("");
	strings = g_strsplit(line, "\t", 3);
	if (strings[0] && strings[1] && strings[2]) {
		name = e_card_name_from_string(strings[1]);
		gtk_object_set(GTK_OBJECT(card),
			       "nickname", strings[0],
			       "full_name", strings[1],
			       "name", name,
			       NULL);
		gtk_object_get(GTK_OBJECT(card),
			       "email", &list,
			       NULL);
		e_list_append(list, strings[2]);
		e_book_add_card(book, card, add_card_cb, card);
	}
	g_strfreev(strings);
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	FILE *fp = fopen (".addressbook", "r");
	char line[2 * 1024];
	int which = 0;
	char *lastline = NULL;

	if (!fp) {
		g_warning ("Can't find .addressbook");
		return;
	}

	while(fgets(line + which * 1024, 1024, fp)) {
		int length;
		char *thisline = line + which * 1024;
		length = strlen(thisline);
		if (thisline[length - 1] == '\n')
			line[--length] = 0;
		if (lastline && *thisline && isspace(*thisline)) {
			char *temp;
			while(*thisline && isspace(*thisline))
				thisline ++;
			temp = lastline;
			lastline = g_strdup_printf("%s%s", lastline, thisline);
			g_free(temp);
			continue;
		}
		if (lastline) {
			parse_line (book, lastline);
			g_free(lastline);
		}
		lastline = g_strdup(thisline);
	}

	if (lastline) {
		parse_line (book, lastline);
		g_free(lastline);
	}
}

static guint
ebook_create (void)
{
	EBook *book;
	gchar *path, *uri;
	
	book = e_book_new ();

	if (!book) {
		printf ("%s: %s(): Couldn't create EBook, bailing.\n",
			__FILE__,
			__FUNCTION__);
		return FALSE;
	}
	

	path = g_concat_dir_and_file (g_get_home_dir (),
				      "evolution/local/Contacts/addressbook.db");
	uri = g_strdup_printf ("file://%s", path);
	g_free (path);

	if (! e_book_load_uri (book, uri, book_open_cb, NULL)) {
		printf ("error calling load_uri!\n");
	}
	g_free(uri);


	return FALSE;
}

#if 0
static char *
read_file (char *name)
{
	int  len;
	char buff[65536];
	char line[1024];
	FILE *f;

	f = fopen (name, "r");
	if (f == NULL)
		g_error ("Unable to open %s!\n", name);

	len  = 0;
	while (fgets (line, sizeof (line), f) != NULL) {
		strcpy (buff + len, line);
		len += strlen (line);
	}

	fclose (f);

	return g_strdup (buff);
}
#endif

int
main (int argc, char **argv)
{

	CORBA_exception_init (&ev);

	gnome_init_with_popt_table("blah", "0.0", argc, argv, NULL, 0, NULL);
	oaf_init (argc, argv);
	init_bonobo (argc, argv);

	gtk_idle_add ((GtkFunction) ebook_create, NULL);
	
	bonobo_main ();

	return 0;
}
