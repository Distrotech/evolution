/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#include <config.h>
#include <glib.h>
#include <gtk/gtkmain.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <liboaf/liboaf.h>

#include "e-book.h"
#include "e-book-util.h"

#define TEST_VCARD                     \
"BEGIN:VCARD\r\n"                      \
"FN:Nat\r\n"                           \
"N:Friedman;Nat;D;Mr.\r\n"             \
"BDAY:1977-08-06\r\n"                  \
"TEL;WORK:617 679 1984\r\n"            \
"TEL;CELL:123 456 7890\r\n"            \
"EMAIL;INTERNET:nat@nat.org\r\n"       \
"EMAIL;INTERNET:nat@ximian.com\r\n"    \
"ADR;WORK;POSTAL:P.O. Box 101;;;Any Town;CA;91921-1234;\r\n" \
"END:VCARD\r\n"                        \
"\r\n"

static CORBA_Environment ev;
static char *cardstr;

static void
init_bonobo (int argc, char **argv)
{
	if (bonobo_init (CORBA_OBJECT_NIL, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE)
		g_error (_("Could not initialize Bonobo"));
}

static void
get_cursor_cb (EBook *book, EBookStatus status, ECardCursor *cursor, gpointer closure)
{
	long length = e_card_cursor_get_length(cursor);
	long i;

	/* we just added a card, so the length should be >1 */
	printf ("\n%s: %s(): Number of cards is %ld\n",
		__FILE__, __FUNCTION__, length);
	if (length < 1)
		printf ("*** Why isn't this above zero?? ***\n\n");
	
	for ( i = 0; i < length; i++ ) {
		ECard *card = e_card_cursor_get_nth(cursor, i);
		char *vcard = e_card_get_vcard_assume_utf8(card);
		printf("Get all cards callback: [%s]\n", vcard);
		g_free(vcard);
		gtk_object_unref(GTK_OBJECT(card));
	}
}

static void
get_card_cb (EBook *book, EBookStatus status, ECard *card, gpointer closure)
{
	char *vcard;

	vcard = e_card_get_vcard_assume_utf8(card);
	printf ("Card added: [%s]\n", vcard);
	g_free(vcard);
	gtk_object_unref(GTK_OBJECT(card));

	printf ("Getting cards..\n");
	e_book_get_cursor(book, "", get_cursor_cb, NULL);
	printf ("Done getting all cards.\n");	
}

static void
add_card_cb (EBook *book, EBookStatus status, const gchar *id, gpointer closure)
{
	GTimer *timer;

	printf ("Status: %d\n", status);

	printf ("Id: %s\n", id);

	timer = g_timer_new ();
	g_timer_start (timer);
	e_book_get_card (book, id, get_card_cb, closure);
	g_timer_stop (timer);
	printf ("%g\n", g_timer_elapsed (timer, NULL));
}

static void
get_fields_cb (EBook *book, EBookStatus status, EList *fields, gpointer closure)
{
	if (fields) {
		EIterator *iter = e_list_get_iterator (fields);

		printf ("Supported fields:\n");

		for (; e_iterator_is_valid (iter); e_iterator_next (iter)) {
			printf (" %s\n", (char*)e_iterator_get (iter));
		}

		gtk_object_unref(GTK_OBJECT(fields));
	}
	else {
		printf ("No supported fields?\n");
	}

	e_book_add_vcard(book, cardstr, add_card_cb, NULL);
}


static void
auth_user_cb (EBook *book, EBookStatus status, gpointer closure)
{
	printf ("user authenticated\n");
	e_book_get_supported_fields (book, get_fields_cb, closure);
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	e_book_authenticate_user (book, "username", "password", "auth_method", auth_user_cb, NULL);
}

static guint
ebook_create (void)
{
	EBook *book;
	
	book = e_book_new ();

	if (!book) {
		printf ("%s: %s(): Couldn't create EBook, bailing.\n",
			__FILE__,
			__FUNCTION__);
		return FALSE;
	}
	

	if (! e_book_load_default_book (book, book_open_cb, NULL)) {
		printf ("error calling load_uri!\n");
	}


	return FALSE;
}

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


int
main (int argc, char **argv)
{

	CORBA_exception_init (&ev);

	gnome_init_with_popt_table ("blah", "0.0", argc, argv, NULL, 0, NULL);
	oaf_init (argc, argv);
	init_bonobo (argc, argv);

	cardstr = NULL;
	if (argc == 2)
		cardstr = read_file (argv [1]);

	if (cardstr == NULL)
		cardstr = TEST_VCARD;

	gtk_idle_add ((GtkFunction) ebook_create, NULL);
	
	bonobo_main ();

	return 0;
}
