/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 *  Authors: Ettore Perazzoli <ettore@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 1999-2002 Ximian, Inc. (www.ximian.com)
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
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-app.h>
#include <libgnomeui/gnome-app-helper.h>
#include <libgnomeui/gnome-popup-menu.h>
#include <libgnomevfs/gnome-vfs-mime-handlers.h>

#include "e-msg-composer.h"
#include "e-msg-composer-select-file.h"
#include "e-msg-composer-attachment.h"
#include "e-msg-composer-attachment-bar.h"

#include <gal/util/e-iconv.h>

#include "camel/camel-data-wrapper.h"
#include "camel/camel-stream-fs.h"
#include "camel/camel-stream-null.h"
#include "camel/camel-stream-filter.h"
#include "camel/camel-mime-filter-bestenc.h"
#include "camel/camel-mime-part.h"


#define ICON_WIDTH 64
#define ICON_SEPARATORS " /-_"
#define ICON_SPACING 2
#define ICON_ROW_SPACING ICON_SPACING
#define ICON_COL_SPACING ICON_SPACING
#define ICON_BORDER 2
#define ICON_TEXT_SPACING 2


static GnomeIconListClass *parent_class = NULL;

struct _EMsgComposerAttachmentBarPrivate {
	GList *attachments;
	guint num_attachments;

	GtkWidget *context_menu;
	GtkWidget *icon_context_menu;
};


enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


static void update (EMsgComposerAttachmentBar *bar);


static char *
size_to_string (gulong size)
{
	char *size_string;
	
	/* FIXME: The following should probably go into a separate module, as
           we might have to do the same thing in other places as well.  Also,
	   I am not sure this will be OK for all the languages.  */
	
	if (size < 1e3L) {
		size_string = NULL;
	} else {
		gdouble displayed_size;
		
		if (size < 1e6L) {
			displayed_size = (gdouble) size / 1.0e3;
			size_string = g_strdup_printf (_("%.0fK"), displayed_size);
		} else if (size < 1e9L) {
			displayed_size = (gdouble) size / 1.0e6;
			size_string = g_strdup_printf (_("%.0fM"), displayed_size);
		} else {
			displayed_size = (gdouble) size / 1.0e9;
			size_string = g_strdup_printf (_("%.0fG"), displayed_size);
		}
	}
	
	return size_string;
}

/* Attachment handling functions.  */

static void
free_attachment_list (EMsgComposerAttachmentBar *bar)
{
	EMsgComposerAttachmentBarPrivate *priv;
	GList *p;
	
	priv = bar->priv;
	
	for (p = priv->attachments; p != NULL; p = p->next)
		g_object_unref (p->data);
	
	priv->attachments = NULL;
}

static void
attachment_changed_cb (EMsgComposerAttachment *attachment,
		       gpointer data)
{
	update (E_MSG_COMPOSER_ATTACHMENT_BAR (data));
}

static void
add_common (EMsgComposerAttachmentBar *bar,
	    EMsgComposerAttachment *attachment)
{
	g_return_if_fail (attachment != NULL);
	
	g_signal_connect (attachment, "changed",
			  G_CALLBACK (attachment_changed_cb),
			  bar);
	
	bar->priv->attachments = g_list_append (bar->priv->attachments,
						attachment);
	bar->priv->num_attachments++;
	
	update (bar);
	
	g_signal_emit (bar, signals[CHANGED], 0);
}

static void
add_from_mime_part (EMsgComposerAttachmentBar *bar,
		    CamelMimePart *part)
{
	add_common (bar, e_msg_composer_attachment_new_from_mime_part (part));
}

static void
add_from_file (EMsgComposerAttachmentBar *bar,
	       const char *file_name,
	       const char *disposition)
{
	EMsgComposerAttachment *attachment;
	EMsgComposer *composer;
	CamelException ex;
	GtkWidget *dialog;
	
	camel_exception_init (&ex);
	attachment = e_msg_composer_attachment_new (file_name, disposition, &ex);
	if (attachment) {
		add_common (bar, attachment);
	} else {
		composer = E_MSG_COMPOSER (gtk_widget_get_toplevel (GTK_WIDGET (bar)));
		dialog = gtk_message_dialog_new(GTK_WINDOW(composer),
						GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						"%s", camel_exception_get_description (&ex));
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		camel_exception_clear (&ex);
	}
}

static void
remove_attachment (EMsgComposerAttachmentBar *bar,
		   EMsgComposerAttachment *attachment)
{
	bar->priv->attachments = g_list_remove (bar->priv->attachments,
						attachment);
	bar->priv->num_attachments--;
	
	g_object_unref(attachment);
	
	g_signal_emit (bar, signals[CHANGED], 0);
}


/* Icon list contents handling.  */

static GdkPixbuf *
pixbuf_for_mime_type (const char *mime_type)
{
	const char *icon_name;
	char *filename = NULL;
	GdkPixbuf *pixbuf;

	/* Special-case these two since GNOME VFS doesn't know about them and
	   they are used every time the user forwards one or more messages
	   inline.  (See #9786.)  */
	if (strcmp (mime_type, "message/digest") == 0
	    || strcmp (mime_type, "multipart/digest") == 0
	    || strcmp (mime_type, "message/rfc822") == 0) {
		char *name;
		
		name = g_build_filename (EVOLUTION_IMAGESDIR, "mail.png", NULL);
		pixbuf = gdk_pixbuf_new_from_file (name, NULL);
		g_free (name);
		
		if (pixbuf != NULL)
			return pixbuf;
	}
	
	icon_name = gnome_vfs_mime_get_icon (mime_type);
	if (icon_name) {
		if (*icon_name == '/') {
			pixbuf = gdk_pixbuf_new_from_file (icon_name, NULL);
			if (pixbuf)
				return pixbuf;
		}
		
		filename = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, icon_name, TRUE, NULL);
		if (!filename) {
			char *fm_icon;
			
			fm_icon = g_strdup_printf ("nautilus/%s", icon_name);
			filename = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, fm_icon, TRUE, NULL);
			if (!filename) {
				g_free (fm_icon);
				fm_icon = g_strdup_printf ("mc/%s", icon_name);
				filename = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, fm_icon, TRUE, NULL);
			}
			g_free (fm_icon);
		}
	}
	
	if (filename && (pixbuf = gdk_pixbuf_new_from_file (filename, NULL))) {
		g_free (filename);
		return pixbuf;
	}
	
	g_free (filename);
	filename = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, "gnome-unknown.png", TRUE, NULL);
	
	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	g_free (filename);
	
	return pixbuf;
}

static void
update (EMsgComposerAttachmentBar *bar)
{
	EMsgComposerAttachmentBarPrivate *priv;
	GnomeIconList *icon_list;
	GList *p;
	
	priv = bar->priv;
	icon_list = GNOME_ICON_LIST (bar);
	
	gnome_icon_list_freeze (icon_list);
	
	gnome_icon_list_clear (icon_list);
	
	/* FIXME could be faster, but we don't care.  */
	for (p = priv->attachments; p != NULL; p = p->next) {
		EMsgComposerAttachment *attachment;
		CamelContentType *content_type;
		char *size_string, *label;
		GdkPixbuf *pixbuf;
		gboolean image;
		const char *desc;
		
		attachment = p->data;
		content_type = camel_mime_part_get_content_type (attachment->body);
		/* Get the image out of the attachment 
		   and create a thumbnail for it */
		image = header_content_type_is (content_type, "image", "*");
		
		if (image && attachment->pixbuf_cache == NULL) {
			CamelDataWrapper *wrapper;
			CamelStream *mstream;
			GdkPixbufLoader *loader;
			gboolean error = TRUE;
			char tmp[4096];
			int t;
			
			wrapper = camel_medium_get_content_object (CAMEL_MEDIUM (attachment->body));
			mstream = camel_stream_mem_new ();
			
			camel_data_wrapper_write_to_stream (wrapper, mstream);
			
			camel_stream_reset (mstream);
			
			/* Stream image into pixbuf loader */
			loader = gdk_pixbuf_loader_new ();
			do {
				t = camel_stream_read (mstream, tmp, 4096);
				if (t > 0) {
					error = !gdk_pixbuf_loader_write (loader, tmp, t, NULL);
					if (error) {
						break;
					}
				} else {
					if (camel_stream_eos (mstream))
						break;
					error = TRUE;
					break;
				}
				
			} while (!camel_stream_eos (mstream));
			
			if (!error) {
				int ratio, width, height;
				
				/* Shrink pixbuf */
				pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
				width = gdk_pixbuf_get_width (pixbuf);
				height = gdk_pixbuf_get_height (pixbuf);
				if (width >= height) {
					if (width > 48) {
						ratio = width / 48;
						width = 48;
						height = height / ratio;
					}
				} else {
					if (height > 48) {
						ratio = height / 48;
						height = 48;
						width = width / ratio;
					}
				}
				
				attachment->pixbuf_cache = gdk_pixbuf_scale_simple 
					(pixbuf,
					 width,
					 height,
					 GDK_INTERP_BILINEAR);
			} else {
				g_warning ("GdkPixbufLoader Error");
				image = FALSE;
			}
			
			/* Destroy everything */
			gdk_pixbuf_loader_close (loader, NULL);
			g_object_unref (loader);
			camel_stream_close (mstream);
		}
		
		desc = camel_mime_part_get_description (attachment->body);
		if (!desc || *desc == '\0')
			desc = camel_mime_part_get_filename (attachment->body);
		
		if (!desc)
			desc = _("attachment");
		
		if (attachment->size
		    && (size_string = size_to_string (attachment->size))) {
			label = g_strdup_printf ("%s (%s)", desc, size_string);
			g_free (size_string);
		} else
			label = g_strdup (desc);
		
		if (image) {
			gnome_icon_list_append_pixbuf (icon_list, attachment->pixbuf_cache, NULL, label);
		} else {
			char *mime_type;
			
			mime_type = header_content_type_simple (content_type);
			pixbuf = pixbuf_for_mime_type (mime_type);
			g_free (mime_type);
			gnome_icon_list_append_pixbuf (icon_list, pixbuf, NULL, label);
			if (pixbuf)
				g_object_unref (pixbuf);
		}
		
		g_free (label);
	}
	
	gnome_icon_list_thaw (icon_list);
}

static void
remove_selected (EMsgComposerAttachmentBar *bar)
{
	GnomeIconList *icon_list;
	EMsgComposerAttachment *attachment;
	GList *attachment_list, *p;
	int num;
	
	icon_list = GNOME_ICON_LIST (bar);
	
	/* Weee!  I am especially proud of this piece of cheesy code: it is
           truly awful.  But unless one attaches a huge number of files, it
           will not be as greedy as intended.  FIXME of course.  */
	
	attachment_list = NULL;
	p = gnome_icon_list_get_selection (icon_list);
	for ( ; p != NULL; p = p->next) {
		num = GPOINTER_TO_INT (p->data);
		attachment = E_MSG_COMPOSER_ATTACHMENT (g_list_nth (bar->priv->attachments, num)->data);
		attachment_list = g_list_prepend (attachment_list, attachment);
	}
	
	for (p = attachment_list; p != NULL; p = p->next)
		remove_attachment (bar, E_MSG_COMPOSER_ATTACHMENT (p->data));
	
	g_list_free (attachment_list);
	
	update (bar);
}

static void
edit_selected (EMsgComposerAttachmentBar *bar)
{
	GnomeIconList *icon_list;
	EMsgComposerAttachment *attachment;
	GList *selection;
	int num;
	
	icon_list = GNOME_ICON_LIST (bar);
	
	selection = gnome_icon_list_get_selection (icon_list);
	num = GPOINTER_TO_INT (selection->data);
	attachment = g_list_nth (bar->priv->attachments, num)->data;
	
	e_msg_composer_attachment_edit (attachment, GTK_WIDGET (bar));
}


/* "Attach" dialog.  */

static void
add_from_user (EMsgComposerAttachmentBar *bar)
{
	EMsgComposer *composer;
	GPtrArray *file_list;
	gboolean is_inline = FALSE;
	int i;
	
	composer = E_MSG_COMPOSER (gtk_widget_get_toplevel (GTK_WIDGET (bar)));
	
	file_list = e_msg_composer_select_file_attachments (composer, &is_inline);
	if (!file_list)
		return;
	
	for (i = 0; i < file_list->len; i++) {
		add_from_file (bar, file_list->pdata[i], is_inline ? "inline" : "attachment");
		g_free (file_list->pdata[i]);
	}
	
	g_ptr_array_free (file_list, TRUE);
}


/* Callbacks.  */

static void
add_cb (GtkWidget *widget, gpointer data, GtkWidget *for_widget)
{
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (data));
	
	add_from_user (E_MSG_COMPOSER_ATTACHMENT_BAR (data));
}

static void
properties_cb (GtkWidget *widget, gpointer data, GtkWidget *for_widget)
{
	EMsgComposerAttachmentBar *bar;
	
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (data));
	
	bar = E_MSG_COMPOSER_ATTACHMENT_BAR (data);
	edit_selected (data);
}

static void
remove_cb (GtkWidget *widget, gpointer data, GtkWidget *for_widget)
{
	EMsgComposerAttachmentBar *bar;
	
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (data));
	
	bar = E_MSG_COMPOSER_ATTACHMENT_BAR (data);
	remove_selected (bar);
}


/* Popup menu handling.  */

static GnomeUIInfo icon_context_menu_info[] = {
	GNOMEUIINFO_ITEM (N_("Remove"),
			  N_("Remove selected items from the attachment list"),
			  remove_cb, NULL),
	GNOMEUIINFO_MENU_PROPERTIES_ITEM (properties_cb, NULL),
	GNOMEUIINFO_END
};

static GtkWidget *
get_icon_context_menu (EMsgComposerAttachmentBar *bar)
{
	EMsgComposerAttachmentBarPrivate *priv;
	
	priv = bar->priv;
	if (priv->icon_context_menu == NULL)
		priv->icon_context_menu = gnome_popup_menu_new (icon_context_menu_info);
	
	return priv->icon_context_menu;
}

static void
popup_icon_context_menu (EMsgComposerAttachmentBar *bar,
			 gint num,
			 GdkEventButton *event)
{
	GtkWidget *menu;
	
	menu = get_icon_context_menu (bar);
	gnome_popup_menu_do_popup (menu, NULL, NULL, event, bar, NULL);
}

static GnomeUIInfo context_menu_info[] = {
	GNOMEUIINFO_ITEM (N_("Add attachment..."),
			  N_("Attach a file to the message"),
			  add_cb, NULL),
	GNOMEUIINFO_END
};

static GtkWidget *
get_context_menu (EMsgComposerAttachmentBar *bar)
{
	EMsgComposerAttachmentBarPrivate *priv;
	
	priv = bar->priv;
	if (priv->context_menu == NULL)
		priv->context_menu = gnome_popup_menu_new (context_menu_info);
	
	return priv->context_menu;
}

static void
popup_context_menu (EMsgComposerAttachmentBar *bar,
		    GdkEventButton *event)
{
	GtkWidget *menu;
	
	menu = get_context_menu (bar);
	gnome_popup_menu_do_popup (menu, NULL, NULL, event, bar, NULL);
}


/* GtkObject methods.  */

static void
destroy (GtkObject *object)
{
	EMsgComposerAttachmentBar *bar;
	
	bar = E_MSG_COMPOSER_ATTACHMENT_BAR (object);
	
	if (bar->priv) {
		free_attachment_list (bar);
		g_free (bar->priv);
		bar->priv = NULL;
	}
	
	if (GTK_OBJECT_CLASS (parent_class)->destroy != NULL)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* GtkWidget methods.  */

static gint
button_press_event (GtkWidget *widget,
		    GdkEventButton *event)
{
	EMsgComposerAttachmentBar *bar;
	GnomeIconList *icon_list;
	int icon_number;
	
	bar = E_MSG_COMPOSER_ATTACHMENT_BAR (widget);
	icon_list = GNOME_ICON_LIST (widget);
	
	if (event->button != 3)
		return GTK_WIDGET_CLASS (parent_class)->button_press_event (widget, event);
	
	icon_number = gnome_icon_list_get_icon_at (icon_list, event->x, event->y);
	
	if (icon_number >= 0) {
		gnome_icon_list_select_icon (icon_list, icon_number);
		popup_icon_context_menu (bar, icon_number, event);
	} else {
		popup_context_menu (bar, event);
	}
	
	return TRUE;
}


/* Initialization.  */

static void
class_init (EMsgComposerAttachmentBarClass *klass)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeIconListClass *icon_list_class;
	
	object_class = GTK_OBJECT_CLASS (klass);
	widget_class = GTK_WIDGET_CLASS (klass);
	icon_list_class = GNOME_ICON_LIST_CLASS (klass);
	
	parent_class = g_type_class_ref (gnome_icon_list_get_type ());
	
	object_class->destroy = destroy;
	
	widget_class->button_press_event = button_press_event;
	
	/* Setup signals.  */
	
	signals[CHANGED] =
		g_signal_new ("changed",
			      E_TYPE_MSG_COMPOSER_ATTACHMENT_BAR,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EMsgComposerAttachmentBarClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
init (EMsgComposerAttachmentBar *bar)
{
	EMsgComposerAttachmentBarPrivate *priv;
	
	priv = g_new (EMsgComposerAttachmentBarPrivate, 1);
	
	priv->attachments = NULL;
	priv->context_menu = NULL;
	priv->icon_context_menu = NULL;
	
	priv->num_attachments = 0;
	
	bar->priv = priv;
}


GType
e_msg_composer_attachment_bar_get_type (void)
{
	static GType type = 0;
	
	if (type == 0) {
		static const GTypeInfo info = {
			sizeof (EMsgComposerAttachmentBarClass),
			NULL, NULL,
			(GClassInitFunc) class_init,
			NULL, NULL,
			sizeof (EMsgComposerAttachmentBar),
			0,
			(GInstanceInitFunc) init,
		};
		
		type = g_type_register_static (GNOME_TYPE_ICON_LIST, "EMsgComposerAttachmentBar", &info, 0);
	}
	
	return type;
}

GtkWidget *
e_msg_composer_attachment_bar_new (GtkAdjustment *adj)
{
	EMsgComposerAttachmentBar *new;
	GnomeIconList *icon_list;
	int width, height, icon_width, window_height;
	PangoFontMetrics *metrics;
	PangoContext *context;
	
	new = g_object_new (e_msg_composer_attachment_bar_get_type (), NULL);
	
	icon_list = GNOME_ICON_LIST (new);
	
	context = gtk_widget_get_pango_context ((GtkWidget *) new);
	metrics = pango_context_get_metrics (context, ((GtkWidget *) new)->style->font_desc, pango_context_get_language (context));
	width = PANGO_PIXELS (pango_font_metrics_get_approximate_char_width (metrics)) * 15;
	/* This should be *2, but the icon list creates too much space above ... */
	height = PANGO_PIXELS (pango_font_metrics_get_ascent (metrics) + pango_font_metrics_get_descent (metrics)) * 3;
	pango_font_metrics_unref (metrics);
	
	icon_width = ICON_WIDTH + ICON_SPACING + ICON_BORDER + ICON_TEXT_SPACING;
	icon_width = MAX (icon_width, width);
	
	gnome_icon_list_construct (icon_list, icon_width, adj, 0);
	
	window_height = ICON_WIDTH + ICON_SPACING + ICON_BORDER + ICON_TEXT_SPACING + height;
	gtk_widget_set_size_request (GTK_WIDGET (new), icon_width * 4, window_height);
	
	gnome_icon_list_set_separators (icon_list, ICON_SEPARATORS);
	gnome_icon_list_set_row_spacing (icon_list, ICON_ROW_SPACING);
	gnome_icon_list_set_col_spacing (icon_list, ICON_COL_SPACING);
	gnome_icon_list_set_icon_border (icon_list, ICON_BORDER);
	gnome_icon_list_set_text_spacing (icon_list, ICON_TEXT_SPACING);
	gnome_icon_list_set_selection_mode (icon_list, GTK_SELECTION_MULTIPLE);
	
	return GTK_WIDGET (new);
}

static const char *
get_default_charset (void)
{
	GConfClient *gconf;
	const char *charset;
	char *buf;
	
	gconf = gconf_client_get_default ();
	buf = gconf_client_get_string (gconf, "/apps/evolution/mail/composer/charset", NULL);
	g_object_unref (gconf);
	
	if (buf != NULL) {
		charset = e_iconv_charset_name (buf);
		g_free (buf);
	} else
		charset = e_iconv_locale_charset ();
	
	return charset;
}

static void
attach_to_multipart (CamelMultipart *multipart,
		     EMsgComposerAttachment *attachment,
		     const char *default_charset)
{
	CamelContentType *content_type;
	CamelDataWrapper *content;
	
	content_type = camel_mime_part_get_content_type (attachment->body);
	content = camel_medium_get_content_object (CAMEL_MEDIUM (attachment->body));
	
	if (!CAMEL_IS_MULTIPART (content)) {
		if (header_content_type_is (content_type, "text", "*")) {
			CamelMimePartEncodingType encoding;
			CamelStreamFilter *filtered_stream;
			CamelMimeFilterBestenc *bestenc;
			CamelStream *stream;
			const char *charset;
			char *type;
			
			/* assume that if a charset is set, that the content is in UTF-8
			 * or else already has rawtext set to TRUE */
			if (!(charset = header_content_type_param (content_type, "charset"))) {
				/* Let camel know that this text part was read in raw and thus is not in
				 * UTF-8 format so that when it writes this part out, it doesn't try to
				 * convert it from UTF-8 into the @default_charset charset. */
				content->rawtext = TRUE;
			}
			
			stream = camel_stream_null_new ();
			filtered_stream = camel_stream_filter_new_with_stream (stream);
			bestenc = camel_mime_filter_bestenc_new (CAMEL_BESTENC_GET_ENCODING);
			camel_stream_filter_add (filtered_stream, CAMEL_MIME_FILTER (bestenc));
			camel_object_unref (CAMEL_OBJECT (stream));
			
			camel_data_wrapper_write_to_stream (content, CAMEL_STREAM (filtered_stream));
			camel_object_unref (CAMEL_OBJECT (filtered_stream));
			
			encoding = camel_mime_filter_bestenc_get_best_encoding (bestenc, CAMEL_BESTENC_8BIT);
			camel_mime_part_set_encoding (attachment->body, encoding);
			
			if (encoding == CAMEL_MIME_PART_ENCODING_7BIT) {
				/* the text fits within us-ascii so this is safe */
				/* FIXME: check that this isn't iso-2022-jp? */
				default_charset = "us-ascii";
			} else if (!charset) {
				if (!default_charset)
					default_charset = get_default_charset ();
				
				/* FIXME: We should really check that this fits within the
                                   default_charset and if not find one that does and/or
				   allow the user to specify? */
			}
			
			if (!charset) {
				/* looks kinda nasty, but this is how ya have to do it */
				header_content_type_set_param (content_type, "charset", default_charset);
				type = header_content_type_format (content_type);
				camel_mime_part_set_content_type (attachment->body, type);
				g_free (type);
			}
			
			camel_object_unref (CAMEL_OBJECT (bestenc));
		} else if (!CAMEL_IS_MIME_MESSAGE (content)) {
			camel_mime_part_set_encoding (attachment->body,
						      CAMEL_MIME_PART_ENCODING_BASE64);
		}
	}
	
	camel_multipart_add_part (multipart, attachment->body);
}

void
e_msg_composer_attachment_bar_to_multipart (EMsgComposerAttachmentBar *bar,
					    CamelMultipart *multipart,
					    const char *default_charset)
{
	EMsgComposerAttachmentBarPrivate *priv;
	GList *p;
	
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (bar));
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	
	priv = bar->priv;
	
	for (p = priv->attachments; p != NULL; p = p->next) {
		EMsgComposerAttachment *attachment;
		
		attachment = E_MSG_COMPOSER_ATTACHMENT (p->data);
		attach_to_multipart (multipart, attachment, default_charset);
	}
}


guint
e_msg_composer_attachment_bar_get_num_attachments (EMsgComposerAttachmentBar *bar)
{
	g_return_val_if_fail (bar != NULL, 0);
	g_return_val_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (bar), 0);
	
	return bar->priv->num_attachments;
}


void
e_msg_composer_attachment_bar_attach (EMsgComposerAttachmentBar *bar,
				      const gchar *file_name)
{
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (bar));
	
	if (file_name == NULL)
		add_from_user (bar);
	else
		add_from_file (bar, file_name, "attachment");
}

void
e_msg_composer_attachment_bar_attach_mime_part (EMsgComposerAttachmentBar *bar,
						CamelMimePart *part)
{
	g_return_if_fail (E_IS_MSG_COMPOSER_ATTACHMENT_BAR (bar));
	
	add_from_mime_part (bar, part);
}
