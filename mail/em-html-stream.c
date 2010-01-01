/*
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <gtk/gtk.h>
#include "em-html-stream.h"

#define d(x)

static EMSyncStreamClass *parent_class = NULL;

static void
emhs_cleanup (EMHTMLStream *emhs)
{
	if (emhs->sync.cancel && emhs->html_stream)
		gtk_html_stream_close (
			emhs->html_stream, GTK_HTML_STREAM_ERROR);

	emhs->html_stream = NULL;
	emhs->sync.cancel = TRUE;
	g_signal_handler_disconnect (emhs->html, emhs->destroy_id);
	g_object_unref (emhs->html);
	emhs->html = NULL;
}

static void
emhs_gtkhtml_destroy (GtkHTML *html,
                      EMHTMLStream *emhs)
{
	emhs->sync.cancel = TRUE;
	emhs_cleanup (emhs);
}

static void
em_html_stream_dispose (GObject *object)
{
	EMHTMLStream *emhs = EM_HTML_STREAM (object);

	if (emhs->html_stream != NULL) {
		/* set 'in finalise' flag */
		camel_stream_close (CAMEL_STREAM (emhs), NULL);
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gssize
emhs_sync_write (CamelStream *stream,
                 const gchar *buffer,
                 gsize n)
{
	EMHTMLStream *emhs = EM_HTML_STREAM (stream);

	if (emhs->html == NULL)
		return -1;

	if (emhs->html_stream == NULL)
		emhs->html_stream = gtk_html_begin_full (
			emhs->html, NULL, NULL, emhs->flags);

	gtk_html_stream_write (emhs->html_stream, buffer, n);

	return (gssize) n;
}

static gint
emhs_sync_flush(CamelStream *stream)
{
	EMHTMLStream *emhs = (EMHTMLStream *)stream;

	if (emhs->html_stream == NULL)
		return -1;

	gtk_html_flush (emhs->html);

	return 0;
}

static gint
emhs_sync_close (CamelStream *stream)
{
	EMHTMLStream *emhs = (EMHTMLStream *)stream;

	if (emhs->html_stream == NULL)
		return -1;

	gtk_html_stream_close (emhs->html_stream, GTK_HTML_STREAM_OK);
	emhs_cleanup (emhs);

	return 0;
}

static void
em_html_stream_class_init (EMHTMLStreamClass *class)
{
	GObjectClass *object_class;
	EMSyncStreamClass *sync_stream_class;

	parent_class = g_type_class_peek_parent (class);

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = em_html_stream_dispose;

	sync_stream_class = EM_SYNC_STREAM_CLASS (class);
	sync_stream_class->sync_write = emhs_sync_write;
	sync_stream_class->sync_flush = emhs_sync_flush;
	sync_stream_class->sync_close = emhs_sync_close;
}

GType
em_html_stream_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID)) {
		type = g_type_register_static_simple (
			EM_TYPE_SYNC_STREAM,
			"EMHTMLStream",
			sizeof (EMHTMLStreamClass),
			(GClassInitFunc) em_html_stream_class_init,
			sizeof (EMHTMLStream),
			(GInstanceInitFunc) NULL,
			0);
	}

	return type;
}

/* TODO: Could pass NULL for html_stream, and do a gtk_html_begin
   on first data -> less flashing */
CamelStream *
em_html_stream_new (GtkHTML *html,
                    GtkHTMLStream *html_stream)
{
	EMHTMLStream *new;

	g_return_val_if_fail (GTK_IS_HTML (html), NULL);

	new = g_object_new (EM_TYPE_HTML_STREAM, NULL);
	new->html_stream = html_stream;
	new->html = g_object_ref (html);
	new->flags = 0;
	new->destroy_id = g_signal_connect (
		html, "destroy",
		G_CALLBACK (emhs_gtkhtml_destroy), new);

	em_sync_stream_set_buffer_size (&new->sync, 8192);

	return CAMEL_STREAM (new);
}

void
em_html_stream_set_flags (EMHTMLStream *emhs, GtkHTMLBeginFlags flags)
{
	g_return_if_fail (EM_IS_HTML_STREAM (emhs));

	emhs->flags = flags;
}
