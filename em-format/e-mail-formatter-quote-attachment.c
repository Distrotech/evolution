/*
 * e-mail-formatter-qoute-attachment.c
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

#include <glib/gi18n-lib.h>

#include <camel/camel.h>

#include <e-util/e-util.h>

#include "e-mail-formatter-quote.h"
#include "e-mail-part-attachment.h"
#include "e-mail-part-utils.h"

#define d(x)

typedef EMailFormatterExtension EMailFormatterQuoteAttachment;
typedef EMailFormatterExtensionClass EMailFormatterQuoteAttachmentClass;

GType e_mail_formatter_quote_attachment_get_type (void);

G_DEFINE_TYPE (
	EMailFormatterQuoteAttachment,
	e_mail_formatter_quote_attachment,
	E_TYPE_MAIL_FORMATTER_QUOTE_EXTENSION)

static const gchar *formatter_mime_types[] = {
	E_MAIL_PART_ATTACHMENT_MIME_TYPE,
	NULL
};

static gboolean
emfqe_attachment_format (EMailFormatterExtension *extension,
                         EMailFormatter *formatter,
                         EMailFormatterContext *context,
                         EMailPart *part,
                         CamelStream *stream,
                         GCancellable *cancellable)
{
	gchar *text, *html;
	EMailPartAttachment *empa;
	EMailPart *attachment_view_part;
	CamelMimeFilterToHTMLFlags text_format_flags;
	CamelMimePart *mime_part;

	empa = E_MAIL_PART_ATTACHMENT (part);

	if (!empa->attachment_view_part_id)
		return FALSE;

	attachment_view_part = e_mail_part_list_ref_part (
		context->part_list, empa->attachment_view_part_id);
	if (attachment_view_part == NULL)
		return FALSE;

	camel_stream_write_string (stream, "<br><br>", cancellable, NULL);

	text_format_flags =
		e_mail_formatter_get_text_format_flags (formatter);
	mime_part = e_mail_part_ref_mime_part (part);
	text = e_mail_part_describe (
		mime_part,
		empa ? empa->snoop_mime_type :
		e_mail_part_get_mime_type (part));
	g_object_unref (mime_part);

	html = camel_text_to_html (
		text,
		text_format_flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS,
		0);
	camel_stream_write_string (stream, html, cancellable, NULL);
	camel_stream_write_string (stream, "<br>", cancellable, NULL);
	g_free (html);
	g_free (text);

	camel_stream_write_string (
		stream,
		"<!--+GtkHTML:<DATA class=\"ClueFlow\" "
		"key=\"orig\" value=\"1\">-->\n"
		"<blockquote type=cite>\n", cancellable, NULL);

	e_mail_formatter_format_as (
		formatter, context, attachment_view_part,
		stream, NULL, cancellable);

	camel_stream_write_string (
		stream,
		"</blockquote><!--+GtkHTML:"
		"<DATA class=\"ClueFlow\" clear=\"orig\">-->",
		cancellable, NULL);

	g_object_unref (attachment_view_part);

	return TRUE;
}

static void
e_mail_formatter_quote_attachment_class_init (EMailFormatterExtensionClass *class)
{
	class->mime_types = formatter_mime_types;
	class->priority = G_PRIORITY_HIGH;
	class->format = emfqe_attachment_format;
}

static void
e_mail_formatter_quote_attachment_init (EMailFormatterExtension *extension)
{
}
