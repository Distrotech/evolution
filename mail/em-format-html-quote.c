/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <camel/camel-stream.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-multipart.h>
#include <camel/camel-mime-message.h>

#include "mail-config.h"

#include "em-format-html-quote.h"


struct _EMFormatHTMLQuotePrivate {
	char *credits;
};


static void efhq_format_clone (EMFormat *, CamelMedium *, EMFormat *);
static void efhq_format_error (EMFormat *emf, CamelStream *stream, const char *txt);
static void efhq_format_message (EMFormat *, CamelStream *, CamelMedium *);
static void efhq_format_source (EMFormat *, CamelStream *, CamelMimePart *);
static void efhq_format_attachment (EMFormat *, CamelStream *, CamelMimePart *, const char *, const EMFormatHandler *);
static void efhq_complete (EMFormat *);

static void efhq_builtin_init (EMFormatHTMLQuoteClass *efhc);


static EMFormatHTMLClass *efhq_parent;


static void
efhq_init (GObject *o)
{
	EMFormatHTMLQuote *efhq = (EMFormatHTMLQuote *) o;
	
	efhq->priv = g_malloc0 (sizeof (*efhq->priv));
	
	/* we want to convert url's etc */
	((EMFormatHTML *) efhq)->text_html_flags |= CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;
}

static void
efhq_finalise (GObject *o)
{
	EMFormatHTMLQuote *efhq = (EMFormatHTMLQuote *) o;
	
	g_free (efhq->priv->credits);
	g_free (efhq->priv);
	
	((GObjectClass *) efhq_parent)->finalize (o);
}

static void
efhq_class_init (GObjectClass *klass)
{
	((EMFormatClass *) klass)->format_clone = efhq_format_clone;
	((EMFormatClass *) klass)->format_error = efhq_format_error;
	((EMFormatClass *) klass)->format_message = efhq_format_message;
	((EMFormatClass *) klass)->format_source = efhq_format_source;
	((EMFormatClass *) klass)->format_attachment = efhq_format_attachment;
	((EMFormatClass *) klass)->complete = efhq_complete;
	
	klass->finalize = efhq_finalise;
	
	efhq_builtin_init ((EMFormatHTMLQuoteClass *) klass);
}

GType
em_format_html_quote_get_type (void)
{
	static GType type = 0;
	
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (EMFormatHTMLQuoteClass),
			NULL, NULL,
			(GClassInitFunc)efhq_class_init,
			NULL, NULL,
			sizeof (EMFormatHTMLQuote), 0,
			(GInstanceInitFunc) efhq_init
		};
		
		efhq_parent = g_type_class_ref (em_format_html_get_type ());
		type = g_type_register_static (em_format_html_get_type (), "EMFormatHTMLQuote", &info, 0);
	}
	
	return type;
}

EMFormatHTMLQuote *
em_format_html_quote_new (void)
{
	return (EMFormatHTMLQuote *) g_object_new (em_format_html_quote_get_type (), NULL);
}

EMFormatHTMLQuote *
em_format_html_quote_new_with_credits (const char *credits)
{
	EMFormatHTMLQuote *emfq;
	
	emfq = (EMFormatHTMLQuote *) g_object_new (em_format_html_quote_get_type (), NULL);
	emfq->priv->credits = g_strdup (credits);
	
	return emfq;
}

static void
efhq_format_clone (EMFormat *emf, CamelMedium *part, EMFormat *src)
{
	((EMFormatClass *) efhq_parent)->format_clone (emf, part, src);
}

static void
efhq_format_error (EMFormat *emf, CamelStream *stream, const char *txt)
{
	/* FIXME: should we even bother writign error text for quoting? probably not... */
	((EMFormatClass *) efhq_parent)->format_error (emf, stream, txt);
}

static void
efhq_format_message (EMFormat *emf, CamelStream *stream, CamelMedium *part)
{
	EMFormatHTMLQuote *emfq = (EMFormatHTMLQuote *) emf;
	GConfClient *gconf;
	char *colour;
	
	gconf = mail_config_get_gconf_client ();
	colour = gconf_client_get_string (gconf, "/apps/evolution/mail/display/citation_colour", NULL);
	
	camel_stream_printf (stream, "%s<!--+GtkHTML:<DATA class=\"ClueFlow\" key=\"orig\" value=\"1\">-->"
			     "<font color=\"%s\">\n", emfq->priv->credits ? emfq->priv->credits : "",
			     colour ? colour : "#737373");
	
	g_free (colour);
	
	((EMFormatClass *) efhq_parent)->format_message (emf, stream, part);
	
	camel_stream_write_string (stream, "</font><!--+GtkHTML:<DATA class=\"ClueFlow\" clear=\"orig\">-->");
}

static void
efhq_format_source (EMFormat *emf, CamelStream *stream, CamelMimePart *part)
{
	/* FIXME: should we just format_message? */
	((EMFormatClass *) efhq_parent)->format_source (emf, stream, part);
}

static void
efhq_format_attachment (EMFormat *emf, CamelStream *stream, CamelMimePart *part, const char *mime_type, const EMFormatHandler *handle)
{
	;
}

static void
efhq_complete (EMFormat *emf)
{
	;
}

static const char *type_remove_table[] = {
	"image/gif",
	"image/jpeg",
	"image/png",
	"image/x-png",
	"image/tiff",
	"image/x-bmp",
	"image/bmp",
	"image/x-cmu-raster",
	"image/x-portable-anymap",
	"image/x-portable-bitmap",
	"image/x-portable-graymap",
	"image/x-portable-pixmap",
	"image/x-xpixmap",
	"message/external-body",
	"multipart/appledouble",
	"multipart/signed",
	
	"image/jpg",
	"image/pjpeg",
};

static void
efhq_builtin_init (EMFormatHTMLQuoteClass *efhc)
{
	int i;
	
	for (i = 0; i < sizeof (type_remove_table) / sizeof (type_remove_table[0]); i++)
		em_format_class_remove_handler ((EMFormatClass *) efhc, type_remove_table[i]);
}
