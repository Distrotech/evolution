/*
 * e-mail-parser-message-rfc822.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-format-extensions.h"

#include <glib/gi18n-lib.h>
#include <glib-object.h>

#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-parser.h>
#include <em-format/e-mail-part-list.h>
#include <em-format/e-mail-part-utils.h>
#include <e-util/e-util.h>

#include <camel/camel.h>

#include <string.h>

typedef struct _EMailParserMessageRFC822 {
	GObject parent;
} EMailParserMessageRFC822;

typedef struct _EMailParserMessageRFC822Class {
	GObjectClass parent_class;
} EMailParserMessageRFC822Class;

static void e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface);
static void e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface);

G_DEFINE_TYPE_EXTENDED (
	EMailParserMessageRFC822,
	e_mail_parser_message_rfc822,
	G_TYPE_OBJECT,
	0,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_EXTENSION,
		e_mail_parser_mail_extension_interface_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_PARSER_EXTENSION,
		e_mail_parser_parser_extension_interface_init));

static const gchar* parser_mime_types[] = { "message/rfc822",
					    "message/news",
					    "message/*",
					    NULL };

static GSList *
empe_msg_rfc822_parse (EMailParserExtension *extension,
                       EMailParser *eparser,
                       CamelMimePart *part,
                       GString *part_id,
                       GCancellable *cancellable)
{
	GSList *parts = NULL;
	EMailPart *mail_part;
	gint len;
	CamelMimePart *message;
	CamelDataWrapper *dw;
	CamelStream *new_stream;
	CamelMimeParser *mime_parser;
	CamelContentType *ct;

	len = part_id->len;
	g_string_append (part_id, ".rfc822");

        /* Create an empty PURI that will represent start of the RFC message */
	mail_part = e_mail_part_new (part, part_id->str);
	mail_part->mime_type = g_strdup ("message/rfc822");
	parts = g_slist_append (NULL, mail_part);

	/* Sometime the _actual_ message is encapsulated in another CamelMimePart,
	 * sometimes the CamelMimePart actually represents the RFC822 message */
	ct = camel_mime_part_get_content_type (part);
	if (camel_content_type_is (ct, "message", "rfc822")) {
		new_stream = camel_stream_mem_new ();
		mime_parser = camel_mime_parser_new ();
		message = (CamelMimePart *) camel_mime_message_new ();

		dw = camel_medium_get_content (CAMEL_MEDIUM (part));
		camel_data_wrapper_decode_to_stream_sync (
			dw, new_stream, cancellable, NULL);
		g_seekable_seek (
			G_SEEKABLE (new_stream), 0, G_SEEK_SET, cancellable, NULL);
		camel_mime_parser_init_with_stream (
			mime_parser, new_stream, NULL);
		camel_mime_part_construct_from_parser_sync (
			message, mime_parser, cancellable, NULL);

		g_object_unref (mime_parser);
		g_object_unref (new_stream);
	} else {
		message = g_object_ref (part);
	}

	parts = g_slist_concat (parts, e_mail_parser_parse_part_as (
						eparser, message, part_id,
						"application/vnd.evolution.message",
						cancellable));

	g_object_unref (message);

        /* Add another generic EMailPart that represents end of the RFC message.
         * The em_format_write() function will skip all parts between the ".rfc822"
         * part and ".rfc822.end" part as they will be rendered in an <iframe> */
	g_string_append (part_id, ".end");
	mail_part = e_mail_part_new (message, part_id->str);
	mail_part->is_hidden = TRUE;
	parts = g_slist_append (parts, mail_part);
	g_string_truncate (part_id, len);

	if (e_mail_part_is_attachment (message)) {
		return e_mail_parser_wrap_as_attachment (
			eparser, message, parts, part_id, cancellable);
	}

	return parts;
}

static guint32
empe_msg_rfc822_get_flags (EMailParserExtension *extension)
{
	return E_MAIL_PARSER_EXTENSION_INLINE |
		E_MAIL_PARSER_EXTENSION_COMPOUND_TYPE;
}

static const gchar **
empe_msg_rfc822_mime_types (EMailExtension *extension)
{
	return parser_mime_types;
}

static void
e_mail_parser_message_rfc822_class_init (EMailParserMessageRFC822Class *klass)
{
}

static void
e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface)
{
	iface->parse = empe_msg_rfc822_parse;
	iface->get_flags = empe_msg_rfc822_get_flags;
}

static void
e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface)
{
	iface->mime_types = empe_msg_rfc822_mime_types;
}

static void
e_mail_parser_message_rfc822_init (EMailParserMessageRFC822 *parser)
{

}
