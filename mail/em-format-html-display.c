/*
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
 *		Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>

#ifdef G_OS_WIN32
/* Work around 'DATADIR' and 'interface' lossage in <windows.h> */
#define DATADIR crap_DATADIR
#include <windows.h>
#undef DATADIR
#undef interface
#endif

#include <glib/gi18n.h>

#include <e-util/e-util.h>
#include <e-util/e-util-private.h>

#include "e-util/e-datetime-format.h"
#include <e-util/e-dialog-utils.h>
#include <e-util/e-icon-factory.h>

#include <shell/e-shell.h>
#include <shell/e-shell-utils.h>

#include <libedataserver/e-flag.h>

#if defined (HAVE_NSS) && defined (ENABLE_SMIME)
#include "certificate-viewer.h"
#include "e-cert-db.h"
#endif

#include "e-mail-display.h"
#include "e-mail-attachment-bar.h"
#include "em-format-html-display.h"
#include "em-format-html-display-parts.h"
#include "em-utils.h"
#include "widgets/misc/e-attachment.h"
#include "widgets/misc/e-attachment-button.h"
#include "widgets/misc/e-attachment-view.h"
#include "shell/e-shell.h"
#include "shell/e-shell-window.h"

#define EM_FORMAT_HTML_DISPLAY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), EM_TYPE_FORMAT_HTML_DISPLAY, EMFormatHTMLDisplayPrivate))

#define d(x)

#define EM_FORMAT_HTML_DISPLAY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), EM_TYPE_FORMAT_HTML_DISPLAY, EMFormatHTMLDisplayPrivate))

struct _EMFormatHTMLDisplayPrivate {
                gint dummy;
};

/* TODO: move the dialogue elsehwere */
/* FIXME: also in em-format-html.c */
static const struct {
	const gchar *icon, *shortdesc, *description;
} smime_sign_table[5] = {
	{ "stock_signature-bad", N_("Unsigned"), N_("This message is not signed. There is no guarantee that this message is authentic.") },
	{ "stock_signature-ok", N_("Valid signature"), N_("This message is signed and is valid meaning that it is very likely that this message is authentic.") },
	{ "stock_signature-bad", N_("Invalid signature"), N_("The signature of this message cannot be verified, it may have been altered in transit.") },
	{ "stock_signature", N_("Valid signature, but cannot verify sender"), N_("This message is signed with a valid signature, but the sender of the message cannot be verified.") },
	{ "stock_signature-bad", N_("Signature exists, but need public key"), N_("This message is signed with a signature, but there is no corresponding public key.") },

};

static const struct {
	const gchar *icon, *shortdesc, *description;
} smime_encrypt_table[4] = {
	{ "stock_lock-broken", N_("Unencrypted"), N_("This message is not encrypted. Its content may be viewed in transit across the Internet.") },
	{ "stock_lock-ok", N_("Encrypted, weak"), N_("This message is encrypted, but with a weak encryption algorithm. It would be difficult, but not impossible for an outsider to view the content of this message in a practical amount of time.") },
	{ "stock_lock-ok", N_("Encrypted"), N_("This message is encrypted.  It would be difficult for an outsider to view the content of this message.") },
	{ "stock_lock-ok", N_("Encrypted, strong"), N_("This message is encrypted, with a strong encryption algorithm. It would be very difficult for an outsider to view the content of this message in a practical amount of time.") },
};

static const GdkRGBA smime_sign_colour[5] = {
	{ 0 }, { 0.53, 0.73, 0.53, 1 }, { 0.73, 0.53, 0.53, 1 }, { 0.91, 0.82, 0.13, 1 }, { 0 },
};

static void efhd_message_prefix 	(EMFormat *emf, CamelMimePart *part, GString *part_id, EMFormatParserInfo *info, GCancellable *cancellable);
static void efhd_message_add_bar	(EMFormat *emf, CamelMimePart *part, GString *part_id, EMFormatParserInfo *info, GCancellable *cancellable);
static void efhd_parse_attachment	(EMFormat *emf, CamelMimePart *part, GString *part_id, EMFormatParserInfo *info, GCancellable *cancellable);
static void efhd_parse_secure		(EMFormat *emf, CamelMimePart *part, GString *part_id, EMFormatParserInfo *info, GCancellable *cancellable);
static void efhd_parse_optional		(EMFormat *emf, CamelMimePart *part, GString *part_id, EMFormatParserInfo *info, GCancellable *cancellable);

static GtkWidget* efhd_attachment_bar		(EMFormat *emf, EMPart *emp, GCancellable *cancellable);
static GtkWidget* efhd_attachment_button	(EMFormat *emf, EMPart *emp, GCancellable *cancellable);
static GtkWidget* efhd_attachment_optional	(EMFormat *emf, EMPart *emp, GCancellable *cancellable);

static void efhd_builtin_init (EMFormatHTMLDisplayClass *efhc);

static gpointer parent_class;

static EAttachmentStore*
find_parent_attachment_store (EMFormatHTMLDisplay *efhd, GString *part_id)
{
	EMFormat *emf = (EMFormat *) efhd;
        EMPartAttachmentBar *empab;
	gchar *tmp, *pos;
        GList *item;

	tmp = g_strdup (part_id->str);

	do {
		gchar *id;

		pos = g_strrstr (tmp, ".");
		if (!pos)
			break;

		g_free (tmp);
		tmp = g_strndup (part_id->str, pos - tmp);
		id = g_strdup_printf ("%s.attachment-bar", tmp);

		item = g_hash_table_lookup (emf->mail_part_table, id);

		g_free (id);

	} while (pos && !item);

	g_free (tmp);

	empab = item->data;

        if (empab)
	        return em_part_attachment_bar_get_store (empab);
        else
                return NULL;
}

static void
efhd_xpkcs7mime_info_response (GtkWidget *widget,
                               guint button,
                               EMPartSMIME *emps)
{
	gtk_widget_destroy (widget);
	em_part_smime_set_widget (emps, NULL);
}

#if defined (HAVE_NSS) && defined (ENABLE_SMIME)
static void
efhd_xpkcs7mime_viewcert_clicked (GtkWidget *button,
                                  EMPartSMIME *emps)
{
	CamelCipherCertInfo *info = g_object_get_data((GObject *)button, "e-cert-info");
	ECert *ec = NULL;

	if (info->cert_data)
		ec = e_cert_new (CERT_DupCertificate (info->cert_data));

	if (ec != NULL) {
		GtkWidget *w = certificate_viewer_show (ec);
		GtkWidget *widget;

		/* oddly enough certificate_viewer_show doesn't ... */
		gtk_widget_show (w);
		g_signal_connect (
			w, "response",
			G_CALLBACK (gtk_widget_destroy), NULL);

		widget = em_part_smime_get_widget (emps);
		if (w && widget)
			gtk_window_set_transient_for ((GtkWindow *) w, (GtkWindow *) widget);

		if (widget)
			g_object_unref (widget);

		g_object_unref (ec);
	} else {
		g_warning("can't find certificate for %s <%s>", info->name?info->name:"", info->email?info->email:"");
	}
}
#endif

static void
efhd_xpkcs7mime_add_cert_table (GtkWidget *grid,
                                GQueue *certlist,
                                EMPartSMIME *emps)
{
	GList *head, *link;
	GtkTable *table;
	gint n = 0;

	table = (GtkTable *) gtk_table_new (certlist->length, 2, FALSE);

	head = g_queue_peek_head_link (certlist);

	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelCipherCertInfo *info = link->data;
		gchar *la = NULL;
		const gchar *l = NULL;

		if (info->name) {
			if (info->email && strcmp (info->name, info->email) != 0)
				l = la = g_strdup_printf("%s <%s>", info->name, info->email);
			else
				l = info->name;
		} else {
			if (info->email)
				l = info->email;
		}

		if (l) {
			GtkWidget *w;
#if defined (HAVE_NSS) && defined (ENABLE_SMIME)
			ECert *ec = NULL;
#endif
			w = gtk_label_new (l);
			gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
			g_free (la);
			gtk_table_attach (table, w, 0, 1, n, n + 1, GTK_FILL, GTK_FILL, 3, 3);
#if defined (HAVE_NSS) && defined (ENABLE_SMIME)
			w = gtk_button_new_with_mnemonic(_("_View Certificate"));
			gtk_table_attach (table, w, 1, 2, n, n + 1, 0, 0, 3, 3);
			g_object_set_data((GObject *)w, "e-cert-info", info);
			g_signal_connect (
				w, "clicked",
				G_CALLBACK (efhd_xpkcs7mime_viewcert_clicked), po);

			if (info->cert_data)
				ec = e_cert_new (CERT_DupCertificate (info->cert_data));

			if (ec == NULL)
				gtk_widget_set_sensitive (w, FALSE);
			else
				g_object_unref (ec);
#else
			w = gtk_label_new (_("This certificate is not viewable"));
			gtk_table_attach (table, w, 1, 2, n, n + 1, 0, 0, 3, 3);
#endif
			n++;
		}
	}

	gtk_container_add (GTK_CONTAINER (grid), GTK_WIDGET (table));
}

static void
efhd_xpkcs7mime_validity_clicked (GtkWidget *button,
                                  EMPartSMIME *emps)
{
	GtkBuilder *builder;
	GtkWidget *grid, *w;
	GtkWidget *part_widget;
	CamelCipherValidity *validity;

	part_widget = em_part_smime_get_widget (emps);
	if (part_widget) {
		g_object_unref (part_widget);
		/* FIXME: window raise? */
		return;
	}

	validity = em_part_get_validity ((EMPart *) emps);

	builder = gtk_builder_new ();
	e_load_ui_builder_definition (builder, "mail-dialogs.ui");

	part_widget = e_builder_get_widget(builder, "message_security_dialog");
	em_part_smime_set_widget (emps, part_widget);

	grid = e_builder_get_widget(builder, "signature_grid");
	w = gtk_label_new (_(smime_sign_table[validity->sign.status].description));
	gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
	gtk_label_set_line_wrap ((GtkLabel *) w, TRUE);
	gtk_container_add (GTK_CONTAINER (grid), w);
	if (validity->sign.description) {
		GtkTextBuffer *buffer;

		buffer = gtk_text_buffer_new (NULL);
		gtk_text_buffer_set_text (buffer, validity->sign.description, 
			strlen (validity->sign.description));
		w = g_object_new (gtk_scrolled_window_get_type (),
				 "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
				 "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
				 "shadow_type", GTK_SHADOW_IN,
				 "expand", TRUE,
				 "child", g_object_new(gtk_text_view_get_type(),
						       "buffer", buffer,
						       "cursor_visible", FALSE,
						       "editable", FALSE,
						       "width_request", 500,
						       "height_request", 160,
						       NULL),
				 NULL);
		g_object_unref (buffer);

		gtk_container_add (GTK_CONTAINER (grid), w);
	}

	if (!g_queue_is_empty (&po->valid->sign.signers))
		efhd_xpkcs7mime_add_cert_table (grid, &validity->sign.signers, emps);

	gtk_widget_show_all (grid);

	grid = e_builder_get_widget(builder, "encryption_grid");
	w = gtk_label_new (_(smime_encrypt_table[validity->encrypt.status].description));
	gtk_misc_set_alignment ((GtkMisc *) w, 0.0, 0.5);
	gtk_label_set_line_wrap ((GtkLabel *) w, TRUE);
	gtk_container_add (GTK_CONTAINER (grid), w);
	if (validity->encrypt.description) {
		GtkTextBuffer *buffer;

		buffer = gtk_text_buffer_new (NULL);
		gtk_text_buffer_set_text (buffer, validity->encrypt.description,
			strlen (validity->encrypt.description));
		w = g_object_new (gtk_scrolled_window_get_type (),
				 "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
				 "vscrollbar_policy", GTK_POLICY_AUTOMATIC,
				 "shadow_type", GTK_SHADOW_IN,
				 "expand", TRUE,
				 "child", g_object_new(gtk_text_view_get_type(),
						       "buffer", buffer,
						       "cursor_visible", FALSE,
						       "editable", FALSE,
						       "width_request", 500,
						       "height_request", 160,
						       NULL),
				 NULL);
		g_object_unref (buffer);

		gtk_container_add (GTK_CONTAINER (grid), w);
	}

	if (!g_queue_is_empty (&po->valid->encrypt.encrypters))
		efhd_xpkcs7mime_add_cert_table (grid, 
			&validity->encrypt.encrypters, emps);

	gtk_widget_show_all (grid);

	g_object_unref (builder);

	g_signal_connect (
	        part_widget, "response", 
		G_CALLBACK(efhd_xpkcs7mime_info_response), emps);
	gtk_widget_show (part_widget);

	g_object_unref (part_widget);
	camel_cipher_validity_free (validity);
}

static GtkWidget*
efhd_xpkcs7mime_button (EMFormat *emf,
                        EMPart *emp,
                        GCancellable *cancellable)
{
	GtkWidget *box, *button, *layout, *widget;
	EMPartSMIME *emps = (EMPartSMIME *) emp;
	const gchar *icon_name;
	gchar *description;
	CamelCipherValidity *validity;

	validity = em_part_get_validity (emp);
	if (!validity)
		return NULL;

	/* FIXME: need to have it based on encryption and signing too */
	if (validity->sign.status != 0)
		icon_name = smime_sign_table[validity->sign.status].icon;
	else
		icon_name = smime_encrypt_table[validity->encrypt.status].icon;

	box = gtk_event_box_new ();
	if (validity->sign.status != 0)
		gtk_widget_override_background_color (box, GTK_STATE_FLAG_NORMAL,
			&smime_sign_colour[validity->sign.status]);

	layout = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
	gtk_container_add (GTK_CONTAINER (box), layout);

	button = gtk_button_new ();
	gtk_box_pack_start (GTK_BOX (layout), button, FALSE, FALSE, 0);
	g_signal_connect (button, "clicked",
		G_CALLBACK (efhd_xpkcs7mime_validity_clicked), emp);

	widget = gtk_image_new_from_icon_name (
			icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_button_set_image (GTK_BUTTON (button), widget);

	description = em_part_smime_get_description (emps);
	widget = gtk_label_new (description);
	gtk_box_pack_start (GTK_BOX (layout), widget, FALSE, FALSE, 0);

	gtk_widget_show_all (box);

	g_free (description);
	camel_cipher_validity_free (validity);

	return box;
}

struct attachment_load_data {
	EAttachment *attachment;
	EFlag *flag;
};

static void
attachment_loaded (EAttachment *attachment,
		   GAsyncResult *res,
		   gpointer user_data)
{
	struct attachment_load_data *data = user_data;
	EShell *shell;
	GtkWindow *window;
	
	shell = e_shell_get_default ();
	window = e_shell_get_active_window (shell);
	if (!E_IS_SHELL_WINDOW (window))
		window = NULL;
	
        e_attachment_load_handle_error (data->attachment, res, window);

	e_flag_set (data->flag);
}

/* Idle callback */
static gboolean
load_attachment_idle (struct attachment_load_data *data)
{
	e_attachment_load_async (data->attachment,
		(GAsyncReadyCallback) attachment_loaded, data);

        return FALSE;
}


static void
efhd_parse_attachment (EMFormat *emf,
                       CamelMimePart *part,
                       GString *part_id,
                       EMFormatParserInfo *info,
                       GCancellable *cancellable)
{
	gchar *text, *html;
	EMFormatHTMLDisplay *efhd = (EMFormatHTMLDisplay *) emf;
	EMPartAttachment *empa;
	EAttachmentStore *store;
	EAttachment *attachment;
	const EMFormatHandler *handler;
	CamelContentType *ct;
	gchar *mime_type;
	gint len;
	const gchar *cid;
	guint32 size;
	struct attachment_load_data *load_data;

	if (g_cancellable_is_cancelled (cancellable))
		return;

	len = part_id->len;
	g_string_append (part_id, ".attachment");

        /* Try to find handler for the mime part */
	ct = camel_mime_part_get_content_type (part);
	if (ct) {
		mime_type = camel_content_type_simple (ct);
		handler = em_format_find_handler (emf, mime_type);
	}

	/* FIXME: should we look up mime_type from object again? */
	text = em_format_describe_part (part, mime_type);
	html = camel_text_to_html (
		text, EM_FORMAT_HTML (emf)->text_html_flags &
		CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS, 0);
	g_free (text);
	g_free (mime_type);

        attachment = e_attachment_new ();
        empa = EM_PART_ATTACHMENT (em_part_attachment_new (emf, part, 
                        part_id->str, NULL));
        em_part_set_widget_func ((EMPart *) empa, efhd_attachment_button);
        em_part_attachment_set_is_shown (empa, 
                (handler && em_format_is_inline (emf, part_id->str, part, handler)));
        em_part_attachment_set_snoop_mime_type (empa, em_format_snoop_type (part));
	em_part_attachment_set_attachment (empa, attachment);
        em_part_attachment_set_view_part_id (empa, part_id->str);
        em_part_attachment_set_description (empa, html);
	em_part_attachment_set_handler (empa, handler);
        em_part_set_validity ((EMPart *) empa, info->validity);

	cid = camel_mime_part_get_content_id (part);
	if (cid) {
                gchar *new_cid = g_strdup_printf ("cid:%s", cid);
                em_part_set_cid ((EMPart *) empa, new_cid);
		g_free (new_cid);
        }

	if (handler) {
                CamelContentType *ct;
                em_part_set_write_func ((EMPart *) empa, handler->write_func);

                /* This mime_type is important for WebKit to determine content type.
                 * We have converted text/ * to text/html, other (binary) formats remained
                 * untouched. */
                ct = camel_content_type_decode (handler->mime_type);
                if (g_strcmp0 (ct->type, "text") == 0)
                        em_part_set_mime_type ((EMPart *) empa, "text/html");
                else {
                        gchar *ct_s = camel_content_type_simple (ct);
                        em_part_set_mime_type ((EMPart *) empa, ct_s);
                        g_free (ct_s);
                }

                camel_content_type_unref (ct);
        }

        em_format_add_part_object (emf, (EMPart *) empa);

        /* Though it is an attachment, we still might be able to parse it and
         * so discover some parts that we might be event able to display. */
        if (handler && handler->parse_func && (handler->parse_func != efhd_parse_attachment)&&
            ((handler->flags & EM_FORMAT_HANDLER_COMPOUND_TYPE) ||
             (handler->flags & EM_FORMAT_HANDLER_INLINE_DISPOSITION))) {
                EMFormatParserInfo attachment_info = { .handler = handler,
                                                       .is_attachment = TRUE };
                handler->parse_func (emf, part, part_id, &attachment_info, cancellable);
        }

	e_attachment_set_mime_part (attachment, part);
	e_attachment_set_shown (attachment, em_part_attachment_get_is_shown (empa));
        if (info->validity) {
	        e_attachment_set_signed (attachment, info->validity->sign.status);
	        e_attachment_set_encrypted (attachment, info->validity->encrypt.status);
        }
	e_attachment_set_can_show (attachment, handler != NULL && handler->write_func);

	store = find_parent_attachment_store (efhd, part_id);
	e_attachment_store_add_attachment (store, attachment);

	if (emf->folder && emf->folder->summary && emf->message_uid) {
                CamelDataWrapper *dw = camel_medium_get_content (CAMEL_MEDIUM (part));
                GByteArray *ba;
                ba = camel_data_wrapper_get_byte_array (dw);
                if (ba) {
                        size = ba->len;

                        if (camel_mime_part_get_encoding (part) == CAMEL_TRANSFER_ENCODING_BASE64)
                                size = size / 1.37;
                }
	}

	load_data = g_new0 (struct attachment_load_data, 1);
	load_data->attachment = g_object_ref (attachment);
	load_data->flag = e_flag_new ();

        e_flag_clear (load_data->flag);

	/* e_attachment_load_async must be called from main thread */
        g_idle_add ((GSourceFunc) load_attachment_idle, load_data);

	e_flag_wait (load_data->flag);

        e_flag_free (load_data->flag);
        g_object_unref (load_data->attachment);
        g_free (load_data);

	if (size != 0) {
		GFileInfo *fileinfo;

		fileinfo = e_attachment_get_file_info (attachment);
		g_file_info_set_size (fileinfo, size);
		e_attachment_set_file_info (attachment, fileinfo);
	}

	g_string_truncate (part_id, len);
}

static void
efhd_parse_optional (EMFormat *emf,
                     CamelMimePart *part,
                     GString *part_id,
                     EMFormatParserInfo *info,
                     GCancellable *cancellable)
{
	EMPartAttachment *empa;
	EAttachment *attachment;
	CamelStream *stream;
        gint len;

        len = part_id->len;
        g_string_append (part_id, ".optional");

	attachment = e_attachment_new ();
	empa = (EMPartAttachment *) em_part_attachment_new (
			emf, part, part_id->str, NULL);
	em_part_attachment_set_view_part_id (empa, part_id->str);
	em_part_attachment_set_handler (empa, em_format_find_handler (emf, "text/plain"));
	em_part_attachment_set_is_shown (empa, FALSE);
	em_part_attachment_set_snoop_mime_type (empa, "text/plain");
	em_part_attachment_set_attachment (empa, attachment);
	em_part_set_widget_func ((EMPart *) empa, efhd_attachment_optional);
	
	e_attachment_set_mime_part (attachment, part);
	em_part_attachment_set_description (empa, 
		N_("Evolution cannot render this email as it is too "
		  "large to process. You can view it unformatted or "
		  "with an external text editor."));
	

	stream = camel_stream_mem_new ();
	em_part_attachment_set_mstream (empa, stream);	

	camel_data_wrapper_decode_to_stream_sync ((CamelDataWrapper *) part, 
		(CamelStream *) stream, cancellable, NULL);

	if (info->validity) {
		em_part_set_validity ((EMPart *) empa, info->validity);
	}

	em_format_add_part_object (emf, (EMPart *) empa);

	g_string_truncate (part_id, len);
}

static void
efhd_parse_secure (EMFormat *emf,
		   CamelMimePart *part,
		   GString *part_id,
		   EMFormatParserInfo *info,
		   GCancellable *cancellable)
{
	if (info->validity
	    && (info->validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
		|| info->validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE)) {
		GString *buffer;
                EMPart *emp;

                emp = em_part_smime_new (emf, part, part_id->str, NULL);
                em_part_set_widget_func (emp, efhd_xpkcs7mime_button);
                em_part_set_validity (emp, info->validity);
		
                em_format_add_part_object (emf, emp);

		buffer = g_string_new ("");

		if (info->validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE) {
			const gchar *desc;
			gint status;

			status = info->validity->sign.status;
			desc = smime_sign_table[status].shortdesc;

			g_string_append (buffer, gettext (desc));

			em_format_html_format_cert_infos (
				(CamelCipherCertInfo *) info->validity->sign.signers.head);
		}

		if (info->validity->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE) {
			const gchar *desc;
			gint status;

			if (info->validity->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE)
				g_string_append (buffer, "\n");

			status = info->validity->encrypt.status;
			desc = smime_encrypt_table[status].shortdesc;
			g_string_append (buffer, gettext (desc));
		}

		em_part_smime_set_description ((EMPartSMIME *) emp, buffer->str);
		g_string_free (buffer, TRUE);
	}
}

static void
efhd_finalize (GObject *object)
{
	EMFormatHTMLDisplay *efhd;

	efhd = EM_FORMAT_HTML_DISPLAY (object);
	g_return_if_fail (efhd != NULL);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
efhd_class_init (EMFormatHTMLDisplayClass *class)
{
	GObjectClass *object_class;
	EMFormatHTMLClass *format_html_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EMFormatHTMLDisplayPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = efhd_finalize;

	format_html_class = EM_FORMAT_HTML_CLASS (class);
	format_html_class->html_widget_type = E_TYPE_MAIL_DISPLAY;

	efhd_builtin_init (class);
}

static void
efhd_init (EMFormatHTMLDisplay *efhd)
{
	efhd->priv = EM_FORMAT_HTML_DISPLAY_GET_PRIVATE (efhd);

        /* we want to convert url's etc */
	EM_FORMAT_HTML (efhd)->text_html_flags |=
		CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS |
		CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES;

}

GType
em_format_html_display_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo type_info = {
			sizeof (EMFormatHTMLDisplayClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) efhd_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EMFormatHTMLDisplay),
			0,     /* n_preallocs */
			(GInstanceInitFunc) efhd_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			EM_TYPE_FORMAT_HTML, "EMFormatHTMLDisplay",
			&type_info, 0);
	}

	return type;
}

EMFormatHTMLDisplay *
em_format_html_display_new (void)
{
	return g_object_new (EM_TYPE_FORMAT_HTML_DISPLAY, NULL);
}

/* ********************************************************************** */

static EMFormatHandler type_builtin_table[] = {
	{ (gchar *) "x-evolution/message/prefix", efhd_message_prefix, },
	{ (gchar *) "x-evolution/message/attachment-bar", (EMFormatParseFunc) efhd_message_add_bar, },
	{ (gchar *) "x-evolution/message/attachment", efhd_parse_attachment, },
	{ (gchar *) "x-evolution/message/x-secure-button", efhd_parse_secure, },
	{ (gchar *) "x-evolution/message/optional", efhd_parse_optional, },
};

static void
efhd_builtin_init (EMFormatHTMLDisplayClass *efhc)
{
	gint i;

	EMFormatClass *emfc = (EMFormatClass *) efhc;

	for (i = 0; i < G_N_ELEMENTS (type_builtin_table); i++)
		em_format_class_add_handler (emfc, &type_builtin_table[i]);
}

static void
efhd_message_prefix (EMFormat *emf,
                     CamelMimePart *part,
                     GString *part_id,
                     EMFormatParserInfo *info,
                     GCancellable *cancellable)
{
	const gchar *flag, *comp, *due;
	time_t date;
	gchar *iconpath, *due_date_str;
	GString *buffer;
	EMPartAttachment *empa;

	if ((emf->folder == NULL) || (emf->message_uid == NULL)
	    || ((flag = camel_folder_get_message_user_tag (emf->folder,
				emf->message_uid, "follow-up")) == NULL)
	    || (flag[0] == 0))
		return;

	empa = (EMPartAttachment *) em_part_attachment_new (
			emf, part, ".message_prefix", NULL);
	em_part_attachment_set_view_part_id (empa, part_id->str);

	comp = camel_folder_get_message_user_tag(emf->folder, emf->message_uid, "completed-on");
	iconpath = e_icon_factory_get_icon_filename (
		comp && comp[0] ?
			"stock_mail-flag-for-followup-done" :
			"stock_mail-flag-for-followup", 
		GTK_ICON_SIZE_MENU);

	if (iconpath) {
		gchar *classid;

		classid = g_strdup_printf (
			"icon:///em-format-html-display/%s/%s",
			part_id->str,
			comp && comp[0] ? "comp" : "uncomp");

		em_part_set_uri ((EMPart *) empa, classid);

		g_free (classid);
	}

	buffer = g_string_new ("");

	if (comp && comp[0]) {
		date = camel_header_decode_date (comp, NULL);
		due_date_str = e_datetime_format_format (
			"mail", "header", DTFormatKindDateTime, date);
		g_string_append_printf (
			buffer, "%s, %s %s",
			flag, _("Completed on"),
			due_date_str ? due_date_str : "???");
		g_free (due_date_str);
	} else if ((due = camel_folder_get_message_user_tag (emf->folder, 
			emf->message_uid, "due-by")) != NULL && due[0]) {
		time_t now;

		date = camel_header_decode_date (due, NULL);
		now = time (NULL);
		if (now > date)
			g_string_append_printf (
				buffer,
				"<b>%s</b> ",
				_("Overdue:"));

		due_date_str = e_datetime_format_format (
			"mail", "header", DTFormatKindDateTime, date);
		/* Translators: the "by" is part of the string,
		 * like "Follow-up by Tuesday, January 13, 2009" */
		g_string_append_printf (
			buffer, "%s %s %s",
			flag, _("by"),
			due_date_str ? due_date_str : "???");
		g_free (due_date_str);
	} else {
		g_string_append (buffer, flag);
	}

	em_part_attachment_set_description (empa, buffer->str);
	g_string_free (buffer, TRUE);
}

/* ********************************************************************** */

/* attachment button callback */
static GtkWidget*
efhd_attachment_button (EMFormat *emf,
			EMPart *emp,
			GCancellable *cancellable)
{
	EMPartAttachment *empa = (EMPartAttachment *) emp;
	EAttachment *attachment;
	GtkWidget *widget;

	/* FIXME: handle default shown case */
	d(printf("adding attachment button/content\n"));

	if (g_cancellable_is_cancelled (cancellable))
		return NULL;

	widget = em_part_attachment_get_forward_widget (empa);
	if (widget) {
		g_warning ("unable to expand the attachment\n");
		g_object_unref (widget);
		return NULL;
	}
	g_object_unref (widget);

	attachment = em_part_attachment_get_attachment (empa);
	widget = e_attachment_button_new ();
	e_attachment_button_set_attachment (
		E_ATTACHMENT_BUTTON (widget), attachment);
	gtk_widget_set_can_focus (widget, TRUE);
	gtk_widget_show (widget);

	g_object_unref (attachment);

	return widget;
}

static GtkWidget*
efhd_attachment_bar (EMFormat *emf,
		     EMPart *emp,
		     GCancellable *cancellable)
{
	EMPartAttachmentBar *empab = (EMPartAttachmentBar *) emp;
	GtkWidget *widget;
	EAttachmentStore *store;

	store = em_part_attachment_bar_get_store (empab);
	widget = e_mail_attachment_bar_new (store);
	g_object_unref (store);

	return widget;
}

static void
efhd_message_add_bar (EMFormat *emf,
                      CamelMimePart *part,
                      GString *part_id,
                      EMFormatParserInfo *info,
                      GCancellable *cancellable)
{
	EMPart *emp;
	EAttachmentStore *store;
	gint len;

	if (g_cancellable_is_cancelled (cancellable))
		return;

	len = part_id->len;
	g_string_append (part_id, ".attachment-bar");

        store = E_ATTACHMENT_STORE (e_attachment_store_new ());

        emp = em_part_attachment_bar_new (emf, part, part_id->str, NULL);
        em_part_set_widget_func (emp, efhd_attachment_bar);
        em_part_attachment_bar_set_store ((EMPartAttachmentBar *) emp, store);
        em_format_add_part_object (emf, emp);

	g_string_truncate (part_id, len);

        /* Store is now owned only by the EMPart */
        g_object_unref (store);
}

static void
efhd_optional_button_show (GtkWidget *widget,
                           GtkWidget *w)
{
	GtkWidget *label = g_object_get_data (G_OBJECT (widget), "text-label");

	if (gtk_widget_get_visible (w)) {
		gtk_widget_hide (w);
		gtk_label_set_text_with_mnemonic (GTK_LABEL (label), _("View _Unformatted"));
	} else {
		gtk_label_set_text_with_mnemonic (GTK_LABEL (label), _("Hide _Unformatted"));
		gtk_widget_show (w);
	}
}


/* optional render attachment button callback */
static GtkWidget*
efhd_attachment_optional (EMFormat *efh,
			  EMPart *emp,
			  GCancellable *cancellable)
{
	GtkWidget *hbox, *vbox, *button, *mainbox, *scroll, *label, *img;
	AtkObject *a11y;
	GtkWidget *view;
	GtkTextBuffer *buffer;
	GByteArray *byte_array;
	EMPartAttachment *empa = (EMPartAttachment *) emp;
	const EMFormatHandler *handler;
	CamelStream *stream;

	if (g_cancellable_is_cancelled (cancellable))
		return NULL;

	/* FIXME: handle default shown case */
	d(printf("adding attachment button/content for optional rendering\n"));

	button = em_part_attachment_get_forward_widget (empa);
	if (button) {
		g_warning ("unable to expand the attachment\n");
		g_object_unref (button);
		return NULL;
	}
	g_object_unref (button);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	mainbox = gtk_hbox_new (FALSE, 0);

	button = gtk_button_new ();
	hbox = gtk_hbox_new (FALSE, 0);
	img = gtk_image_new_from_icon_name (
		"stock_show-all", GTK_ICON_SIZE_BUTTON);
	label = gtk_label_new_with_mnemonic(_("View _Unformatted"));
	g_object_set_data (G_OBJECT (button), "text-label", (gpointer)label);
	gtk_box_pack_start (GTK_BOX (hbox), img, TRUE, TRUE, 2);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);
	gtk_widget_show_all (hbox);
	gtk_container_add (GTK_CONTAINER (button), GTK_WIDGET (hbox));

	handler = em_part_attachment_get_handler (empa);
	if (handler)
		g_signal_connect (
			button, "clicked",
			G_CALLBACK (efhd_optional_button_show), scroll);
	else {
		gtk_widget_set_sensitive (button, FALSE);
		gtk_widget_set_can_focus (button, FALSE);
	}

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (mainbox), button, FALSE, FALSE, 6);

	button = gtk_button_new ();
	hbox = gtk_hbox_new (FALSE, 0);
	img = gtk_image_new_from_stock (
		GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
	label = gtk_label_new_with_mnemonic(_("O_pen With"));
	gtk_box_pack_start (GTK_BOX (hbox), img, TRUE, TRUE, 2);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 2);
	gtk_box_pack_start (GTK_BOX (hbox), gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_NONE), TRUE, TRUE, 2);
	gtk_widget_show_all (hbox);
	gtk_container_add (GTK_CONTAINER (button), GTK_WIDGET (hbox));

	a11y = gtk_widget_get_accessible (button);
	/* Translators: Name of an Attachment button for a11y object */
	atk_object_set_name (a11y, C_("Button", "Attachment"));

	gtk_box_pack_start (GTK_BOX (mainbox), button, FALSE, FALSE, 6);

	gtk_widget_show_all (mainbox);

	gtk_box_pack_start (GTK_BOX (vbox), mainbox, FALSE, FALSE, 6);

	view = gtk_text_view_new ();
	gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
	gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), FALSE);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

	stream = em_part_attachment_get_mstream (empa);
	byte_array = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
	gtk_text_buffer_set_text (
		buffer, (gchar *) byte_array->data, byte_array->len);
	g_object_unref (stream);
	em_part_attachment_set_mstream (empa, NULL);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (view));
	gtk_box_pack_start (GTK_BOX (vbox), scroll, TRUE, TRUE, 6);
	gtk_widget_show (GTK_WIDGET (view));

	if (!em_part_attachment_get_is_shown (empa))
		gtk_widget_hide (scroll);

	gtk_widget_show (vbox);

	em_part_attachment_set_handler (empa, NULL);

	return view;
}
