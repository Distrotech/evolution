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

int
main (int argc, char **argv)
{
	EBook *book;
	EBookQuery *query;
	EBookStatus status;
	EList *cards;
	EIterator *iter;

	gnome_program_init("test-ebook", "0.0", LIBGNOME_MODULE, argc, argv, NULL);

	if (bonobo_init (&argc, argv) == FALSE)
		g_error ("Could not initialize Bonobo");

	pthread_create(&mainloop_thread, NULL, run_mainloop, NULL);

	book = e_book_new ();

	status = e_book_load_local_addressbook (book);
	if (status != E_BOOK_STATUS_OK) {
		printf ("failed to open local addressbook\n");
		exit(0);
	}

	/*
	  XXX this should be:

	  query = e_book_query_field_exists (E_CARD_SIMPLE_FIELD_FULL_NAME);

	  but backends don't handle "exists" yet.
	*/
	query = e_book_query_field_test (E_CARD_SIMPLE_FIELD_FULL_NAME,
					 E_BOOK_QUERY_CONTAINS, "");

	status = e_book_get_card_list (book, query, &cards);

	e_book_query_unref (query);

	if (status != E_BOOK_STATUS_OK) {
		printf ("error getting card list\n");
		exit(0);
	}

	iter = e_list_get_iterator (cards);
	while (e_iterator_is_valid (iter)) {
		ECard *card = (ECard*)e_iterator_get (iter);

		printf ("Contact: %s\n", card->file_as);

		e_iterator_next (iter);
	}

	g_object_unref (iter);
	g_object_unref (cards);
	g_object_unref (book);

	bonobo_main_quit();

	return 0;
}
