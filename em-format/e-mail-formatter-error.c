/*
 * e-mail-formatter-error.c
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

#include <e-util/e-util.h>

#include "e-mail-formatter-extension.h"

typedef EMailFormatterExtension EMailFormatterError;
typedef EMailFormatterExtensionClass EMailFormatterErrorClass;

GType e_mail_formatter_error_get_type (void);

G_DEFINE_TYPE (
	EMailFormatterError,
	e_mail_formatter_error,
	E_TYPE_MAIL_FORMATTER_EXTENSION)

static const gchar *formatter_mime_types[] = {
	"application/vnd.evolution.error",
	NULL
};

static gboolean
emfe_error_format (EMailFormatterExtension *extension,
                   EMailFormatter *formatter,
                   EMailFormatterContext *context,
                   EMailPart *part,
                   CamelStream *stream,
                   GCancellable *cancellable)
{
	CamelStream *filtered_stream;
	CamelMimeFilter *filter;
	CamelMimePart *mime_part;
	CamelDataWrapper *dw;
	gchar *html;

	mime_part = e_mail_part_ref_mime_part (part);
	dw = camel_medium_get_content (CAMEL_MEDIUM (mime_part));

	html = g_strdup_printf (
		"<div class=\"part-container -e-mail-formatter-frame-color "
		"-e-mail-formatter-body-color -e-web-view-text-color\">"
		"<div class=\"part-container-inner-margin pre\">\n"
		"<table border=\"0\" cellspacing=\"10\" "
		"cellpadding=\"0\" width=\"100%%\">\n"
		"<tr valign=\"top\"><td width=50>"
		"<img src=\"gtk-stock://%s/?size=%d\" /></td>\n"
		"<td style=\"color: red;\">",
		GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_DIALOG);

	camel_stream_write_string (stream, html, cancellable, NULL);
	g_free (html);

	filtered_stream = camel_stream_filter_new (stream);
	filter = camel_mime_filter_tohtml_new (
		CAMEL_MIME_FILTER_TOHTML_CONVERT_NL |
		CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filtered_stream), filter);
	g_object_unref (filter);

	camel_data_wrapper_decode_to_stream_sync (dw, filtered_stream, cancellable, NULL);
	camel_stream_flush (filtered_stream, cancellable, NULL);
	g_object_unref (filtered_stream);

	camel_stream_write_string (
		stream,
		"</td>\n"
		"</tr>\n"
		"</table>\n"
		"</div>\n"
		"</div>",
		cancellable, NULL);

	g_object_unref (mime_part);

	return TRUE;
}

static void
e_mail_formatter_error_class_init (EMailFormatterExtensionClass *class)
{
	class->mime_types = formatter_mime_types;
	class->priority = G_PRIORITY_LOW;
	class->format = emfe_error_format;
}

static void
e_mail_formatter_error_init (EMailFormatterExtension *extension)
{
}
