/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <config.h>
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
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	GList *list = e_card_load_cards_from_file_with_default_charset("gnomecard.vcf", "ISO-8859-1");
	GList *iterator;
	for (iterator = list; iterator; iterator = g_list_next(iterator)) {
		ECard *card = iterator->data;
		e_book_add_card(book, card, add_card_cb, card);
	}
	g_list_free(list);
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
