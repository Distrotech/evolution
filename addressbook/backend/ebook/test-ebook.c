/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include "e-book.h"
#include "e-card-simple.h"
#include <libgnome/gnome-init.h>
#include <bonobo/bonobo-main.h>
#include <pthread.h>
#include <stdlib.h>

static pthread_t mainloop_thread;

static void*
run_mainloop (void *data)
{
	bonobo_main();
}

static void
print_email (EContact *contact)
{
	char *file_as = e_contact_get (contact, E_CONTACT_FILE_AS);
	GList *emails, *e;

	printf ("Contact: %s\n", file_as);
	printf ("Email addresses:\n");
	emails = e_contact_get (contact, E_CONTACT_EMAIL);
	for (e = emails; e; e = e->next) {
		EVCardAttribute *attr = e->data;
		GList *values = e_vcard_attribute_get_values (attr);
		printf ("\t%s\n",  values && values->data ? (char*)values->data : "");
		e_vcard_attribute_free (attr);
	}
	g_list_free (emails);

	g_free (file_as);

	printf ("\n");
}

static void
print_all_emails (EBook *book)
{
	EBookQuery *query;
	EBookStatus status;
	GList *cards, *c;


	/*
	  this should be:
	    query = e_book_query_field_exists (E_CARD_SIMPLE_FIELD_FULL_NAME);
	  but backends don't handle "exists" yet.
	*/
	query = e_book_query_field_test (E_CONTACT_FULL_NAME,
					 E_BOOK_QUERY_CONTAINS, "");

	status = e_book_get_contacts (book, query, &cards);

	e_book_query_unref (query);

	if (status != E_BOOK_STATUS_OK) {
		printf ("error %d getting card list\n", status);
		exit(0);
	}

	for (c = cards; c; c = c->next) {
		EContact *contact = E_CONTACT (c->data);

		print_email (contact);

		g_object_unref (contact);
	}
	g_list_free (cards);
}

static void
print_one_email (EBook *book)
{
	EBookStatus status;
	EContact *contact;

	status = e_book_get_contact (book, "pas-id-0002023", &contact);

	if (status != E_BOOK_STATUS_OK) {
		printf ("error %d getting card\n", status);
		return;
	}

	print_email (contact);

	g_object_unref (contact);
}

int
main (int argc, char **argv)
{
	EBook *book;
	EBookStatus status;

	gnome_program_init("test-ebook", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	pthread_create(&mainloop_thread, NULL, run_mainloop, NULL);


	/*
	** the actual ebook foo
	*/

	book = e_book_new ();

	printf ("loading addressbook\n");
	status = e_book_load_local_addressbook (book);
	if (status != E_BOOK_STATUS_OK) {
		printf ("failed to open local addressbook\n");
		exit(0);
	}

	printf ("printing one contact\n");
	print_one_email (book);

	printf ("printing all contacts\n");
	print_all_emails (book);

	g_object_unref (book);

	bonobo_main_quit();

	return 0;
}
