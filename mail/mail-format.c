/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Authors:
 *  Dan Winship <danw@ximian.com>
 *
 *  Copyright 2000, 2001 Ximian, Inc.
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

#include <ctype.h>    /* for isprint */
#include <string.h>   /* for strstr  */
#include <fcntl.h>

#include <liboaf/liboaf.h>
#include <libgnome/libgnome.h>
#include <libgnomevfs/gnome-vfs-mime-info.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>
#include <gal/widgets/e-unicode.h>
#include <gal/util/e-iconv.h>

#include <camel/camel-mime-utils.h>
#include <camel/camel-pgp-mime.h>
#include <camel/camel-stream-null.h>
#include <shell/e-setup.h>
#include <e-util/e-html-utils.h>
#include <gal/util/e-unicode-i18n.h>

#include "mail.h"
#include "mail-tools.h"
#include "mail-display.h"
#include "mail-mt.h"
#include "mail-crypto.h"

static char *try_inline_pgp (char *start, CamelMimePart *part,
			     guint offset, MailDisplay *md);
static char *try_inline_pgp_sig (char *start, CamelMimePart *part,
				 guint offset, MailDisplay *md);
static char *try_uudecoding (char *start, CamelMimePart *part,
			     guint offset, MailDisplay *md);
static char *try_inline_binhex (char *start, CamelMimePart *part,
				guint offset, MailDisplay *md);

static gboolean handle_text_plain            (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_text_plain_flowed     (char *text,
					      MailDisplay *md);
static gboolean handle_text_enriched         (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_text_html             (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_image                 (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_mixed       (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_related     (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_alternative (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_appledouble (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_encrypted   (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_multipart_signed      (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_message_rfc822        (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);
static gboolean handle_message_external_body (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);

static gboolean handle_via_bonobo            (CamelMimePart *part,
					      const char *mime_type,
					      MailDisplay *md);

/* writes the header info for a mime message into an html stream */
static void write_headers (CamelMimeMessage *message, MailDisplay *md);

/* dispatch html printing via mimetype */
static gboolean format_mime_part (CamelMimePart *part, MailDisplay *md);

static void
free_url (gpointer key, gpointer value, gpointer data)
{
	g_free (key);
	if (data)
		g_byte_array_free (value, TRUE);
}

static void
free_part_urls (gpointer urls)
{
	g_hash_table_foreach (urls, free_url, NULL);
	g_hash_table_destroy (urls);
}

static void
free_data_urls (gpointer urls)
{
	g_hash_table_foreach (urls, free_url, GINT_TO_POINTER (1));
	g_hash_table_destroy (urls);
}

static char *
add_url (const char *kind, char *url, gpointer data, MailDisplay *md)
{
	GHashTable *urls;
	gpointer old_key, old_value;

	urls = g_datalist_get_data (md->data, kind);
	g_return_val_if_fail (urls != NULL, NULL);
	if (g_hash_table_lookup_extended (urls, url, &old_key, &old_value)) {
		g_free (url);
		url = old_key;
	}
	g_hash_table_insert (urls, url, data);
	return url;
}

/**
 * mail_format_mime_message: 
 * @mime_message: the input mime message
 * @md: the MailDisplay to render into
 *
 * Writes a CamelMimeMessage out into a MailDisplay
 **/
void
mail_format_mime_message (CamelMimeMessage *mime_message, MailDisplay *md)
{
	GHashTable *hash;
	
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (mime_message));
	
	hash = g_datalist_get_data (md->data, "part_urls");
	if (!hash) {
		hash = g_hash_table_new (g_str_hash, g_str_equal);
		g_datalist_set_data_full (md->data, "part_urls", hash,
					  free_part_urls);
	}
	hash = g_datalist_get_data (md->data, "data_urls");
	if (!hash) {
		hash = g_hash_table_new (g_str_hash, g_str_equal);
		g_datalist_set_data_full (md->data, "data_urls", hash,
					  free_data_urls);
	}
	
	hash = g_datalist_get_data (md->data, "attachment_states");
	if (!hash) {
		hash = g_hash_table_new (NULL, NULL);
		g_datalist_set_data_full (md->data, "attachment_states", hash,
					  (GDestroyNotify) g_hash_table_destroy);
	}
	hash = g_datalist_get_data (md->data, "fake_parts");
	if (!hash) {
		hash = g_hash_table_new (NULL, NULL);
		g_datalist_set_data_full (md->data, "fake_parts", hash,
					  (GDestroyNotify) g_hash_table_destroy);
	}
	
	write_headers (mime_message, md);
	format_mime_part (CAMEL_MIME_PART (mime_message), md);
}


/**
 * mail_format_raw_message: 
 * @mime_message: the input mime message
 * @md: the MailDisplay to render into
 *
 * Writes a CamelMimeMessage source out into a MailDisplay
 **/
void
mail_format_raw_message (CamelMimeMessage *mime_message, MailDisplay *md)
{
	GByteArray *bytes;
	char *html;
	
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (mime_message));
	
	if (!mail_content_loaded (CAMEL_DATA_WRAPPER (mime_message), md,
				  TRUE, NULL, NULL))
		return;
	
	mail_html_write (md->html, md->stream,
			 "<table cellspacing=0 cellpadding=10 width=\"100%\"><tr><td><tt>\n");
	
	bytes = mail_format_get_data_wrapper_text (CAMEL_DATA_WRAPPER (mime_message), md);
	if (bytes) {
		g_byte_array_append (bytes, "", 1);
		html = e_text_to_html (bytes->data, E_TEXT_TO_HTML_CONVERT_NL |
				       E_TEXT_TO_HTML_CONVERT_SPACES | E_TEXT_TO_HTML_ESCAPE_8BIT);
		g_byte_array_free (bytes, TRUE);
		
		mail_html_write (md->html, md->stream, html);
		g_free (html);
	}
	
	mail_html_write (md->html, md->stream, "</tt></td></tr></table>");
}

static const char *
get_cid (CamelMimePart *part, MailDisplay *md)
{
	char *cid;
	static int fake_cid_counter = 0;

	/* If we have a real Content-ID, use it. If we don't,
	 * make a (syntactically invalid, unique) fake one.
	 */
	if (camel_mime_part_get_content_id (part)) {
		cid = g_strdup_printf ("cid:%s",
				       camel_mime_part_get_content_id (part));
	} else
		cid = g_strdup_printf ("cid:@@@%d", fake_cid_counter++);

	return add_url ("part_urls", cid, part, md);
}

static const char *
get_location (CamelMimePart *part, MailDisplay *md)
{
	const char *loc;

	/* FIXME: relative URLs */
	loc = camel_mime_part_get_content_location (part);
	if (!loc)
		return NULL;

	return add_url ("part_urls", g_strdup (loc), part, md);
}

static const char *
get_url_for_icon (const char *icon_name, MailDisplay *md)
{
	char *icon_path, buf[1024], *url;
	int fd, nread;
	GByteArray *ba;

	/* FIXME: cache */

	if (*icon_name == '/')
		icon_path = g_strdup (icon_name);
	else {
		icon_path = gnome_pixmap_file (icon_name);
		if (!icon_path)
			return "file:///dev/null";
	}

	fd = open (icon_path, O_RDONLY);
	g_free (icon_path);
	if (fd == -1)
		return "file:///dev/null";

	ba = g_byte_array_new ();
	while (1) {
		nread = read (fd, buf, sizeof (buf));
		if (nread < 1)
			break;
		g_byte_array_append (ba, buf, nread);
	}
	close (fd);

	url = g_strdup_printf ("x-evolution-data:%p", ba);
	return add_url ("data_urls", url, ba, md);
}


static GHashTable *mime_handler_table, *mime_function_table;

static void
setup_mime_tables (void)
{
	mime_handler_table = g_hash_table_new (g_str_hash, g_str_equal);
	mime_function_table = g_hash_table_new (g_str_hash, g_str_equal);
	
	g_hash_table_insert (mime_function_table, "text/plain",
			     handle_text_plain);
	g_hash_table_insert (mime_function_table, "text/richtext",
			     handle_text_enriched);
	g_hash_table_insert (mime_function_table, "text/enriched",
			     handle_text_enriched);
	g_hash_table_insert (mime_function_table, "text/html",
			     handle_text_html);
	
	g_hash_table_insert (mime_function_table, "image/gif",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/jpeg",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/png",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-png",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/tiff",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-bmp",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/bmp",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-cmu-raster",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-ico",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-portable-anymap",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-portable-bitmap",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-portable-graymap",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-portable-pixmap",
			     handle_image);
	g_hash_table_insert (mime_function_table, "image/x-xpixmap",
			     handle_image);
	
	g_hash_table_insert (mime_function_table, "message/rfc822",
			     handle_message_rfc822);
	g_hash_table_insert (mime_function_table, "message/news",
			     handle_message_rfc822);
	g_hash_table_insert (mime_function_table, "message/external-body",
			     handle_message_external_body);
	
	g_hash_table_insert (mime_function_table, "multipart/alternative",
			     handle_multipart_alternative);
	g_hash_table_insert (mime_function_table, "multipart/related",
			     handle_multipart_related);
	g_hash_table_insert (mime_function_table, "multipart/mixed",
			     handle_multipart_mixed);
	g_hash_table_insert (mime_function_table, "multipart/appledouble",
			     handle_multipart_appledouble);
	g_hash_table_insert (mime_function_table, "multipart/encrypted",
			     handle_multipart_encrypted);
	g_hash_table_insert (mime_function_table, "multipart/signed",
			     handle_multipart_signed);
	
	/* RFC 2046 says unrecognized text subtypes can be treated
	 * as text/plain (as long as you recognize the character set),
	 * and unrecognized multipart subtypes as multipart/mixed.  */
	g_hash_table_insert (mime_function_table, "text/*",
			     handle_text_plain);
	g_hash_table_insert (mime_function_table, "multipart/*",
			     handle_multipart_mixed);
}

static gboolean
component_supports (OAF_ServerInfo *component, const char *mime_type)
{
	OAF_Property *prop;
	CORBA_sequence_CORBA_string stringv;
	int i;

	prop = oaf_server_info_prop_find (component,
					  "bonobo:supported_mime_types");
	if (!prop || prop->v._d != OAF_P_STRINGV)
		return FALSE;

	stringv = prop->v._u.value_stringv;
	for (i = 0; i < stringv._length; i++) {
		if (!g_strcasecmp (mime_type, stringv._buffer[i]))
			return TRUE;
	}
	return FALSE;
}

/**
 * mail_lookup_handler:
 * @mime_type: a MIME type
 *
 * Looks up the MIME type in its own tables and GNOME-VFS's and returns
 * a MailMimeHandler structure detailing the component, application,
 * and built-in handlers (if any) for that MIME type. (If the component
 * is non-%NULL, the built-in handler will always be handle_via_bonobo().)
 * The MailMimeHandler's @generic field is set if the match was for the
 * MIME supertype rather than the exact type.
 *
 * Return value: a MailMimeHandler (which should not be freed), or %NULL
 * if no handlers are available.
 **/
MailMimeHandler *
mail_lookup_handler (const char *mime_type)
{
	MailMimeHandler *handler;
	char *mime_type_main;
	const char *p;
	
	if (mime_handler_table == NULL)
		setup_mime_tables ();

	/* See if we've already found it. */
	handler = g_hash_table_lookup (mime_handler_table, mime_type);
	if (handler)
		return handler;

	/* No. Create a new one and look up application and full type
	 * handler. If we find a builtin, create the handler and
	 * register it.
	 */
	handler = g_new0 (MailMimeHandler, 1);
	handler->applications =
		gnome_vfs_mime_get_short_list_applications (mime_type);
	handler->builtin =
		g_hash_table_lookup (mime_function_table, mime_type);

	if (handler->builtin) {
		handler->generic = FALSE;
		goto reg;
	}

	/* Try for a exact component match. */
	handler->component = gnome_vfs_mime_get_default_component (mime_type);
	if (handler->component &&
	    component_supports (handler->component, mime_type)) {
		handler->generic = FALSE;
		handler->builtin = handle_via_bonobo;
		goto reg;
	}
	
	/* Try for a generic builtin match. */
	p = strchr (mime_type, '/');
	if (p == NULL)
		p = mime_type + strlen (mime_type);
	mime_type_main = alloca ((p - mime_type) + 3);
	memcpy (mime_type_main, mime_type, p - mime_type);
	memcpy (mime_type_main + (p - mime_type), "/*", 3);
	
	handler->builtin = g_hash_table_lookup (mime_function_table,
						mime_type_main);
	
	if (handler->builtin) {
		handler->generic = TRUE;
		if (handler->component) {
			CORBA_free (handler->component);
			handler->component = NULL;
		}
		goto reg;
	}

	/* Try for a generic component match. */
	if (handler->component) {
		handler->generic = TRUE;
		handler->builtin = handle_via_bonobo;
		goto reg;
	}

	/* If we at least got an application, use that. */
	if (handler->applications) {
		handler->generic = TRUE;
		goto reg;
	}

	/* Nada. */
	g_free (handler);
	return NULL;

 reg:
	g_hash_table_insert (mime_handler_table, g_strdup (mime_type),
			     handler);
	return handler;
}

/* An "anonymous" MIME part is one that we shouldn't call attention
 * to the existence of, but simply display.
 */
static gboolean
is_anonymous (CamelMimePart *part, const char *mime_type)
{
	if (!g_strncasecmp (mime_type, "multipart/", 10) ||
	    !g_strncasecmp (mime_type, "message/", 8))
		return TRUE;
	
	if (!g_strncasecmp (mime_type, "text/", 5) &&
	    !camel_mime_part_get_filename (part))
		return TRUE;
	
	return FALSE;
}

/**
 * mail_part_is_inline:
 * @part: a CamelMimePart
 *
 * Return value: whether or not the part should/will be displayed inline.
 **/
gboolean
mail_part_is_inline (CamelMimePart *part)
{
	const char *disposition;
	CamelContentType *content_type;
	char *type;
	gboolean anon;
	
	/* If it has an explicit disposition, return that. */
	disposition = camel_mime_part_get_disposition (part);
	if (disposition)
		return g_strcasecmp (disposition, "inline") == 0;
	
	/* Certain types should default to inline. FIXME: this should
	 * be customizable.
	 */
	content_type = camel_mime_part_get_content_type (part);
	if (!header_content_type_is (content_type, "message", "*"))
		return TRUE;
	
	/* Otherwise, display it inline if it's "anonymous", and
	 * as an attachment otherwise.
	 */
	type = header_content_type_simple (content_type);
	anon = is_anonymous (part, type);
	g_free (type);
	
	return anon;
}

enum inline_states {
	I_VALID     = (1 << 0),
	I_ACTUALLY  = (1 << 1),
	I_DISPLAYED = (1 << 2)
};

static gint
get_inline_flags (CamelMimePart *part, MailDisplay *md)
{
	GHashTable *asht;
	gint val;

	/* check if we already know. */

	asht = g_datalist_get_data (md->data, "attachment_states");
	val = GPOINTER_TO_INT (g_hash_table_lookup (asht, part));
	if (val)
		return val;

	/* ok, we don't know. Figure it out. */

	if (mail_part_is_inline (part))
		val = (I_VALID | I_ACTUALLY | I_DISPLAYED);
	else
		val = (I_VALID);

	g_hash_table_insert (asht, part, GINT_TO_POINTER (val));

	return val;
}

gboolean
mail_part_is_displayed_inline (CamelMimePart *part, MailDisplay *md)
{
	return (gboolean) (get_inline_flags (part, md) & I_DISPLAYED);
}

void
mail_part_toggle_displayed (CamelMimePart *part, MailDisplay *md)
{
	GHashTable *asht = g_datalist_get_data (md->data, "attachment_states");
	gpointer ostate, opart;
	gint state;
	
	if (g_hash_table_lookup_extended (asht, part, &opart, &ostate)) {
		g_hash_table_remove (asht, part);
		
		state = GPOINTER_TO_INT (ostate);
		
		if (state & I_DISPLAYED)
			state &= ~I_DISPLAYED;
		else
			state |= I_DISPLAYED;
	} else {
		state = I_VALID | I_DISPLAYED;
	}
	
	g_hash_table_insert (asht, part, GINT_TO_POINTER (state));
}

static void
mail_part_set_default_displayed_inline (CamelMimePart *part, MailDisplay *md,
					gboolean displayed)
{
	GHashTable *asht = g_datalist_get_data (md->data, "attachment_states");
	gint state;
	
	if (g_hash_table_lookup (asht, part))
		return;

	state = I_VALID | (displayed ? I_DISPLAYED : 0);
	g_hash_table_insert (asht, part, GINT_TO_POINTER (state));
}

static void
attachment_header (CamelMimePart *part, const char *mime_type, MailDisplay *md)
{
	char *htmlinfo, *html, *fmt;
	const char *info;
	
	/* Start the table, create the pop-up object. */
	gtk_html_stream_printf (md->stream,
				"<table cellspacing=0 cellpadding=0>"
				"<tr><td><table width=10 cellspacing=0 cellpadding=0><tr><td></td></tr></table></td>"
				"<td><object classid=\"popup:%s\" type=\"%s\"></object></td>"
				"<td><table width=3 cellspacing=0 cellpadding=0><tr><td></td></tr></table></td>"
				"<td><font size=-1>",
				get_cid (part, md), mime_type);
	
	/* Write the MIME type */
	info = gnome_vfs_mime_get_value (mime_type, "description");
	html = e_text_to_html (info ? info : mime_type, 0);
	htmlinfo = e_utf8_from_locale_string (html);
	g_free (html);
	fmt = e_utf8_from_locale_string (_("%s attachment"));
	gtk_html_stream_printf (md->stream, fmt, htmlinfo);
	g_free (htmlinfo);
	g_free (fmt);
		
	/* Write the name, if we have it. */
	info = camel_mime_part_get_filename (part);
	if (info) {
		htmlinfo = e_text_to_html (info, 0);
		gtk_html_stream_printf (md->stream, " (%s)", htmlinfo);
		g_free (htmlinfo);
	}
	
	/* Write a description, if we have one. */
	info = camel_mime_part_get_description (part);
	if (info) {
		htmlinfo = e_text_to_html (info, E_TEXT_TO_HTML_CONVERT_URLS);
		gtk_html_stream_printf (md->stream, ", \"%s\"", htmlinfo);
		g_free (htmlinfo);
	}
	
	mail_html_write (md->html, md->stream, "</font></td></tr><tr>"
			 "<td height=10><table height=10 cellspacing=0 cellpadding=0>"
			 "<tr><td></td></tr></table></td></tr></table>\n");
}

static gboolean
format_mime_part (CamelMimePart *part, MailDisplay *md)
{
	CamelDataWrapper *wrapper;
	char *mime_type;
	MailMimeHandler *handler;
	gboolean output;
	int inline_flags;

	/* Record URLs associated with this part */
	get_cid (part, md);
	get_location (part, md);

	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));

	if (CAMEL_IS_MULTIPART (wrapper) &&
	    camel_multipart_get_number (CAMEL_MULTIPART (wrapper)) == 0) {
		char *mesg;
		
		mesg = e_utf8_from_locale_string (_("Could not parse MIME message. Displaying as source."));
		mail_error_printf (md->html, md->stream, "\n%s\n", mesg);
		g_free (mesg);
		if (mail_content_loaded (wrapper, md, TRUE, NULL, NULL))
			handle_text_plain (part, "text/plain", md);
		return TRUE;
	}
	
	mime_type = camel_data_wrapper_get_mime_type (wrapper);
	g_strdown (mime_type);
	
	handler = mail_lookup_handler (mime_type);
	if (!handler) {
		char *id_type;
		
		/* Special case MIME types that we know that we can't
		 * display but are some kind of plain text to prevent
		 * evil infinite recursion.
		 */
		
		if (!strcmp (mime_type, "application/mac-binhex40")) {
			handler = NULL;
		} else {
			id_type = mail_identify_mime_part (part, md);
			if (id_type) {
				g_free (mime_type);
				mime_type = id_type;
				handler = mail_lookup_handler (id_type);
			}
		}
	}
	
	inline_flags = get_inline_flags (part, md);
	
	/* No header for anonymous inline parts. */
	if (!((inline_flags & I_ACTUALLY) && is_anonymous (part, mime_type)))
		attachment_header (part, mime_type, md);
	
	if (handler && handler->builtin && inline_flags & I_DISPLAYED &&
	    mail_content_loaded (wrapper, md, TRUE, NULL, NULL))
		output = (*handler->builtin) (part, mime_type, md);
	else
		output = TRUE;
	
	g_free (mime_type);
	return output;
}

/* flags for write_field_to_stream */
enum {
	WRITE_BOLD=1,
	WRITE_NOCOLUMNS=2,
};

static void
write_field_row_begin (const char *name, gint flags, GtkHTML *html, GtkHTMLStream *stream)
{
	char *encoded_name;
	gboolean bold = (flags & WRITE_BOLD);
	gboolean nocolumns = (flags & WRITE_NOCOLUMNS);
	
	encoded_name = e_utf8_from_gtk_string (GTK_WIDGET (html), name);
	
	if (nocolumns) {
		gtk_html_stream_printf (stream, "<tr><td>%s%s:%s ",
					bold ? "<b>" : "", encoded_name,
					bold ? "</b>" : "");
	} else {
		gtk_html_stream_printf (stream, "<tr><%s align=\"right\" valign=\"top\">%s:"
					"<b>&nbsp;</%s><td>", bold ? "th" : "td",
					encoded_name, bold ? "th" : "td");
	}
	
	g_free (encoded_name);
}

static void
write_date (CamelMimeMessage *message, int flags, GtkHTML *html, GtkHTMLStream *stream)
{
	char *datestr;
	time_t date;
	int offset;
	
	write_field_row_begin (_("Date"), flags, html, stream);
	
	date = camel_mime_message_get_date (message, &offset);
	datestr = header_format_date (date, offset);
	
	gtk_html_stream_printf (stream, "%s</td> </tr>", datestr);
	
	g_free (datestr);
}

static void
write_text_header (const char *name, const char *value, int flags, GtkHTML *html, GtkHTMLStream *stream)
{
	char *encoded;
	
	if (value && *value)
		encoded = e_text_to_html (value, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
	else
		encoded = "";
	
	write_field_row_begin (name, flags, html, stream);
	
	gtk_html_stream_printf (stream, "%s</td> </tr>", encoded);
	
	if (value && *value)
		g_free (encoded);
}

static void
write_address (MailDisplay *md, const CamelInternetAddress *addr, const char *field_name, int flags)
{
	const char *name, *email;
	gint i;
	
	if (addr == NULL || !camel_internet_address_get (addr, 0, NULL, NULL))
		return;
	
	write_field_row_begin (field_name, flags, md->html, md->stream);
	
	i = 0;
	while (camel_internet_address_get (addr, i, &name, &email)) {
		CamelInternetAddress *subaddr;
		gchar *addr_txt, *addr_url;
		gboolean have_name = name && *name;
		gboolean have_email = email && *email;
		gchar *name_disp = NULL;
		gchar *email_disp = NULL;

		subaddr = camel_internet_address_new ();
		camel_internet_address_add (subaddr, name, email);
		addr_txt = camel_address_format (CAMEL_ADDRESS (subaddr));
		addr_url = camel_url_encode (addr_txt, TRUE, NULL);
		camel_object_unref (CAMEL_OBJECT (subaddr));
		
		if (have_name) {
			name_disp = e_text_to_html (name, 0);
		}
		
		if (have_email) {
			email_disp = e_text_to_html (email, 0);
		}
		
		if (i)
			mail_html_write (md->html, md->stream, ", ");
		
		if (have_email || have_name) {
			if (!have_email) {
				email_disp = g_strdup ("???");
			}
			
			if (have_name) {
				gtk_html_stream_printf (md->stream,
							"%s &lt;<a href=\"mailto:%s\">%s</a>&gt;",
							name_disp, addr_url, email_disp);
			} else {
				gtk_html_stream_printf (md->stream,
							"<a href=\"mailto:%s\">%s</a>",
							addr_url, email_disp);
			}			

		} else {
			char *str;
			
			str = e_utf8_from_locale_string (_("Bad Address"));
			gtk_html_stream_printf (md->stream, "<i>%s</i>", str);
			g_free (str);
		}

		g_free (name_disp);
		g_free (email_disp);
		g_free (addr_txt);
		g_free (addr_url);
		
		i++;
	}
	
	mail_html_write (md->html, md->stream, "</td></tr>");
}

/* order of these must match write_header code */
static char *default_headers[] = {
	"From", "Reply-To", "To", "Cc", "Bcc", "Subject", "Date",
};

/* return index of header in default_headers array */
static int
default_header_index(const char *name)
{
	int i;

	for (i=0;i<sizeof(default_headers)/sizeof(default_headers[0]);i++)
		if (!g_strcasecmp(name, default_headers[i]))
			return i;
	
	return -1;
}

/* index is index of header in default_headers array */
static void
write_default_header(CamelMimeMessage *message, MailDisplay *md, int index, int flags)
{
	switch(index) {
	case 0:
		write_address (md, camel_mime_message_get_from (message), _("From"), flags | WRITE_BOLD);
		break;
	case 1:
		write_address (md, camel_mime_message_get_reply_to (message), _("Reply-To"), flags | WRITE_BOLD);
		break;
	case 2:
		write_address(md, camel_mime_message_get_recipients(message, CAMEL_RECIPIENT_TYPE_TO),
			      _("To"), flags | WRITE_BOLD);
		break;
	case 3:
		write_address (md, camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC),
			       _("Cc"), flags | WRITE_BOLD);
		break;
	case 4:
		write_address (md, camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_BCC),
			       _("Bcc"), flags | WRITE_BOLD);
		break;
	case 5:
		write_text_header (_("Subject"), camel_mime_message_get_subject (message),
				   flags | WRITE_BOLD, md->html, md->stream);
		break;
	case 6:
		write_date (message, flags | WRITE_BOLD, md->html, md->stream);
		break;
	default:
		g_assert_not_reached();
	}
}

#define COLOR_IS_LIGHT(r, g, b)  ((r + g + b) > (128 * 3))

static void
write_headers (CamelMimeMessage *message, MailDisplay *md)
{
	gboolean full = (md->display_style == MAIL_CONFIG_DISPLAY_FULL_HEADERS);
	char bgcolor[7], fontcolor[7];
	GtkStyle *style = NULL;
	int i;

	/* My favorite thing to do...much around with colors so we respect people's stupid themes */
	style = gtk_widget_get_style (GTK_WIDGET (md->html));
	if (style) {
		int state = GTK_WIDGET_STATE (GTK_WIDGET (md->html));
		gushort r, g, b;
		
		r = style->base[state].red / 256;
		g = style->base[state].green / 256;
		b = style->base[state].blue / 256;
		
		if (COLOR_IS_LIGHT (r, g, b)) {
			r *= 0.92;
			g *= 0.92;
			b *= 0.92;
		} else {
			r = 255 - (0.92 * (255 - r));
			g = 255 - (0.92 * (255 - g));
			b = 255 - (0.92 * (255 - b));
		}
		
		sprintf (bgcolor, "%.2X%.2X%.2X", r, g, b);
		
		r = style->text[state].red;
		g = style->text[state].green / 256;
		b = style->text[state].blue / 256;
		
		sprintf (fontcolor, "%.2X%.2X%.2X", r, g, b);
	} else {
		strcpy (bgcolor, "EEEEEE");
		strcpy (fontcolor, "000000");
	}
	
	gtk_html_stream_printf (md->stream,
				"<table width=\"100%%\" cellpadding=0 cellspacing=0>"
				"<tr><td colspan=3 height=10><table height=10 cellpadding=0 cellspacing=0>"
				"<tr><td></td></tr></table></td></tr>"
				"<tr><td><table width=10 cellpadding=0 cellspacing=0><tr><td></td></tr></table></td>"
				"<td width=\"100%%\"><font color=\"#%s\">"
				"<table bgcolor=\"#000000\" width=\"100%%\" "
				"cellspacing=0 cellpadding=1><tr><td>"
				"<table bgcolor=\"#%s\" width=\"100%%\" cellpadding=0 cellspacing=0>"
				"<tr><td><table>\n", fontcolor, bgcolor);
	
	if (full) {
		struct _header_raw *header;
		const char *charset;
		CamelContentType *ct;
		char *value;

		ct = camel_mime_part_get_content_type(CAMEL_MIME_PART(message));
		charset = header_content_type_param(ct, "charset");
		charset = e_iconv_charset_name(charset);

		header = CAMEL_MIME_PART(message)->headers;
		while (header) {
			i = default_header_index(header->name);
			if (i == -1) {
				value = header_decode_string(header->value, charset);
				write_text_header(header->name, value, WRITE_NOCOLUMNS, md->html, md->stream);
				g_free(value);
			} else
				write_default_header(message, md, i, WRITE_NOCOLUMNS);
			header = header->next;
		}
	} else {
		for (i=0;i<sizeof(default_headers)/sizeof(default_headers[0]);i++)
			write_default_header(message, md, i, 0);
	}
	
	mail_html_write (md->html, md->stream,
			 "</table></td></tr></table></td></tr></table></font></td>"
			 "<td><table width=10 cellpadding=0 cellspacing=0><tr><td>"
			 "</td></tr></table></td></tr></table>\n");
}

static void
load_offline_content (MailDisplay *md, gpointer data)
{
	CamelDataWrapper *wrapper = data;
	CamelStream *stream;
	
	stream = camel_stream_null_new ();
	camel_data_wrapper_write_to_stream (wrapper, stream);
	camel_object_unref (CAMEL_OBJECT (stream));
	camel_object_unref (CAMEL_OBJECT (wrapper));
}

gboolean
mail_content_loaded (CamelDataWrapper *wrapper, MailDisplay *md, gboolean redisplay, const gchar *url, GtkHTMLStream *handle)
{
	if (!camel_data_wrapper_is_offline (wrapper))
		return TRUE;
	
	camel_object_ref (CAMEL_OBJECT (wrapper));
	if (redisplay)
		mail_display_redisplay_when_loaded (md, wrapper, load_offline_content, wrapper);
	else
		mail_display_stream_write_when_loaded (md, wrapper, url, load_offline_content, handle, wrapper);
	
	return FALSE;
}

/* Return the contents of a data wrapper, or %NULL if it contains only
 * whitespace.
 */
GByteArray *
mail_format_get_data_wrapper_text (CamelDataWrapper *wrapper, MailDisplay *mail_display)
{
	CamelStream *memstream;
	CamelStreamFilter *filtered_stream;
	GByteArray *ba;
	char *text, *end;
	
	memstream = camel_stream_mem_new ();
	ba = g_byte_array_new ();
	camel_stream_mem_set_byte_array (CAMEL_STREAM_MEM (memstream), ba);
	
	filtered_stream = camel_stream_filter_new_with_stream (memstream);
	camel_object_unref (CAMEL_OBJECT (memstream));
	
	if (wrapper->rawtext || (mail_display && mail_display->charset)) {
		CamelMimeFilterCharset *filter;
		const char *charset;
		
		if (!wrapper->rawtext) {
			/* data wrapper had been successfully converted to UTF-8 using the mime
			   part's charset, but the user thinks he knows best so we'll let him
			   shoot himself in the foot here... */
			CamelContentType *content_type;
			
			/* get the original charset of the mime part */
			content_type = camel_data_wrapper_get_mime_type_field (wrapper);
			charset = content_type ? header_content_type_param (content_type, "charset") : NULL;
			if (!charset)
				charset = mail_config_get_default_charset ();
			
			/* since the content is already in UTF-8, we need to decode into the
			   original charset before we can convert back to UTF-8 using the charset
			   the user is overriding with... */
			filter = camel_mime_filter_charset_new_convert ("utf-8", charset);
			if (filter) {
				camel_stream_filter_add (filtered_stream, CAMEL_MIME_FILTER (filter));
				camel_object_unref (CAMEL_OBJECT (filter));
			}
		}
		
		/* find out the charset the user wants to override to */
		if (mail_display && mail_display->charset)
			charset = mail_display->charset;
		else
			charset = mail_config_get_default_charset ();
		
		filter = camel_mime_filter_charset_new_convert (charset, "utf-8");
		if (filter) {
			camel_stream_filter_add (filtered_stream, CAMEL_MIME_FILTER (filter));
			camel_object_unref (CAMEL_OBJECT (filter));
		}
	}
	
	camel_data_wrapper_write_to_stream (wrapper, CAMEL_STREAM (filtered_stream));
	camel_stream_flush (CAMEL_STREAM (filtered_stream));
	camel_object_unref (CAMEL_OBJECT (filtered_stream));
	
	for (text = ba->data, end = text + ba->len; text < end; text++) {
		if (!isspace ((unsigned char)*text))
			break;
	}
	
	if (text >= end) {
		g_byte_array_free (ba, TRUE);
		return NULL;
	}
	
	return ba;
}

static void
write_hr (MailDisplay *md)
{
	mail_html_write (md->html, md->stream,
			 "<table cellspacing=0 cellpadding=10 width=\"100%\"><tr><td width=\"100%\">"
			 "<hr noshadow size=1></td></tr></table>\n");
}

/*----------------------------------------------------------------------*
 *                     Mime handling functions
 *----------------------------------------------------------------------*/

struct {
	char *start;
	char * (*handler) (char *start, CamelMimePart *part,
			   guint offset, MailDisplay *md);
} text_specials[] = {
	{ "-----BEGIN PGP MESSAGE-----\n", try_inline_pgp },
	{ "-----BEGIN PGP SIGNED MESSAGE-----\n", try_inline_pgp_sig },
	{ "begin ", try_uudecoding },
	{ "(This file must be converted with BinHex 4.0)\n", try_inline_binhex }
};
#define NSPECIALS (sizeof (text_specials) / sizeof (*text_specials))

static void
write_one_text_plain_chunk (const char *text, int len, MailDisplay *md)
{
	char *buf;
	
	mail_html_write (md->html, md->stream,
			 "<table cellspacing=0 cellpadding=10 width=\"100%\"><tr><td>\n");
	
	buf = g_strndup (text, len);
	mail_text_write  (md->html, md->stream, buf);
	g_free (buf);
	
	mail_html_write (md->html, md->stream, "</td></tr></table>\n");
}

static gboolean
handle_text_plain (CamelMimePart *part, const char *mime_type,
		   MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	CamelContentType *type;
	gboolean check_specials;
	char *p, *start, *text;
	const char *format;
	GByteArray *bytes;
	int i;
	
	bytes = mail_format_get_data_wrapper_text (wrapper, md);
	if (!bytes)
		return FALSE;
	
	g_byte_array_append (bytes, "", 1);
	text = bytes->data;
	g_byte_array_free (bytes, FALSE);
	
	/* Check for RFC 2646 flowed text. */
	type = camel_mime_part_get_content_type (part);
	format = header_content_type_param (type, "format");
	if (format && !g_strcasecmp (format, "flowed"))
		return handle_text_plain_flowed (text, md);
	
	/* Only look for binhex and stuff if this is real text/plain.
	 * (and not, say, application/mac-binhex40 that mail-identify
	 * has decided to call text/plain because it starts with English
	 * text...)
	 */
	check_specials = !g_strcasecmp (mime_type, "text/plain");
	
	p = text;
	while (p && check_specials) {
		/* Look for special cases. */
		for (i = 0; i < NSPECIALS; i++) {
			start = strstr (p, text_specials[i].start);
			if (start && (start == p || start[-1] == '\n'))
				break;
		}
		if (!start)
			break;
		
		/* Deal with special case */
		if (start != p)
			write_one_text_plain_chunk (p, start - p, md);
		
		p = text_specials[i].handler (start, part, start - text, md);
		if (p == start) {
			/* Oops. That failed. Output this line normally and
			 * skip over it.
			 */
			p = strchr (start, '\n');
			/* Last line, drop out, and dump */
			if (p == NULL) {
				p = start;
				break;
			}
			p++;
			write_one_text_plain_chunk (start, p - start, md);
		} else if (p)
			write_hr (md);
	}
	/* Finish up (or do the whole thing if there were no specials). */
	if (p)
		write_one_text_plain_chunk (p, strlen (p), md);
	
	g_free (text);
	
	return TRUE;
}

static gboolean
handle_text_plain_flowed (char *buf, MailDisplay *md)
{
	char *text, *line, *eol, *p;
	int prevquoting = 0, quoting, len, br_pending = 0;
	guint32 citation_color = mail_config_get_citation_color ();
	
	mail_html_write (md->html, md->stream,
			 "\n<!-- text/plain, flowed -->\n"
			 "<table cellspacing=0 cellpadding=10 width=\"100%\"><tr><td>\n<tt>\n");
	
	for (line = buf; *line; line = eol + 1) {
		/* Process next line */
		eol = strchr (line, '\n');
		if (eol)
			*eol = '\0';

		quoting = 0;
		for (p = line; *p == '>'; p++)
			quoting++;
		if (quoting != prevquoting) {
			if (prevquoting == 0) {
				gtk_html_stream_printf (md->stream, "<font color=\"#%06x\">", citation_color);
				if (br_pending)
					br_pending--;
			}
			while (quoting > prevquoting) {
				mail_html_write (md->html, md->stream, "<blockquote>");
				prevquoting++;
			}
			while (quoting < prevquoting) {
				mail_html_write (md->html, md->stream, "</blockquote>");
				prevquoting--;
			}
			if (quoting == 0) {
				mail_html_write (md->html, md->stream, "</font>\n");
				if (br_pending)
					br_pending--;
			}
		}
		
		if (*p == ' ')
			p++;
		len = strlen (p);
		if (len == 0) {
			br_pending++;
			continue;
		}
		
		while (br_pending) {
			mail_html_write (md->html, md->stream, "<br>\n");
			br_pending--;
		}
		
		/* replace '<' with '&lt;', etc. */
		text = e_text_to_html (p, E_TEXT_TO_HTML_CONVERT_SPACES |
				       E_TEXT_TO_HTML_CONVERT_URLS);
		if (text && *text)
			mail_html_write (md->html, md->stream, text);
		g_free (text);
		
		if (p[len - 1] != ' ' || !strcmp (p, "-- "))
			br_pending++;
		
		if (!eol)
			break;
	}
	g_free (buf);

	mail_html_write (md->html, md->stream, "</tt>\n</td></tr></table>\n");

	return TRUE;
}

static CamelMimePart *
fake_mime_part_from_data (const char *data, int len, const char *type,
			  guint offset, MailDisplay *md)
{
	GHashTable *fake_parts = g_datalist_get_data (md->data, "fake_parts");
	CamelStream *memstream;
	CamelDataWrapper *wrapper;
	CamelMimePart *part;

	part = g_hash_table_lookup (fake_parts, GUINT_TO_POINTER (offset));
	if (part)
		return part;

	memstream = camel_stream_mem_new_with_buffer (data, len);
	wrapper = camel_data_wrapper_new ();
	camel_data_wrapper_construct_from_stream (wrapper, memstream);
	camel_data_wrapper_set_mime_type (wrapper, type);
	camel_object_unref (CAMEL_OBJECT (memstream));
	part = camel_mime_part_new ();
	camel_medium_set_content_object (CAMEL_MEDIUM (part), wrapper);
	camel_object_unref (CAMEL_OBJECT (wrapper));
	camel_mime_part_set_disposition (part, "inline");

	g_hash_table_insert (fake_parts, GUINT_TO_POINTER (offset), part);
	return part;
}

static void
destroy_part (CamelObject *root, gpointer event_data, gpointer user_data)
{
	camel_object_unref (user_data);
}

static char *
try_inline_pgp (char *start, CamelMimePart *mime_part,
		guint offset, MailDisplay *md)
{
	CamelMimePart *part;
	CamelMultipart *multipart;
	char *end;
	
	end = strstr (start, "\n-----END PGP MESSAGE-----\n");
	if (!end)
		return start;
	
	end += sizeof ("\n-----END PGP MESSAGE-----\n") - 1;
	
	multipart = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart),
					  "multipart/encrypted; "
					  "protocol=\"application/pgp-encrypted\"; "
					  "x-inline-pgp-hack=true");
	
	part = fake_mime_part_from_data ("Version: 1\n",
					 sizeof ("Version: 1\n") - 1,
					 "application/pgp-encrypted",
					 offset + 1, md);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (CAMEL_OBJECT (part));
	
	part = fake_mime_part_from_data (start, end - start + 1,
					 "application/octet-stream",
					 offset, md);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (CAMEL_OBJECT (part));
	
	part = camel_mime_part_new ();
	camel_medium_set_content_object (CAMEL_MEDIUM (part),
					 CAMEL_DATA_WRAPPER (multipart));
	
	camel_object_hook_event (CAMEL_OBJECT (md->current_message),
				 "finalize", destroy_part, part);
	
	write_hr (md);
	format_mime_part (part, md);
	
	return end;
}

static char *
try_inline_pgp_sig (char *start, CamelMimePart *mime_part,
		    guint offset, MailDisplay *md)
{
	CamelMimePart *part;
	CamelMultipart *multipart;
	char *msg_start, *msg_end, *sig_start, *sig_end;
	CamelContentType *type;
	char *type_str;
	
	/* We know start points to "-----BEGIN PGP SIGNED MESSAGE-----\n" */
	msg_start = start + sizeof ("-----BEGIN PGP SIGNED MESSAGE-----\n") - 1;
	/* Skip 'One or more "Hash" Armor Headers' followed by
	 * 'Exactly one empty line'.
	 */
	msg_start = strstr (msg_start, "\n\n");
	if (!msg_start)
		return start;
	msg_start += 2;
	msg_end = strstr (msg_start, "\n-----BEGIN PGP SIGNATURE-----\n");
	if (!msg_end)
		return start;
	
	sig_start = msg_end;
	sig_end = strstr (sig_start, "\n-----END PGP SIGNATURE-----\n");
	if (!sig_end)
		return start;
	sig_end += sizeof ("\n-----END PGP SIGNATURE-----\n") - 1;
	
	multipart = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart),
					  "multipart/signed; micalg=pgp-sha1;"
					  "x-inline-pgp-hack=true");
	
	type = camel_mime_part_get_content_type (mime_part);
	type_str = header_content_type_format (type);
	part = fake_mime_part_from_data (msg_start, msg_end - msg_start,
					 type_str, offset, md);
	g_free (type_str);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (CAMEL_OBJECT (part));
	
	part = fake_mime_part_from_data (sig_start, sig_end - sig_start,
					 "application/pgp-signature",
					 offset + 1, md);
	camel_multipart_add_part (multipart, part);
	camel_object_unref (CAMEL_OBJECT (part));
	
	part = camel_mime_part_new ();
	camel_medium_set_content_object (CAMEL_MEDIUM (part),
					 CAMEL_DATA_WRAPPER (multipart));
	
	camel_object_hook_event (CAMEL_OBJECT (md->current_message),
				 "finalize", destroy_part, part);
	
	write_hr (md);
	format_mime_part (part, md);
	
	return sig_end;
}

static char *
try_uudecoding (char *start, CamelMimePart *mime_part,
		guint offset, MailDisplay *md)
{
	int mode, len, state = 0;
	char *filename, *estart, *p, *out, uulen = 0;
	guint32 save = 0;
	CamelMimePart *part;
	
	/* Make sure it's a real uudecode begin line:
	 * begin [0-7]+ .*
	 */
	mode = strtoul (start + 6, &p, 8);
	if (p == start + 6 || *p != ' ')
		return start;
	estart = strchr (start, '\n');
	if (!estart)
		return start;
	
	while (isspace ((unsigned char)*p))
		p++;
	filename = g_strndup (p, estart++ - p);
	
	/* Make sure there's an end line. */
	p = strstr (p, "\nend\n");
	if (!p) {
		g_free (filename);
		return start;
	}
	
	out = g_malloc (p - estart);
	len = uudecode_step (estart, p - estart, out, &state, &save, &uulen);
	
	part = fake_mime_part_from_data (out, len, "application/octet-stream",
					 offset, md);
	g_free (out);
	camel_mime_part_set_filename (part, filename);
	g_free (filename);
	camel_object_hook_event (CAMEL_OBJECT (md->current_message),
				 "finalize", destroy_part, part);
	
	write_hr (md);
	format_mime_part (part, md);
	
	return p + 4;
}

static char *
try_inline_binhex (char *start, CamelMimePart *mime_part,
		   guint offset, MailDisplay *md)
{
	char *p;
	CamelMimePart *part;
	
	/* Find data start. */
	p = strstr (start, "\n:");
	if (!p)
		return start;
	
	/* And data end. */
	p = strchr (p + 2, ':');
	if (!p || (*(p + 1) != '\n' && *(p + 1) != '\0'))
		return start;
	p += 2;
	
	part = fake_mime_part_from_data (start, p - start,
					 "application/mac-binhex40",
					 offset, md);
	camel_object_hook_event (CAMEL_OBJECT (md->current_message),
				 "finalize", destroy_part, part);
	
	write_hr (md);
	format_mime_part (part, md);
	
	return p;
}

static void
g_string_append_len (GString *string, const char *str, int len)
{
	char *tmp;
	
	tmp = g_malloc (len + 1);
	tmp[len] = 0;
	memcpy (tmp, str, len);
	g_string_append (string, tmp);
	g_free (tmp);
}

/* text/enriched (RFC 1896) or text/richtext (included in RFC 1341) */
static gboolean
handle_text_enriched (CamelMimePart *part, const char *mime_type,
		      MailDisplay *md)
{
	static GHashTable *translations = NULL;
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	GByteArray *ba, *bytes;
	char *text, *p, *xed;
	int len, nofill = 0;
	gboolean enriched;
	GString *string;
	
	if (!translations) {
		translations = g_hash_table_new (g_strcase_hash,
						 g_strcase_equal);
		g_hash_table_insert (translations, "bold", "<b>");
		g_hash_table_insert (translations, "/bold", "</b>");
		g_hash_table_insert (translations, "italic", "<i>");
		g_hash_table_insert (translations, "/italic", "</i>");
		g_hash_table_insert (translations, "fixed", "<tt>");
		g_hash_table_insert (translations, "/fixed", "</tt>");
		g_hash_table_insert (translations, "smaller", "<font size=-1>");
		g_hash_table_insert (translations, "/smaller", "</font>");
		g_hash_table_insert (translations, "bigger", "<font size=+1>");
		g_hash_table_insert (translations, "/bigger", "</font>");
		g_hash_table_insert (translations, "underline", "<u>");
		g_hash_table_insert (translations, "/underline", "</u>");
		g_hash_table_insert (translations, "center", "<p align=center>");
		g_hash_table_insert (translations, "/center", "</p>");
		g_hash_table_insert (translations, "flushleft", "<p align=left>");
		g_hash_table_insert (translations, "/flushleft", "</p>");
		g_hash_table_insert (translations, "flushright", "<p align=right>");
		g_hash_table_insert (translations, "/flushright", "</p>");
		g_hash_table_insert (translations, "excerpt", "<blockquote>");
		g_hash_table_insert (translations, "/excerpt", "</blockquote>");
		g_hash_table_insert (translations, "paragraph", "<p>");
		g_hash_table_insert (translations, "signature", "<address>");
		g_hash_table_insert (translations, "/signature", "</address>");
		g_hash_table_insert (translations, "comment", "<!-- ");
		g_hash_table_insert (translations, "/comment", " -->");
		g_hash_table_insert (translations, "param", "<!-- ");
		g_hash_table_insert (translations, "/param", " -->");
		g_hash_table_insert (translations, "np", "<hr>");
	}
	
	bytes = mail_format_get_data_wrapper_text (wrapper, md);
	if (!bytes)
		return FALSE;
	
	if (!g_strcasecmp (mime_type, "text/richtext")) {
		enriched = FALSE;
		mail_html_write (md->html, md->stream,
				 "\n<!-- text/richtext -->\n");
	} else {
		enriched = TRUE;
		mail_html_write (md->html, md->stream,
				 "\n<!-- text/enriched -->\n");
	}
	
	/* This is not great code, but I don't feel like fixing it right
	 * now. I mean, it's just text/enriched...
	 */
	string = g_string_sized_new (2 * bytes->len);
	g_byte_array_append (bytes, "", 1);
	p = text = bytes->data;
	g_byte_array_free (bytes, FALSE);
	
	while (p) {
		len = strcspn (p, " <>&\n");
		if (len)
			g_string_append_len (string, p, len);
		
		p += len;
		if (!*p)
			break;
		
		switch (*p++) {
		case ' ':
			while (*p == ' ') {
				g_string_append (string, "&nbsp;");
				p++;
			}
			g_string_append (string, " ");
			break;
		case '\n':
			g_string_append (string, " ");
			if (enriched && nofill <= 0) {
				while (*p == '\n') {
					g_string_append (string, "<br>");
					p++;
				}
			}
			break;
		case '>':
			g_string_append (string, "&gt;");
			break;
		case '&':
			g_string_append (string, "&amp;");
			break;
		case '<':
			if (enriched) {
				if (*p == '<') {
					g_string_append (string, "&lt;");
					p++;
					break;
				}
			} else {
				if (strncmp (p, "lt>", 3) == 0) {
					g_string_append (string, "&lt;");
					p += 3;
					break;
				} else if (strncmp (p, "nl>", 3) == 0) {
					g_string_append (string, "<br>");
					p += 3;
					break;
				}
			}
			
			if (strncmp (p, "nofill>", 7) == 0) {
				nofill++;
				g_string_append (string, "<pre>");
			} else if (strncmp (p, "/nofill>", 8) == 0) {
				nofill--;
				g_string_append (string, "</pre>");
			} else {
				char *copy, *match;
				
				len = strcspn (p, ">");
				copy = g_strndup (p, len);
				match = g_hash_table_lookup (translations,
							     copy);
				g_free (copy);
				if (match)
					g_string_append (string, match);
			}
			
			p = strchr (p, '>');
			if (p)
				p++;
		}
	}
	g_free (text);
	
	ba = g_byte_array_new ();
	g_byte_array_append (ba, (const guint8 *)string->str,
			     string->len);
	g_string_free (string, TRUE);
	
	xed = g_strdup_printf ("x-evolution-data:%p", part);
	gtk_html_stream_printf (md->stream, "<iframe src=\"%s\" frameborder=0 scrolling=no></iframe>", xed);
	add_url ("data_urls", xed, ba, md);
	
	return TRUE;
}

static gboolean
handle_text_html (CamelMimePart *part, const char *mime_type,
		  MailDisplay *md)
{
	const char *location;
	
	mail_html_write (md->html, md->stream, "\n<!-- text/html -->\n");
	
	/* FIXME: deal with relative URLs */
	location = get_location (part, md);
	if (!location)
		location = get_cid (part, md);
	gtk_html_stream_printf (md->stream, "<iframe src=\"%s\" frameborder=0 scrolling=no></iframe>", location);
	return TRUE;
}

static gboolean
handle_image (CamelMimePart *part, const char *mime_type, MailDisplay *md)
{
	gtk_html_stream_printf (md->stream, "<img hspace=10 vspace=10 src=\"%s\">",
				get_cid (part, md));
	return TRUE;
}

static gboolean
handle_multipart_mixed (CamelMimePart *part, const char *mime_type,
			MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	CamelMultipart *mp;
	int i, nparts;
	gboolean output = FALSE;
	
	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	mp = CAMEL_MULTIPART (wrapper);

	nparts = camel_multipart_get_number (mp);	
	for (i = 0; i < nparts; i++) {
		if (i != 0 && output)
			write_hr (md);

		part = camel_multipart_get_part (mp, i);
		
		output = format_mime_part (part, md);
	}

	return TRUE;
}

static gboolean
handle_multipart_encrypted (CamelMimePart *part, const char *mime_type,
			    MailDisplay *md)
{
	CamelDataWrapper *wrapper;
	CamelMimePart *mime_part;
	CamelException ex;
	
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
	
	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	
	/* Currently we only handle RFC2015-style PGP encryption. */
	if (!camel_pgp_mime_is_rfc2015_encrypted (part))
		return handle_multipart_mixed (part, mime_type, md);
	
	camel_exception_init (&ex);
	mime_part = mail_crypto_pgp_mime_part_decrypt (part, &ex);
	
	if (camel_exception_is_set (&ex)) {
		char *error;
		
		error = e_utf8_from_locale_string (camel_exception_get_description (&ex));
		
		mail_error_printf (md->html, md->stream, "\n%s\n", error);
		g_free (error);
		
		camel_exception_clear (&ex);
		return TRUE;
	} else {
		/* replace the encrypted part with the decrypted part */
		camel_medium_set_content_object (CAMEL_MEDIUM (part),
						 camel_medium_get_content_object (CAMEL_MEDIUM (mime_part)));
		camel_object_unref (CAMEL_OBJECT (mime_part));
		
		/* and continue on our merry way... */
		return format_mime_part (part, md);
	}
}

static gboolean
handle_multipart_signed (CamelMimePart *part, const char *mime_type,
			 MailDisplay *md)
{
	CamelMimePart *subpart;
	CamelDataWrapper *wrapper;
	CamelMultipart *mp;
	gboolean output = FALSE;
	int nparts, i;
	
	wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (part));
	
	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	
	/* Display all the subparts (there should be only 1)
	 * except the signature (last part).
	 */
	mp = CAMEL_MULTIPART (wrapper);
	
	nparts = camel_multipart_get_number (mp);
	for (i = 0; i < nparts - 1; i++) {
		if (i != 0 && output)
			write_hr (md);
		
		subpart = camel_multipart_get_part (mp, i);
		
		output = format_mime_part (subpart, md);
	}
	
	subpart = camel_multipart_get_part (mp, i);
	mail_part_set_default_displayed_inline (subpart, md, FALSE);
	
	if (!mail_part_is_displayed_inline (subpart, md)) {
		char *url;
		
		/* Write out the click-for-info object */
		url = g_strdup_printf ("signature:%p/%lu", subpart,
				       (unsigned long)time (NULL));
		gtk_html_stream_printf (md->stream,
					"<br><table cellspacing=0 cellpadding=0>"
					"<tr><td><table width=10 cellspacing=0 cellpadding=0>"
					"<tr><td></td></tr></table></td>"
					"<td><object classid=\"%s\"></object></td>"
					"<td><table width=3 cellspacing=0 cellpadding=0>"
					"<tr><td></td></tr></table></td>"
					"<td><font size=-1>", url);
		add_url ("part_urls", url, subpart, md);
		
		mail_html_write (md->html, md->stream, 
				 U_("This message is digitally signed. "
				    "Click the lock icon for more information."));
		
		mail_html_write (md->html, md->stream,
				 "</font></td></tr><tr><td height=10><table height=10 cellspacing=0 cellpadding=0>"
				 "<tr><td></td></tr></table></td></tr></table>\n");
	} else {
		CamelCipherValidity *valid = NULL;
		CamelException ex;
		const char *message = NULL;
		gboolean good = FALSE;
		
		/* Write out the verification results */
		camel_exception_init (&ex);
		if (camel_pgp_mime_is_rfc2015_signed (part)) {
			valid = mail_crypto_pgp_mime_part_verify (part, &ex);
			if (!valid) {
				message = camel_exception_get_description (&ex);
			} else {
				good = camel_cipher_validity_get_valid (valid);
				message = camel_cipher_validity_get_description (valid);
			}
		} else
			message = U_("Evolution does not recognize this type of signed message.");
		
		if (good) {
			gtk_html_stream_printf (md->stream,
						"<table><tr valign=top>"
						"<td><img src=\"%s\"></td>"
						"<td>%s<br><br>",
						get_url_for_icon (EVOLUTION_ICONSDIR "/pgp-signature-ok.png", md),
						U_("This message is digitally signed and "
						   "has been found to be authentic."));
		} else {
			gtk_html_stream_printf (md->stream,
						"<table><tr valign=top>"
						"<td><img src=\"%s\"></td>"
						"<td>%s<br><br>",
						get_url_for_icon (EVOLUTION_ICONSDIR "/pgp-signature-bad.png", md),
						U_("This message is digitally signed but can "
						   "not be proven to be authentic."));
		}
		
		if (message) {
			gtk_html_stream_printf (md->stream, "<font size=-1 %s>",
						good ? "" : "color=red");
			mail_text_write (md->html, md->stream, message);
			mail_html_write (md->html, md->stream, "</font>");
		}
		
		mail_html_write (md->html, md->stream, "</td></tr></table>");
		camel_exception_clear (&ex);
		camel_cipher_validity_free (valid);
	}
	
	return TRUE;
}

/* As seen in RFC 2387! */
static gboolean
handle_multipart_related (CamelMimePart *part, const char *mime_type,
			  MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	CamelMultipart *mp;
	CamelMimePart *body_part, *display_part = NULL;
	CamelContentType *content_type;
	const char *start;
	int i, nparts;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	
	mp = CAMEL_MULTIPART (wrapper);
	nparts = camel_multipart_get_number (mp);	

	content_type = camel_mime_part_get_content_type (part);
	start = header_content_type_param (content_type, "start");
	if (start) {
		int len;

		/* The "start" parameter includes <>s, which Content-Id
		 * does not.
		 */
		len = strlen (start) - 2;

		for (i = 0; i < nparts; i++) {
			const char *cid;

			body_part = camel_multipart_get_part (mp, i);
			cid = camel_mime_part_get_content_id (body_part);

			if (!strncmp (cid, start + 1, len) &&
			    strlen (cid) == len) {
				display_part = body_part;
				break;
			}
		}
	} else {
		/* No start parameter, so it defaults to the first part. */
		display_part = camel_multipart_get_part (mp, 0);
	}

	if (!display_part) {
		/* Oops. Hrmph. */
		return handle_multipart_mixed (part, mime_type, md);
	}

	/* Record the Content-ID/Content-Location of any non-displayed parts. */
	for (i = 0; i < nparts; i++) {
		body_part = camel_multipart_get_part (mp, i);
		if (body_part == display_part)
			continue;

		get_cid (body_part, md);
		get_location (body_part, md);
	}

	/* Now, display the displayed part. */
	return format_mime_part (display_part, md);
}

/* RFC 2046 says "display the last part that you are able to display". */
static CamelMimePart *
find_preferred_alternative (CamelMultipart *multipart, gboolean want_plain)
{
	int i, nparts;
	CamelMimePart *preferred_part = NULL;
	MailMimeHandler *handler;

	nparts = camel_multipart_get_number (multipart);
	for (i = 0; i < nparts; i++) {
		CamelMimePart *part = camel_multipart_get_part (multipart, i);
		CamelContentType *type = camel_mime_part_get_content_type (part);
		char *mime_type = header_content_type_simple (type);

		g_strdown (mime_type);
		if (want_plain && !strcmp (mime_type, "text/plain"))
			return part;
		handler = mail_lookup_handler (mime_type);
		if (handler && (!preferred_part || !handler->generic))
			preferred_part = part;
		g_free (mime_type);
	}

	return preferred_part;
}

static gboolean
handle_multipart_alternative (CamelMimePart *part, const char *mime_type,
			      MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	CamelMultipart *multipart;
	CamelMimePart *mime_part;
	
	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	
	multipart = CAMEL_MULTIPART (wrapper);
	
	mime_part = find_preferred_alternative (multipart, FALSE);
	if (mime_part)
		return format_mime_part (mime_part, md);
	else
		return handle_multipart_mixed (part, mime_type, md);
}

/* RFC 1740 */
static gboolean
handle_multipart_appledouble (CamelMimePart *part, const char *mime_type,
			      MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	CamelMultipart *multipart;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (wrapper), FALSE);
	
	multipart = CAMEL_MULTIPART (wrapper);

	/* The first part is application/applefile and is not useful
	 * to us. The second part _may_ be displayable data. Most
	 * likely it's application/octet-stream though.
	 */
	part = camel_multipart_get_part (multipart, 1);
	return format_mime_part (part, md);
}

static gboolean
handle_message_rfc822 (CamelMimePart *part, const char *mime_type,
		       MailDisplay *md)
{
	CamelDataWrapper *wrapper =
		camel_medium_get_content_object (CAMEL_MEDIUM (part));
	
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (wrapper), FALSE);
	
	mail_html_write (md->html, md->stream, "<blockquote>");
	mail_format_mime_message (CAMEL_MIME_MESSAGE (wrapper), md);
	mail_html_write (md->html, md->stream, "</blockquote>");
	
	return TRUE;
}

static gboolean
handle_message_external_body (CamelMimePart *part, const char *mime_type,
			      MailDisplay *md)
{
	CamelContentType *type;
	const char *access_type;
	char *url = NULL, *desc = NULL;
	char *fmt;
	
	type = camel_mime_part_get_content_type (part);
	access_type = header_content_type_param (type, "access-type");
	if (!access_type)
		goto fallback;
	
	if (!g_strcasecmp (access_type, "ftp") ||
	    !g_strcasecmp (access_type, "anon-ftp")) {
		const char *name, *site, *dir, *mode, *ftype;
		char *path;
		
		name = header_content_type_param (type, "name");
		site = header_content_type_param (type, "site");
		if (name == NULL || site == NULL)
			goto fallback;
		dir = header_content_type_param (type, "directory");
		mode = header_content_type_param (type, "mode");
		
		/* Generate the path. */
		if (dir) {
			const char *p = dir + strlen (dir);
			
			path = g_strdup_printf ("%s%s%s%s",
						*dir == '/' ? "" : "/",
						dir,
						*p == '/' ? "" : "/",
						name);
		} else {
			path = g_strdup_printf ("%s%s",
						*name == '/' ? "" : "/",
						name);
		}
		
		if (mode && *mode == 'A')
			ftype = ";type=A";
		else if (mode && *mode == 'I')
			ftype = ";type=I";
		else
			ftype = "";
		
		url = g_strdup_printf ("ftp://%s%s%s", site, path, ftype);
		g_free (path);
		fmt = e_utf8_from_locale_string (_("Pointer to FTP site (%s)"));
		desc = g_strdup_printf (fmt, url);
		g_free (fmt);
	} else if (!g_strcasecmp (access_type, "local-file")) {
		const char *name, *site;
		
		name = header_content_type_param (type, "name");
		if (name == NULL)
			goto fallback;
		site = header_content_type_param (type, "site");
		
		url = g_strdup_printf ("file://%s%s", *name == '/' ? "" : "/",
				       name);
		if (site) {
			fmt = e_utf8_from_locale_string (_("Pointer to local file (%s) "
							   "valid at site \"%s\""));
			desc = g_strdup_printf (fmt, name, site);
			g_free (fmt);
		} else {
			fmt = e_utf8_from_locale_string (_("Pointer to local file (%s)"));
			desc = g_strdup_printf (fmt, name);
			g_free (fmt);
		}
	} else if (!g_strcasecmp (access_type, "URL")) {
		const char *urlparam;
		char *s, *d;
		
		/* RFC 2017 */
		
		urlparam = header_content_type_param (type, "url");
		if (urlparam == NULL)
			goto fallback;
		
		/* For obscure MIMEy reasons, the URL may be split into
		 * multiple words, and needs to be rejoined. (The URL
		 * must have any real whitespace %-encoded, so we just
		 * get rid of all of it.
		 */
		url = g_strdup (urlparam);
		s = d = url;
		
		while (*s) {
			if (!isspace ((unsigned char)*s))
				*d++ = *s;
			s++;
		}
		*d = *s;
		
		fmt = e_utf8_from_locale_string (_("Pointer to remote data (%s)"));
		desc = g_strdup_printf (fmt, url);
		g_free (fmt);
	}
	
 fallback:
	if (!desc) {
		if (access_type) {
			fmt = e_utf8_from_locale_string (_("Pointer to unknown external data "
							   "(\"%s\" type)"));
			desc = g_strdup_printf (fmt, access_type);
			g_free (fmt);
		} else
			desc = e_utf8_from_locale_string (_("Malformed external-body part."));
	}
	
#if 0 /* FIXME */
	handle_mystery (part, md, url, "gnome-globe.png", desc,
			url ? "open it in a browser" : NULL);
#endif
	
	g_free (desc);
	g_free (url);
	return TRUE;
}

static gboolean
handle_via_bonobo (CamelMimePart *part, const char *mime_type,
		   MailDisplay *md)
{
	gtk_html_stream_printf (md->stream,
				"<object classid=\"%s\" type=\"%s\"></object>",
				get_cid (part, md), mime_type);
	return TRUE;
}

/**
 * mail_get_message_rfc822:
 * @message: the message
 * @want_plain: whether the caller prefers plain to html
 * @cite: whether or not to cite the message text
 *
 * See mail_get_message_body() below for more details.
 *
 * Return value: an HTML string representing the text parts of @message.
 **/
static char *
mail_get_message_rfc822 (CamelMimeMessage *message, gboolean want_plain, gboolean cite)
{
	CamelDataWrapper *contents;
	GString *retval;
	const CamelInternetAddress *cia;
	char *text, *citation, *buf, *html;
	time_t date_val;
	int offset;

	contents = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	text = mail_get_message_body (contents, want_plain, cite);
	if (!text)
		text = g_strdup ("");
	citation = cite ? "&gt; " : "";
	retval = g_string_new (NULL);

	/* Kludge: if text starts with "<PRE>", wrap it around the
	 * headers too so we won't get a blank line between them for the
	 * <P> to <PRE> switch.
	 */
	if (!g_strncasecmp (text, "<pre>", 5))
		g_string_sprintfa (retval, "<PRE>");

	/* create credits */
	cia = camel_mime_message_get_from (message);
	buf = camel_address_format (CAMEL_ADDRESS (cia));
	if (buf) {
		html = e_text_to_html (buf, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
		g_string_sprintfa (retval, "%s<b>From:</b> %s<br>",
				   citation, html);
		g_free (html);
		g_free (buf);
	}

	cia = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	buf = camel_address_format (CAMEL_ADDRESS (cia));
	if (buf) {
		html = e_text_to_html (buf, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
		g_string_sprintfa (retval, "%s<b>To:</b> %s<br>",
				   citation, html);
		g_free (html);
		g_free (buf);
	}

	cia = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_CC);
	buf = camel_address_format (CAMEL_ADDRESS (cia));
	if (buf) {
		html = e_text_to_html (buf, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
		g_string_sprintfa (retval, "%s<b>Cc:</b> %s<br>",
				   citation, html);
		g_free (html);
		g_free (buf);
	}

	buf = (char *) camel_mime_message_get_subject (message);
	if (buf) {
		html = e_text_to_html (buf, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
		g_string_sprintfa (retval, "%s<b>Subject:</b> %s<br>",
				   citation, html);
		g_free (html);
	}

	date_val = camel_mime_message_get_date (message, &offset);
	buf = header_format_date (date_val, offset);
	html = e_text_to_html (buf, E_TEXT_TO_HTML_CONVERT_NL | E_TEXT_TO_HTML_CONVERT_URLS);
	g_string_sprintfa (retval, "%s<b>Date:</b> %s<br>", citation, html);
	g_free (html);

	if (!g_strncasecmp (text, "<pre>", 5))
		g_string_sprintfa (retval, "%s<br>%s", citation, text + 5);
	else
		g_string_sprintfa (retval, "%s<br>%s", citation, text);
	g_free (text);

	buf = retval->str;
	g_string_free (retval, FALSE);
	return buf;
}

/**
 * mail_get_message_body:
 * @data: the message or mime part content
 * @want_plain: whether the caller prefers plain to html
 * @cite: whether or not to cite the message text
 *
 * This creates an HTML string representing @data. If @want_plain is %TRUE,
 * it will be an HTML string that looks like a text/plain representation
 * of @data (but it will still be HTML).
 *
 * If @cite is %TRUE, the message will be cited as a reply, using "> "s.
 *
 * Return value: the HTML string, which the caller must free, or
 * %NULL if @data doesn't include any data which should be forwarded or
 * replied to.
 **/
char *
mail_get_message_body (CamelDataWrapper *data, gboolean want_plain, gboolean cite)
{
	CamelContentType *mime_type;
	char *subtext, *old, *div, *text = NULL;
	GByteArray *bytes = NULL;
	CamelMimePart *subpart;
	CamelMultipart *mp;
	int i, nparts;
	
	mime_type = camel_data_wrapper_get_mime_type_field (data);
	
	/* If it is message/rfc822 or message/news, extract the
	 * important headers and recursively process the body.
	 */
	if (header_content_type_is (mime_type, "message", "rfc822") ||
	    header_content_type_is (mime_type, "message", "news"))
		return mail_get_message_rfc822 (CAMEL_MIME_MESSAGE (data), want_plain, cite);
	
	/* If it's a vcard or icalendar, ignore it. */
	if (header_content_type_is (mime_type, "text", "x-vcard") ||
	    header_content_type_is (mime_type, "text", "calendar"))
		return NULL;
	
	/* Get the body data for other text/ or message/ types */
	if (header_content_type_is (mime_type, "text", "*") ||
	    header_content_type_is (mime_type, "message", "*")) {
		bytes = mail_format_get_data_wrapper_text (data, NULL);
		
		if (bytes) {
			g_byte_array_append (bytes, "", 1);
			text = bytes->data;
			g_byte_array_free (bytes, FALSE);
		}
		
		if (text && !header_content_type_is (mime_type, "text", "html")) {
			char *html;
			
			html = e_text_to_html (text, E_TEXT_TO_HTML_PRE | (cite ? E_TEXT_TO_HTML_CITE : 0));
			g_free (text);
			text = html;
		}
		return text;
	}
	
	/* If it's not message and it's not text, and it's not
	 * multipart, we don't want to deal with it.
	 */
	if (!header_content_type_is (mime_type, "multipart", "*"))
		return NULL;
	
	mp = CAMEL_MULTIPART (data);
	
	if (header_content_type_is (mime_type, "multipart", "alternative")) {
		/* Pick our favorite alternative and reply to it. */
		
		subpart = find_preferred_alternative (mp, want_plain);
		if (!subpart)
			return NULL;
		
		data = camel_medium_get_content_object (CAMEL_MEDIUM (subpart));
		return mail_get_message_body (data, want_plain, cite);
	}
	
	/* Otherwise, concatenate all the parts that we can. */
	if (want_plain) {
		if (cite)
			div = "<br>\n&gt; ----<br>\n&gt; <br>\n";
		else
			div = "<br>\n----<br>\n<br>\n";
	} else
		div = "<br><hr><br>";
	
	nparts = camel_multipart_get_number (mp);
	for (i = 0; i < nparts; i++) {
		subpart = camel_multipart_get_part (mp, i);
		
		data = camel_medium_get_content_object (CAMEL_MEDIUM (subpart));
		subtext = mail_get_message_body (data, want_plain, cite);
		if (!subtext)
			continue;
		
		if (text) {
			old = text;
			text = g_strdup_printf ("%s%s%s", old, div, subtext);
			g_free (subtext);
			g_free (old);
		} else
			text = subtext;
	}
	
	return text;
}
