/*
 * e-mail-parser-vcard-inline.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "e-mail-parser-vcard-inline.h"
#include "e-mail-part-vcard-inline.h"

#include <camel/camel.h>

#include <em-format/e-mail-extension-registry.h>
#include <em-format/e-mail-parser-extension.h>
#include <em-format/e-mail-part.h>
#include <em-format/e-mail-part-utils.h>
#include <em-format/e-mail-formatter.h>

#include <libebook/libebook.h>
#include <libedataserver/libedataserver.h>
#include <libedataserverui/libedataserverui.h>

#include <shell/e-shell.h>
#include <addressbook/gui/merging/eab-contact-merging.h>
#include <addressbook/util/eab-book-util.h>

#include <libebackend/libebackend.h>

#define d(x)

typedef struct _EMailParserVCardInline {
	EExtension parent;
} EMailParserVCardInline;

typedef struct _EMailParserVCardInlineClass {
	EExtensionClass parent_class;
} EMailParserVCardInlineClass;

GType e_mail_parser_vcard_inline_get_type (void);
static void e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface);
static void e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EMailParserVCardInline,
	e_mail_parser_vcard_inline,
	E_TYPE_EXTENSION,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_MAIL_EXTENSION,
		e_mail_parser_mail_extension_interface_init)
	G_IMPLEMENT_INTERFACE_DYNAMIC (
		E_TYPE_MAIL_PARSER_EXTENSION,
		e_mail_parser_parser_extension_interface_init));

static const gchar* parser_mime_types[] = { "text/vcard", "text/x-vcard",
					    "text/directory", NULL };

static void
mail_part_vcard_inline_free (EMailPart *mail_part)
{
	EMailPartVCardInline *vi_part = (EMailPartVCardInline *) mail_part;

	g_clear_object (&vi_part->contact_display);
	g_clear_object (&vi_part->message_label);
	g_clear_object (&vi_part->formatter);
	g_clear_object (&vi_part->iframe);
	g_clear_object (&vi_part->save_button);
	g_clear_object (&vi_part->toggle_button);
	g_clear_object (&vi_part->folder);

	if (vi_part->message_uid) {
		g_free (vi_part->message_uid);
		vi_part->message_uid = NULL;
	}
}

static void
client_loaded_cb (ESource *source,
                  GAsyncResult *result,
                  GSList *contact_list)
{
	EShell *shell;
	EClient *client = NULL;
	EBookClient *book_client;
	ESourceRegistry *registry;
	GSList *iter;
	GError *error = NULL;

	e_client_utils_open_new_finish (source, result, &client, &error);

	if (error != NULL) {
		g_warn_if_fail (client == NULL);
		g_warning (
			"%s: Failed to open book client: %s",
			G_STRFUNC, error->message);
		g_error_free (error);
		goto exit;
	}

	g_return_if_fail (E_IS_BOOK_CLIENT (client));

	book_client = E_BOOK_CLIENT (client);

	shell = e_shell_get_default ();
	registry = e_shell_get_registry (shell);

	for (iter = contact_list; iter != NULL; iter = iter->next) {
		EContact *contact;

		contact = E_CONTACT (iter->data);
		eab_merging_book_add_contact (
			registry, book_client, contact, NULL, NULL);
	}

	g_object_unref (client);

 exit:
	e_client_util_free_object_slist (contact_list);
}

static void
save_vcard_cb (WebKitDOMEventTarget *button,
               WebKitDOMEvent *event,
               EMailPartVCardInline *vcard_part)
{
	EShell *shell;
	ESource *source;
	ESourceRegistry *registry;
	ESourceSelector *selector;
	GSList *contact_list;
	const gchar *extension_name;
	GtkWidget *dialog;

	shell = e_shell_get_default ();
	registry = e_shell_get_registry (shell);
	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;

	dialog = e_source_selector_dialog_new (NULL, registry, extension_name);

	selector = e_source_selector_dialog_get_selector (
		E_SOURCE_SELECTOR_DIALOG (dialog));

	source = e_source_registry_ref_default_address_book (registry);
	e_source_selector_set_primary_selection (selector, source);
	g_object_unref (source);

	if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK) {
		gtk_widget_destroy (dialog);
		return;
	}

	source = e_source_selector_dialog_peek_primary_selection (
		E_SOURCE_SELECTOR_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	g_return_if_fail (source != NULL);

	contact_list = e_client_util_copy_object_slist (NULL, vcard_part->contact_list);

	e_client_utils_open_new (
		source, E_CLIENT_SOURCE_TYPE_CONTACTS,
		FALSE, NULL, (GAsyncReadyCallback) client_loaded_cb,
		contact_list);
}

static void
display_mode_toggle_cb (WebKitDOMEventTarget *button,
                        WebKitDOMEvent *event,
                        EMailPartVCardInline *vcard_part)
{
	EABContactDisplayMode mode;
	gchar *uri;

	mode = eab_contact_formatter_get_display_mode (vcard_part->formatter);
	if (mode == EAB_CONTACT_DISPLAY_RENDER_NORMAL) {
		mode = EAB_CONTACT_DISPLAY_RENDER_COMPACT;

		webkit_dom_html_element_set_inner_text (
			WEBKIT_DOM_HTML_ELEMENT (button),
			_("Show Full vCard"), NULL);

	} else {
		mode = EAB_CONTACT_DISPLAY_RENDER_NORMAL;

		webkit_dom_html_element_set_inner_text (
			WEBKIT_DOM_HTML_ELEMENT (button),
			_("Show Compact vCard"), NULL);
	}

	eab_contact_formatter_set_display_mode (vcard_part->formatter, mode);

	uri = e_mail_part_build_uri (
		vcard_part->folder, vcard_part->message_uid,
		"part_id", G_TYPE_STRING, vcard_part->parent.id,
		"mode", G_TYPE_INT, E_MAIL_FORMATTER_MODE_RAW, NULL);

	webkit_dom_html_iframe_element_set_src (
		WEBKIT_DOM_HTML_IFRAME_ELEMENT (vcard_part->iframe), uri);

	g_free (uri);
}

static void
bind_dom (EMailPartVCardInline *vcard_part,
          WebKitDOMElement *attachment)
{
	WebKitDOMNodeList *list;
	WebKitDOMElement *iframe, *toggle_button, *save_button;

        /* IFRAME */
	list = webkit_dom_element_get_elements_by_tag_name (attachment, "iframe");
	if (webkit_dom_node_list_get_length (list) != 1)
		return;
	iframe = WEBKIT_DOM_ELEMENT (webkit_dom_node_list_item (list, 0));
	if (vcard_part->iframe)
		g_object_unref (vcard_part->iframe);
	vcard_part->iframe = g_object_ref (iframe);

	/* TOGGLE DISPLAY MODE BUTTON */
	list = webkit_dom_element_get_elements_by_class_name (
		attachment, "org-gnome-vcard-inline-display-mode-button");
	if (webkit_dom_node_list_get_length (list) != 1)
		return;
	toggle_button = WEBKIT_DOM_ELEMENT (webkit_dom_node_list_item (list, 0));
	if (vcard_part->toggle_button)
		g_object_unref (vcard_part->toggle_button);
	vcard_part->toggle_button = g_object_ref (toggle_button);

	/* SAVE TO ADDRESSBOOK BUTTON */
	list = webkit_dom_element_get_elements_by_class_name (
		attachment, "org-gnome-vcard-inline-save-button");
	if (webkit_dom_node_list_get_length (list) != 1)
		return;
	save_button = WEBKIT_DOM_ELEMENT (webkit_dom_node_list_item (list, 0));
	if (vcard_part->save_button)
		g_object_unref (vcard_part->save_button);
	vcard_part->save_button = g_object_ref (save_button);

	webkit_dom_event_target_add_event_listener (
		WEBKIT_DOM_EVENT_TARGET (toggle_button),
		"click", G_CALLBACK (display_mode_toggle_cb),
		FALSE, vcard_part);

	webkit_dom_event_target_add_event_listener (
		WEBKIT_DOM_EVENT_TARGET (save_button),
		"click", G_CALLBACK (save_vcard_cb),
		FALSE, vcard_part);

	/* Bind collapse buttons for contact lists. */
	eab_contact_formatter_bind_dom (
		webkit_dom_html_iframe_element_get_content_document (
			WEBKIT_DOM_HTML_IFRAME_ELEMENT (iframe)));
}

static void
decode_vcard (EMailPartVCardInline *vcard_part,
              CamelMimePart *mime_part)
{
	CamelDataWrapper *data_wrapper;
	CamelMedium *medium;
	CamelStream *stream;
	GSList *contact_list;
	GByteArray *array;
	const gchar *string;
	const guint8 padding[2] = {0};

	array = g_byte_array_new ();
	medium = CAMEL_MEDIUM (mime_part);

	/* Stream takes ownership of the byte array. */
	stream = camel_stream_mem_new_with_byte_array (array);
	data_wrapper = camel_medium_get_content (medium);
	camel_data_wrapper_decode_to_stream_sync (
		data_wrapper, stream, NULL, NULL);

	/* because the result is not NULL-terminated */
	g_byte_array_append (array, padding, 2);

	string = (gchar *) array->data;
	contact_list = eab_contact_list_from_string (string);
	vcard_part->contact_list = contact_list;

	g_object_unref (mime_part);
	g_object_unref (stream);
}

static GSList *
empe_vcard_inline_parse (EMailParserExtension *extension,
                         EMailParser *parser,
                         CamelMimePart *part,
                         GString *part_id,
                         GCancellable *cancellable)
{
	EMailPartVCardInline *vcard_part;
	gint len;

	len = part_id->len;
	g_string_append (part_id, ".org-gnome-vcard-inline-display");

	vcard_part = (EMailPartVCardInline *) e_mail_part_subclass_new (
			part, part_id->str, sizeof (EMailPartVCardInline),
			(GFreeFunc) mail_part_vcard_inline_free);
	vcard_part->parent.mime_type = camel_content_type_simple (
					camel_mime_part_get_content_type (part));
	vcard_part->parent.bind_func = (EMailPartDOMBindFunc) bind_dom;
	vcard_part->parent.is_attachment = TRUE;
	vcard_part->formatter = g_object_new (
					EAB_TYPE_CONTACT_FORMATTER,
				       "display-mode", EAB_CONTACT_DISPLAY_RENDER_COMPACT,
				       "render-maps", FALSE, NULL);
	g_object_ref (part);

	decode_vcard (vcard_part, part);

	g_string_truncate (part_id, len);

	return e_mail_parser_wrap_as_attachment (
			parser, part, g_slist_append (NULL, vcard_part),
			part_id, cancellable);
}

static guint32
empe_vcard_inline_get_flags (EMailParserExtension *extension)
{
	return E_MAIL_PARSER_EXTENSION_INLINE_DISPOSITION;
}

static const gchar **
empe_mime_types (EMailExtension *extension)
{
	return parser_mime_types;
}

void
e_mail_parser_vcard_inline_type_register (GTypeModule *type_module)
{
	e_mail_parser_vcard_inline_register_type (type_module);
}

static void
e_mail_parser_mail_extension_interface_init (EMailExtensionInterface *iface)
{
	iface->mime_types = empe_mime_types;
}

static void
e_mail_parser_parser_extension_interface_init (EMailParserExtensionInterface *iface)
{
	iface->parse = empe_vcard_inline_parse;
	iface->get_flags = empe_vcard_inline_get_flags;
}

static void
e_mail_parser_vcard_inline_constructed (GObject *object)
{
	EExtensible *extensible;
	EMailExtensionRegistry *reg;

	extensible = e_extension_get_extensible (E_EXTENSION (object));
	reg = E_MAIL_EXTENSION_REGISTRY (extensible);

	e_mail_extension_registry_add_extension (reg, E_MAIL_EXTENSION (object));
}

static void
e_mail_parser_vcard_inline_class_init (EMailParserVCardInlineClass *klass)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	e_mail_parser_vcard_inline_parent_class = g_type_class_peek_parent (klass);

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = e_mail_parser_vcard_inline_constructed;

	extension_class = E_EXTENSION_CLASS (klass);
	extension_class->extensible_type = E_TYPE_MAIL_PARSER_EXTENSION_REGISTRY;
}

static void
e_mail_parser_vcard_inline_class_finalize (EMailParserVCardInlineClass *klass)
{

}

static void
e_mail_parser_vcard_inline_init (EMailParserVCardInline *self)
{
}
