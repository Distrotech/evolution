/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-minicard-control.c
 *
 * Authors:
 *    Chris Lahey <clahey@ximian.com>
 *
 * Copyright 1999, 2000, Ximian, Inc.
 */

#include <config.h>

#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-persist.h>
#include <bonobo/bonobo-persist-stream.h>
#include <bonobo/bonobo-stream-client.h>
#include <addressbook/backend/ebook/e-book.h>
#include <addressbook/backend/ebook/e-card.h>

#include "e-minicard-control.h"
#include "e-minicard-widget.h"
#include "e-card-merging.h"

#if 0
enum {
	PROP_RUNNING
} MyArgs;

#define RUNNING_KEY  "Clock::Running"

static void
get_prop (BonoboPropertyBag *bag,
	  BonoboArg         *arg,
	  guint              arg_id,
	  CORBA_Environment *ev,
	  gpointer           user_data)
{
	GtkObject *clock = user_data;

	switch (arg_id) {

	case PROP_RUNNING:
	{
		gboolean b = GPOINTER_TO_UINT (gtk_object_get_data (clock, RUNNING_KEY));
		BONOBO_ARG_SET_BOOLEAN (arg, b);
		break;
	}

	default:
		g_warning ("Unhandled arg %d", arg_id);
		break;
	}
}

static void
set_prop (BonoboPropertyBag *bag,
	  const BonoboArg   *arg,
	  guint              arg_id,
	  CORBA_Environment *ev,
	  gpointer           user_data)
{
	GtkClock *clock = user_data;

	switch (arg_id) {

	case PROP_RUNNING:
	{
		guint i;

		i = BONOBO_ARG_GET_BOOLEAN (arg);

		if (i)
			gtk_clock_start (clock);
		else
			gtk_clock_stop (clock);

		gtk_object_set_data (GTK_OBJECT (clock), RUNNING_KEY,
				     GUINT_TO_POINTER (i));
		break;
	}

	default:
		g_warning ("Unhandled arg %d", arg_id);
		break;
	}
}
#endif

/*
 * Bonobo::PersistStream
 *
 * These two functions implement the Bonobo::PersistStream load and
 * save methods which allow data to be loaded into and out of the
 * BonoboObject.
 */
static char *
stream_read (Bonobo_Stream stream)
{
	Bonobo_Stream_iobuf *buffer;
	CORBA_Environment    ev;
	char *data = NULL;
	gint length = 0;

	CORBA_exception_init (&ev);
	do {
#define READ_CHUNK_SIZE 65536
		Bonobo_Stream_read (stream, READ_CHUNK_SIZE,
				    &buffer, &ev);

		if (ev._major != CORBA_NO_EXCEPTION) {
			CORBA_exception_free (&ev);
			return NULL;
		}

		if (buffer->_length <= 0)
			break;

		data = g_realloc (data, length + buffer->_length + 1);

		memcpy (data + length, buffer->_buffer, buffer->_length);

		length += buffer->_length;

		CORBA_free (buffer);
	} while (1);

	CORBA_free (buffer);
	CORBA_exception_free (&ev);

	if (data)
		data[length] = '\0';
	else
		data = g_strdup("");

	return data;
} /* stream_read */

/*
 * This function implements the Bonobo::PersistStream:load method.
 */
static void
pstream_load (BonoboPersistStream *ps, const Bonobo_Stream stream,
	      Bonobo_Persist_ContentType type, void *data,
	      CORBA_Environment *ev)
{
	ECard *card;
	char *vcard;
	GtkWidget *minicard = data;

	if (type && g_strcasecmp (type, "text/vCard") != 0 &&	    
	    g_strcasecmp (type, "text/x-vCard") != 0) {	    
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}

	if ((vcard = stream_read (stream)) == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_FileNotFound, NULL);
		return;
	}

	card = e_card_new_with_default_charset(vcard, "ISO-8859-1");
	g_free(vcard);
	gtk_object_set(GTK_OBJECT(minicard),
		       "card", card,
		       NULL);
	gtk_object_unref(GTK_OBJECT(card));
} /* pstream_load */

/*
 * This function implements the Bonobo::PersistStream:save method.
 */
static void
pstream_save (BonoboPersistStream *ps, const Bonobo_Stream stream,
	      Bonobo_Persist_ContentType type, void *data,
	      CORBA_Environment *ev)
{
	char                *vcard;
	ECard               *card;
	EMinicardWidget     *minicard = data;
	int                  length;

	if (type && g_strcasecmp (type, "text/vCard") != 0 &&	    
	    g_strcasecmp (type, "text/x-vCard") != 0) {	    
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}

	gtk_object_get (GTK_OBJECT (minicard),
			"card", &card,
			NULL);
	vcard = e_card_get_vcard(card);
	length = strlen (vcard);
	bonobo_stream_client_write (stream, vcard, length, ev);
	g_free (vcard);
} /* pstream_save */

static CORBA_long
pstream_get_max_size (BonoboPersistStream *ps, void *data,
		      CORBA_Environment *ev)
{
  GtkWidget *minicard = data;
  ECard *card;
  char *vcard;
  gint length;
  
  gtk_object_get (GTK_OBJECT (minicard),
		  "card", &card, NULL);
  vcard = e_card_get_vcard(card);
  length = strlen (vcard);
  g_free (vcard);

  return length;
}

static Bonobo_Persist_ContentTypeList *
pstream_get_content_types (BonoboPersistStream *ps, void *closure,
			   CORBA_Environment *ev)
{
	return bonobo_persist_generate_content_types (2, "text/vCard", "text/x-vCard");
}

static void
book_open_cb (EBook *book, EBookStatus status, gpointer closure)
{
	ECard *card = closure;
	e_card_merging_book_add_card(book, card, NULL, NULL);
	gtk_object_unref(GTK_OBJECT(card));
}

static void
save_in_addressbook(GtkWidget *button, EMinicardWidget *minicard)
{
	EBook *book;
	gchar *path, *uri;
	ECard *card;

	book = e_book_new ();

	if (!book) {
		printf ("%s: %s(): Couldn't create EBook, bailing.\n",
			__FILE__,
			__FUNCTION__);
		return;
	}


	path = g_concat_dir_and_file (g_get_home_dir (),
				      "evolution/local/Contacts/addressbook.db");
	uri = g_strdup_printf ("file://%s", path);
	g_free (path);

	gtk_object_get(GTK_OBJECT(minicard),
		       "card", &card,
		       NULL);
	gtk_object_ref(GTK_OBJECT(card));

	if (! e_book_load_uri (book, uri, book_open_cb, card)) {
		printf ("error calling load_uri!\n");
	}
	g_free(uri);
}

static BonoboObject *
e_minicard_control_factory (BonoboGenericFactory *Factory, void *closure)
{
#if 0
	BonoboPropertyBag  *pb;
#endif
	BonoboControl      *control;
	BonoboPersistStream *stream;
	GtkWidget	   *minicard;
	GtkWidget          *button;
	GtkWidget          *vbox;

	/* Create the control. */

	minicard = e_minicard_widget_new ();
	gtk_widget_show (minicard);

	button = gtk_button_new_with_label(_("Save in addressbook"));
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(save_in_addressbook), minicard);
	gtk_widget_show (button);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), minicard, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);
	gtk_widget_show (vbox);

	control = bonobo_control_new (vbox);

	stream = bonobo_persist_stream_new (pstream_load, pstream_save,
					    pstream_get_max_size,
					    pstream_get_content_types,
					    minicard);

#if 0
	/* Create the properties. */
	pb = bonobo_property_bag_new (get_prop, set_prop, clock);
	bonobo_control_set_properties (control, pb);

	bonobo_property_bag_add (pb, "running", PROP_RUNNING,
				 BONOBO_ARG_BOOLEAN, NULL,
				 "Whether or not the clock is running", 0);
#endif

	if (stream == NULL) {
		bonobo_object_unref (BONOBO_OBJECT (control));
		return NULL;
	}

	bonobo_object_add_interface (BONOBO_OBJECT (control),
				    BONOBO_OBJECT (stream));

	return BONOBO_OBJECT (control);
}

void
e_minicard_control_factory_init (void)
{
	static BonoboGenericFactory *factory = NULL;

	if (factory != NULL)
		return;

	factory =
		bonobo_generic_factory_new (
		        "OAFIID:GNOME_Evolution_Addressbook_MiniCard_ControlFactory",
			e_minicard_control_factory, NULL);

	if (factory == NULL)
		g_error ("I could not register a EMinicard control factory.");
}
