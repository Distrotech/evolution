/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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

#include <gtk/gtkvbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkvpaned.h>

#include <libgnomeprintui/gnome-print-dialog.h>

#include "mail-config.h"

#include <camel/camel-mime-message.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-url.h>

#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-browser.h"
#include "message-list.h"

struct _EMFolderBrowserPrivate {
	GtkWidget *preview;	/* container for message display */

	int show_preview:1;
	int show_list:1;
};

static GtkVBoxClass *emfb_parent;

/* Needed since the paned wont take the position its given otherwise ... */
static void
paned_realised(GtkWidget *w, EMFolderBrowser *emfb)
{
	GConfClient *gconf;

	gconf = mail_config_get_gconf_client ();
	gtk_paned_set_position((GtkPaned *)emfb->vpane, gconf_client_get_int(gconf, "/apps/evolution/mail/display/paned_size", NULL));
}

static void
emfb_init(GObject *o)
{
	EMFolderBrowser *emfb = (EMFolderBrowser *)o;
	struct _EMFolderBrowserPrivate *p;

	p = emfb->priv = g_malloc0(sizeof(struct _EMFolderBrowserPrivate));

	emfb->view.preview = (EMFormatHTMLDisplay *)em_format_html_display_new();

	/* FIXME: setup search bar */

	emfb->vpane = gtk_vpaned_new();
	g_signal_connect(emfb->vpane, "realize", G_CALLBACK(paned_realised), emfb);
	gtk_widget_show(emfb->vpane);

	gtk_box_pack_start_defaults((GtkBox *)emfb, emfb->vpane);
	
	gtk_paned_add1((GtkPaned *)emfb->vpane, (GtkWidget *)emfb->view.list);
	gtk_widget_show((GtkWidget *)emfb->view.list);

	/* currently: just use a scrolledwindow for preview widget */
	p->preview = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy((GtkScrolledWindow *)p->preview, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type((GtkScrolledWindow *)p->preview, GTK_SHADOW_IN);
	gtk_widget_show(p->preview);

	gtk_container_add((GtkContainer *)p->preview, (GtkWidget *)emfb->view.preview->formathtml.html);
	gtk_widget_show((GtkWidget *)emfb->view.preview->formathtml.html);

	gtk_paned_add2((GtkPaned *)emfb->vpane, p->preview);
	gtk_widget_show(p->preview);

	/* FIXME: setup selection */
	/* FIXME: setup dnd */
}

static void
emfb_finalise(GObject *o)
{
	EMFolderBrowser *emfb = (EMFolderBrowser *)o;

	g_free(emfb->priv);

	((GObjectClass *)emfb_parent)->finalize(o);
}

static void
emfb_class_init(GObjectClass *klass)
{
	klass->finalize = emfb_finalise;
}

GType
em_folder_browser_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFolderBrowserClass),
			NULL, NULL,
			(GClassInitFunc)emfb_class_init,
			NULL, NULL,
			sizeof(EMFolderBrowser), 0,
			(GInstanceInitFunc)emfb_init
		};
		emfb_parent = g_type_class_ref(em_folder_view_get_type());
		type = g_type_register_static(em_folder_view_get_type(), "EMFolderBrowser", &info, 0);
	}

	return type;
}

GtkWidget *em_folder_browser_new(void)
{
	EMFolderBrowser *emfb = g_object_new(em_folder_browser_get_type(), 0);

	return (GtkWidget *)emfb;
}

void em_folder_browser_show_preview(EMFolderBrowser *emfb, gboolean state)
{
	if (emfb->priv->show_preview
	    || emfb->view.list == NULL)
		return;
	
	emfb->priv->show_preview = state;
	
	if (state) {
		GConfClient *gconf = mail_config_get_gconf_client ();
		int paned_size /*, y*/;

		paned_size = gconf_client_get_int (gconf, "/apps/evolution/mail/display/paned_size", NULL);

		/*y = save_cursor_pos (emfb);*/
		gtk_paned_set_position (GTK_PANED (emfb->vpane), paned_size);
		gtk_widget_show (GTK_WIDGET (emfb->priv->preview));
		/* need to load/show the current message? */
		/*do_message_selected (emfb);*/
		/*set_cursor_pos (emfb, y);*/
	} else {
		gtk_widget_hide(emfb->priv->preview);
		/*
		mail_display_set_message (emfb->mail_display, NULL, NULL, NULL);
		emfb_ui_message_loaded (emfb);*/
	}
}
