/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <camel/camel-stream.h>
#include <camel/camel-object.h>
#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/gtkhtml-stream.h>
#include <gtk/gtkmain.h>
#include "em-camel-stream.h"

static void em_camel_stream_class_init (EMCamelStreamClass *klass);
static void em_camel_stream_init (CamelObject *object);
static void em_camel_stream_finalize (CamelObject *object);

static ssize_t stream_write(CamelStream *stream, const char *buffer, size_t n);
static int stream_close(CamelStream *stream);
static int stream_flush(CamelStream *stream);

static CamelStreamClass *parent_class = NULL;

CamelType
em_camel_stream_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (CAMEL_STREAM_TYPE,
					    "EMCamelStream",
					    sizeof (EMCamelStream),
					    sizeof (EMCamelStreamClass),
					    (CamelObjectClassInitFunc) em_camel_stream_class_init,
					    NULL,
					    (CamelObjectInitFunc) em_camel_stream_init,
					    (CamelObjectFinalizeFunc) em_camel_stream_finalize);
	}
	
	return type;
}

static void
em_camel_stream_class_init (EMCamelStreamClass *klass)
{
	CamelStreamClass *stream_class = CAMEL_STREAM_CLASS (klass);
	
	parent_class = (CamelStreamClass *) CAMEL_STREAM_TYPE;
	
	/* virtual method overload */
	stream_class->write = stream_write;
	stream_class->flush = stream_flush;
	stream_class->close = stream_close;
}

static void
em_camel_stream_init (CamelObject *object)
{
	;
}

static void
em_camel_stream_finalize (CamelObject *object)
{
	EMCamelStream *estream = (EMCamelStream *)object;

	if (estream->html_stream)
		gtk_html_stream_close(estream->html_stream, GTK_HTML_STREAM_ERROR);
}

static ssize_t
stream_write (CamelStream *stream, const char *buffer, size_t n)
{
	EMCamelStream *dstream = EM_CAMEL_STREAM (stream);

	gtk_html_stream_write (dstream->html_stream, buffer, n);
	
	return (ssize_t) n;
}

static int
stream_flush(CamelStream *stream)
{
	EMCamelStream *estream = (EMCamelStream *)stream;

	if (estream->html_stream) {
		/* FIXME: flush html stream via gtkhtml_stream_flush which doens't exist yet ... */
		while (gtk_events_pending ())
			gtk_main_iteration ();
	}

	return 0;
}

static int
stream_close(CamelStream *stream)
{
	EMCamelStream *estream = (EMCamelStream *)stream;

	if (estream->html_stream) {
		printf("closing html stream ok\n");
		gtk_html_stream_close(estream->html_stream, GTK_HTML_STREAM_OK);
		estream->html_stream = NULL;
	}

	return 0;
}

CamelStream *
em_camel_stream_new (GtkHTMLStream *html_stream)
{
	EMCamelStream *new;
	
	new = EM_CAMEL_STREAM (camel_object_new (EM_CAMEL_STREAM_TYPE));
	new->html_stream = html_stream;
	
	return CAMEL_STREAM (new);
}
