/*
 * evolution-source.c
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

#include <em-format/e-mail-formatter-extension.h>
#include <em-format/e-mail-formatter.h>
#include <em-format/e-mail-inline-filter.h>
#include <e-util/e-util.h>

#include <glib/gi18n-lib.h>
#include <camel/camel.h>

typedef struct _EMailFormatterSource {
	GObject parent;
} EMailFormatterSource;

typedef struct _EMailFormatterSourceClass {
	GObjectClass parent_class;
} EMailFormatterSourceClass;

static void e_mail_formatter_formatter_extension_interface_init (EMailFormatterExtensionInterface *iface);
static void e_mail_formatter_mail_extension_interface_init (EMailExtensionInterface *iface);

G_DEFINE_TYPE_EXTENDED (
	EMailFormatterSource,
	e_mail_formatter_source,
	G_TYPE_OBJECT,
	0,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_EXTENSION,
		e_mail_formatter_mail_extension_interface_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_FORMATTER_EXTENSION,
		e_mail_formatter_formatter_extension_interface_init)
)

static const gchar *formatter_mime_types[] = { "application/vnd.evolution.source", NULL };

static gboolean
emfe_source_format (EMailFormatterExtension *extension,
                    EMailFormatter *formatter,
                    EMailFormatterContext *context,
                    EMailPart *part,
                    CamelStream *stream,
                    GCancellable *cancellable)
{
	GString *buffer;
	CamelStream *filtered_stream;
	CamelMimeFilter *filter;
	CamelDataWrapper *dw = (CamelDataWrapper *) part->part;

	filtered_stream = camel_stream_filter_new (stream);

	filter = camel_mime_filter_tohtml_new (
		CAMEL_MIME_FILTER_TOHTML_CONVERT_NL |
		CAMEL_MIME_FILTER_TOHTML_CONVERT_SPACES |
		CAMEL_MIME_FILTER_TOHTML_PRESERVE_8BIT, 0);
	camel_stream_filter_add (
		CAMEL_STREAM_FILTER (filtered_stream), filter);
	g_object_unref (filter);

	buffer = g_string_new ("");

	if (CAMEL_IS_MIME_MESSAGE (part->part)) {
		g_string_append_printf (
			buffer,
			"<div class=\"part-container\" "
			"style=\"border: 0; background: #%06x; color: #%06x;\" >",
			e_color_to_value ((GdkColor *)
				e_mail_formatter_get_color (
					formatter, E_MAIL_FORMATTER_COLOR_BODY)),
			e_color_to_value ((GdkColor *)
				e_mail_formatter_get_color (
					formatter, E_MAIL_FORMATTER_COLOR_TEXT)));
	} else {
		g_string_append_printf (
			buffer,
			"<div class=\"part-container\" "
			"style=\"border-color: #%06x; background: #%06x; color: #%06x;\">"
			"<div class=\"part-container-inner-margin pre\">\n",
			e_color_to_value ((GdkColor *)
				e_mail_formatter_get_color (
					formatter, E_MAIL_FORMATTER_COLOR_FRAME)),
			e_color_to_value ((GdkColor *)
				e_mail_formatter_get_color (
					formatter, E_MAIL_FORMATTER_COLOR_BODY)),
			e_color_to_value ((GdkColor *)
				e_mail_formatter_get_color (
					formatter, E_MAIL_FORMATTER_COLOR_TEXT)));
	}

	camel_stream_write_string (
		stream, buffer->str, cancellable, NULL);
	camel_stream_write_string (
		stream, "<code class=\"pre\">", cancellable, NULL);

	camel_data_wrapper_write_to_stream_sync (dw, filtered_stream,
		cancellable, NULL);
	camel_stream_flush (filtered_stream, cancellable, NULL);
	g_object_unref (filtered_stream);

	camel_stream_write_string (
		stream, "</code>", cancellable, NULL);

	g_string_free (buffer, TRUE);

	if (CAMEL_IS_MIME_MESSAGE (part->part)) {
		camel_stream_write_string (stream, "</div>", cancellable, NULL);
	} else {
		camel_stream_write_string (stream, "</div></div>", cancellable, NULL);
	}

	return TRUE;
}

static const gchar *
emfe_source_get_display_name (EMailFormatterExtension *extension)
{
	return _("Source");
}

static const gchar *
emfe_source_get_description (EMailFormatterExtension *extension)
{
	return _("Display source of a MIME part");
}

static const gchar **
emfe_source_mime_types (EMailExtension *extension)
{
	return formatter_mime_types;
}

static void
e_mail_formatter_source_class_init (EMailFormatterSourceClass *class)
{
}

static void
e_mail_formatter_formatter_extension_interface_init (EMailFormatterExtensionInterface *iface)
{
	iface->format = emfe_source_format;
	iface->get_display_name = emfe_source_get_display_name;
	iface->get_description = emfe_source_get_description;
}

static void
e_mail_formatter_mail_extension_interface_init (EMailExtensionInterface *iface)
{
	iface->mime_types = emfe_source_mime_types;
}

static void
e_mail_formatter_source_init (EMailFormatterSource *formatter)
{

}
