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
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "em-format-html-print.h"
#include "em-format-html-display.h"
#include "e-mail-attachment-bar.h"
#include <e-util/e-print.h>
#include <e-util/e-util.h>
#include <widgets/misc/e-attachment-store.h>
#include <libemail-engine/mail-ops.h>

#include "em-format-html-print.h"

static gpointer parent_class = NULL;

struct _EMFormatHTMLPrintPrivate {

	EMFormatHTML *original_formatter;
	EMFormatPURI *top_level_puri;
	GtkPrintOperationAction print_action;

};

G_DEFINE_TYPE (
	EMFormatHTMLPrint, 
	em_format_html_print, 
	EM_TYPE_FORMAT_HTML);

enum {
	PROP_0,
	PROP_ORIGINAL_FORMATTER,
	PROP_PRINT_ACTION
};

static void efhp_write_print_layout	(EMFormat *emf, EMFormatPURI *puri, CamelStream *stream, EMFormatWriterInfo *info, GCancellable *cancellable);

static void
attachment_bar_html (EMFormatPURI *puri,
		     GString *buffer,
		     GCancellable *cancellable)
{
	EAttachmentStore *store;
	EMailAttachmentBar *bar;
	GList *attachments, *iter;
	GtkIconTheme *icon_theme;

	bar = E_MAIL_ATTACHMENT_BAR (
		puri->widget_func (puri->emf, puri, cancellable));
	store = e_mail_attachment_bar_get_store (bar);

	g_string_append_printf (buffer, 
		"<fieldset class=\"attachments\"><legend>%s</legend>",
		_("Attachments"));

	icon_theme = gtk_icon_theme_get_default ();
	attachments = e_attachment_store_get_attachments (store);
	for (iter = attachments; iter != NULL; iter = iter->next) {
		EAttachment *attachment = iter->data;
		GFileInfo *finfo = e_attachment_get_file_info (attachment);
		GIcon *icon;
		GtkIconInfo *icon_info;

		icon = g_file_info_get_icon (finfo);
		if (icon) {
			icon_info = gtk_icon_theme_lookup_by_gicon (icon_theme,
				icon, 48, 0);
		} 

		if (!icon || !icon_info) {
			icon_info = gtk_icon_theme_lookup_icon (icon_theme,
				"gtk-file", 48, 0);
		}

		g_string_append_printf (buffer,
			"<div class=\"attachment\" >" \
			"<img src=\"evo-file://%s\" width=\"64\" height=\"64\" />"
			"<br>%s</div>",
			gtk_icon_info_get_filename (icon_info),
			g_file_info_get_display_name (finfo));
	}

	g_string_append (buffer, "<div style=\"clear: both; width: 100%\"></div></fieldset>");

	g_list_free (attachments);
}


static void
efhp_write_print_layout (EMFormat *emf,
			 EMFormatPURI *puri,
			 CamelStream *stream,
			 EMFormatWriterInfo *info,
			 GCancellable *cancellable)
{
	GList *iter;
	GString *mail_uri;
	EMFormatWriterInfo print_info = {
		EM_FORMAT_WRITE_MODE_PRINTING, FALSE, FALSE, FALSE };

	camel_stream_write_string (stream,
		"<!DOCTYPE HTML>\n<html>\n"  \
		"<head>\n<meta name=\"generator\" content=\"Evolution Mail Component\" />\n" \
		"<title>Evolution Mail Display</title>\n" \
		"<link type=\"text/css\" rel=\"stylesheet\" href=\"evo-file://" EVOLUTION_PRIVDATADIR "/theme/webview.css\" />\n" \
		"</head>\n" \
		"<body style=\"background: #FFF; color: #000;\">", cancellable, NULL);

	for (iter = emf->mail_part_list; iter != NULL; iter = iter->next) {

		EMFormatPURI *puri = iter->data;

		/* Convert attachment bar to fancy HTML */
		/* FIXME WEBKIT
		if (g_str_has_suffix (puri->uri, ".attachment-bar")) {
			attachment_bar_html (puri, str, cancellable);
			continue;
		}
		*/

		/* Skip widget-parts. We either don't want them displayed
		 * or we will handle them manually */
		if (puri->write_func == NULL ||
		    g_str_has_prefix (puri->uri, "print_layout"))
			continue;

		if (puri->is_attachment || g_str_has_suffix (puri->uri, ".attachment")) {
			const EMFormatHandler *handler;
			EAttachment *attachment;
			GFileInfo *fi;

			CamelContentType *ct = camel_mime_part_get_content_type (puri->part);
			gchar *mime_type = camel_content_type_simple (ct);

			handler = em_format_find_handler (puri->emf, mime_type);
			g_free (mime_type);

			/* If we can't inline this attachment, skip it */
			if (!em_format_is_inline (puri->emf,  puri->uri, puri->part, handler))
				continue;

			/* FIXME WEBKIT
			attachment = ((EMFormatAttachmentPURI *) puri)->attachment;
			fi = e_attachment_get_file_info (attachment);
			g_string_append_printf (str, "<table border=\"0\" width=\"100%%\"><tr>" \
				"<td><strong><big>%s</big></strong></td>" \
				"<td style=\"text-align: right;\">Type: %s&nbsp;&nbsp;&nbsp;&nbsp;" \
				"Size: %ld bytes</td></tr></table>",
			   	e_attachment_get_description (attachment),
				e_attachment_get_mime_type (attachment),
				g_file_info_get_size (fi));
			*/
		}

		puri->write_func(puri->emf, puri, stream, &print_info, cancellable);
	}

	camel_stream_write_string (stream, "</body></html>", cancellable, NULL);
}


static void
efhp_finalize (GObject *object)
{
	EMFormatHTMLPrint *efhp = EM_FORMAT_HTML_PRINT (object);

	if (efhp->priv->original_formatter) {
		g_object_unref (efhp->priv->original_formatter);
		efhp->priv->original_formatter = NULL;
	}

	if (efhp->priv->top_level_puri) {
		em_format_puri_free (efhp->priv->top_level_puri);
		efhp->priv->top_level_puri = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
efhp_is_inline (EMFormat *emf,
                const gchar *part_id,
                CamelMimePart *mime_part,
                const EMFormatHandler *handle)
{
	/* When printing, inline any part that has a handler. */
	return (handle != NULL);
}

static void
efhp_set_orig_formatter (EMFormatHTMLPrint *efhp,
		    	 EMFormat *formatter)
{
	EMFormat *emfp, *emfs;
	EMFormatPURI *puri;
	GHashTableIter iter;
	gpointer key, value;

	efhp->priv->original_formatter = g_object_ref (formatter);

	emfp = EM_FORMAT (efhp);
	emfs = EM_FORMAT (formatter);

	emfp->mail_part_list = g_list_copy (emfs->mail_part_list);

	/* Make a shallow copy of the table. This table will NOT destroy
	 * the PURIs when free'd! */
	emfp->mail_part_table = 
		g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_iter_init (&iter, emfs->mail_part_table);
	while (g_hash_table_iter_next (&iter, &key, &value))
		g_hash_table_insert (emfp->mail_part_table, key, value);

	emfp->folder = g_object_ref (emfs->folder);
	emfp->message_uid = g_strdup (emfs->message_uid);
	emfp->message = g_object_ref (emfs->message);

	/* Add a generic PURI that will write a HTML layout
	   for all the parts */
	puri = em_format_puri_new (EM_FORMAT (efhp),
		sizeof (EMFormatPURI), NULL, "print_layout");
	puri->write_func = efhp_write_print_layout;
	puri->mime_type = g_strdup ("text/html");
	em_format_add_puri (EM_FORMAT (efhp), puri);		
	efhp->priv->top_level_puri = puri;	
}

static void
efhp_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	EMFormatHTMLPrintPrivate *priv;

	priv = EM_FORMAT_HTML_PRINT (object)->priv;

	switch (prop_id) {
		
		case PROP_ORIGINAL_FORMATTER:
			efhp_set_orig_formatter (
				EM_FORMAT_HTML_PRINT (object),
				(EMFormat *) g_value_get_object (value));
			return;

		case PROP_PRINT_ACTION:
			priv->print_action = 
				g_value_get_int (value);
			return;
		
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
efhp_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	EMFormatHTMLPrintPrivate *priv;

	priv = EM_FORMAT_HTML_PRINT (object)->priv;

	switch (prop_id) {
		
		case PROP_ORIGINAL_FORMATTER:
			g_value_set_pointer (value,
				priv->original_formatter);
			return;

		case PROP_PRINT_ACTION:
			g_value_set_int (value,
				priv->print_action);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
em_format_html_print_class_init (EMFormatHTMLPrintClass *class)
{
	GObjectClass *object_class;
	EMFormatClass *format_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMFormatHTMLPrintPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = efhp_finalize;
	object_class->set_property = efhp_set_property;
	object_class->get_property = efhp_get_property;

	format_class = EM_FORMAT_CLASS (class);
	format_class->is_inline = efhp_is_inline;

	g_object_class_install_property (
		object_class,
		PROP_ORIGINAL_FORMATTER,
		g_param_spec_object (
			"original-formatter",
			NULL,
			NULL,
			EM_TYPE_FORMAT,
			G_PARAM_WRITABLE |
			G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (
		object_class,
		PROP_PRINT_ACTION,
		g_param_spec_int (
			"print-action",
			NULL,
			NULL,
			GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
			GTK_PRINT_OPERATION_ACTION_EXPORT,
			GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
em_format_html_print_init (EMFormatHTMLPrint *efhp)
{
	efhp->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		efhp, EM_TYPE_FORMAT_HTML_PRINT, EMFormatHTMLPrintPrivate);

	efhp->export_filename = NULL;
	efhp->async = TRUE;		
}

EMFormatHTMLPrint *
em_format_html_print_new (EMFormatHTML *source,
                          GtkPrintOperationAction action)
{
	EMFormatHTMLPrint *efhp;

	efhp = g_object_new (EM_TYPE_FORMAT_HTML_PRINT, 
		"original-formatter", source,
		"print-action", action,
		NULL);

	return efhp;
}

static void
emfhp_complete (EMFormatHTMLPrint *efhp)
{
/* FIXME WEBKIT
	GtkPrintOperation *operation;
	EWebView *web_view;
	GError *error = NULL;

	operation = e_print_operation_new ();
// FIXME WEBKIT: Port to webkit's API, probably from outside
	if (efhp->action == GTK_PRINT_OPERATION_ACTION_EXPORT)
		gtk_print_operation_set_export_filename (operation, efhp->export_filename);

	gtk_html_print_operation_run (
		GTK_HTML (web_view),
		operation, efhp->action, NULL,
		(GtkHTMLPrintCalcHeight) NULL,
		(GtkHTMLPrintCalcHeight) efhp_calc_footer_height,
		(GtkHTMLPrintDrawFunc) NULL,
		(GtkHTMLPrintDrawFunc) efhp_draw_footer,
		NULL, &error);

	g_object_unref (operation);
*/
}

void
em_format_html_print_message (EMFormatHTMLPrint *efhp,
                              CamelMimeMessage *message,
                              CamelFolder *folder,
                              const gchar *message_uid)
{
	g_return_if_fail (EM_IS_FORMAT_HTML_PRINT (efhp));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	/* Wrap flags to display all entries by default.*/
	EM_FORMAT_HTML (efhp)->header_wrap_flags |=
		EM_FORMAT_HTML_HEADER_TO |
		EM_FORMAT_HTML_HEADER_CC |
		EM_FORMAT_HTML_HEADER_BCC;

	g_signal_connect (
		efhp, "complete", G_CALLBACK (emfhp_complete), efhp);

	/* FIXME Not passing a GCancellable here. */
	em_format_parse ((EMFormat *) efhp, message, folder, NULL);
}

GtkPrintOperationAction
em_format_html_print_get_action (EMFormatHTMLPrint *efhp)
{
	g_return_val_if_fail (EM_IS_FORMAT_HTML_PRINT (efhp), 
		GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG);
	
	return efhp->priv->print_action;
}