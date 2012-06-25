/*
 * e-mail-parser-attachment-bar.c
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
#include "e-mail-part-attachment-bar.h"

#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-parser.h>
#include <e-util/e-util.h>

#include <widgets/misc/e-attachment-bar.h>

#include <camel/camel.h>

static void
mail_part_attachment_bar_free (EMailPart *part)
{
	EMailPartAttachmentBar *empab = (EMailPartAttachmentBar *) part;

	g_clear_object (&empab->store);
}

/******************************************************************************/

typedef struct _EMailParserAttachmentBar {
	GObject parent;
} EMailParserAttachmentBar;

typedef struct _EMailParserAttachmentBarClass {
	GObjectClass parent_class;
} EMailParserAttachmentBarClass;

static void e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface);
static void e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface);

G_DEFINE_TYPE_EXTENDED (
	EMailParserAttachmentBar,
	e_mail_parser_attachment_bar,
	G_TYPE_OBJECT,
	0,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_EXTENSION,
		e_mail_parser_mail_extension_interface_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_MAIL_PARSER_EXTENSION,
		e_mail_parser_parser_extension_interface_init))

static const gchar *parser_mime_types[] = { "application/vnd.evolution.widget.attachment-bar", NULL };

static GSList *
empe_attachment_bar_parse (EMailParserExtension *extension,
                           EMailParser *parser,
                           CamelMimePart *part,
                           GString *part_id,
                           GCancellable *cancellable)
{
	EMailPartAttachmentBar *empab;
	gint len;

	len = part_id->len;
	g_string_append (part_id, ".attachment-bar");
	empab = (EMailPartAttachmentBar *) e_mail_part_subclass_new (
			part, part_id->str, sizeof (EMailPartAttachmentBar),
			(GFreeFunc) mail_part_attachment_bar_free);
	empab->parent.mime_type = g_strdup ("application/vnd.evolution.widget.attachment-bar");
	empab->store = E_ATTACHMENT_STORE (e_attachment_store_new ());
	g_string_truncate (part_id, len);

	return g_slist_append (NULL, empab);
}

static const gchar **
empe_attachment_bar_mime_types (EMailExtension *extension)
{
	return parser_mime_types;
}

static void
e_mail_parser_attachment_bar_class_init (EMailParserAttachmentBarClass *klass)
{
}

static void
e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface)
{
	iface->parse = empe_attachment_bar_parse;
}

static void
e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface)
{
	iface->mime_types = empe_attachment_bar_mime_types;
}

static void
e_mail_parser_attachment_bar_init (EMailParserAttachmentBar *parser)
{

}
