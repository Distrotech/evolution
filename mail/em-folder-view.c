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

#include <string.h>

#include <gtk/gtkvbox.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkvpaned.h>

#include <gtkhtml/gtkhtml.h>

#include <libgnome/gnome-url.h>

#include <libgnomeprintui/gnome-print-dialog.h>

#include <camel/camel-mime-message.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-multipart.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-url.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-object.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>

#include "widgets/misc/e-charset-picker.h"

#include <e-util/e-dialog-utils.h>

#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-view.h"
#include "em-message-browser.h"
#include "message-list.h"
#include "em-utils.h"
#include "em-popup.h"

#include <gtkhtml/gtkhtml.h>
#include <gtkhtml/htmlobject.h>
#include <gtkhtml/htmlengine.h>
#include <gtkhtml/htmlengine-save.h>

#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-config.h"	/* hrm, pity we need this ... */
#include "mail-autofilter.h"
#include "mail-vfolder.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff, sigh */

static void emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv);
static int emfv_list_right_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv);

static void emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *);
static int emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *);

static void emfv_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri);
static void emfv_set_folder_uri(EMFolderView *emfv, const char *uri);
static void emfv_set_message(EMFolderView *emfv, const char *uid);
static void emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state);

static void emfv_message_reply(EMFolderView *emfv, int mode);
static void vfolder_type_current (EMFolderView *emfv, int type);
static void filter_type_current (EMFolderView *emfv, int type);

struct _EMFolderViewPrivate {
	guint seen_id;
};

static GtkVBoxClass *emfv_parent;

static void
emfv_init(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;
	struct _EMFolderViewPrivate *p;

	gtk_box_set_homogeneous (GTK_BOX (emfv), FALSE);

	p = emfv->priv = g_malloc0(sizeof(struct _EMFolderViewPrivate));

	emfv->ui_files = g_slist_append(NULL, EVOLUTION_UIDIR "/evolution-mail-message.xml");
	emfv->ui_app_name = "evolution-mail";

	emfv->list = (MessageList *)message_list_new();
	g_signal_connect(emfv->list, "message_selected", G_CALLBACK(emfv_list_message_selected), emfv);

	/* FIXME: should this hang off message-list instead? */
	g_signal_connect(emfv->list->tree, "right_click", G_CALLBACK(emfv_list_right_click), emfv);

	emfv->preview = (EMFormatHTMLDisplay *)em_format_html_display_new();
	g_signal_connect(emfv->preview, "link_clicked", G_CALLBACK(emfv_format_link_clicked), emfv);
	g_signal_connect(emfv->preview, "popup_event", G_CALLBACK(emfv_format_popup_event), emfv);

	/* setup selection?  etc? */
}

static void
emfv_finalise(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;

	g_free(emfv->priv);

	((GObjectClass *)emfv_parent)->finalize(o);
}

static void
emfv_destroy (GtkObject *o)
{
	EMFolderView *emfv = (EMFolderView *) o;
	
	if (emfv->priv->seen_id) {
		gtk_timeout_remove (emfv->priv->seen_id);
		emfv->priv->seen_id = 0;
	}
	
	((GtkObjectClass *) emfv_parent)->destroy (o);
}

static void
emfv_class_init(GObjectClass *klass)
{
	klass->finalize = emfv_finalise;
	
	((GtkObjectClass *) klass)->destroy = emfv_destroy;
	
	((EMFolderViewClass *)klass)->set_folder = emfv_set_folder;
	((EMFolderViewClass *)klass)->set_folder_uri = emfv_set_folder_uri;
	((EMFolderViewClass *)klass)->set_message = emfv_set_message;
	((EMFolderViewClass *)klass)->activate = emfv_activate;
}

GType
em_folder_view_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMFolderViewClass),
			NULL, NULL,
			(GClassInitFunc)emfv_class_init,
			NULL, NULL,
			sizeof(EMFolderView), 0,
			(GInstanceInitFunc)emfv_init
		};
		emfv_parent = g_type_class_ref(gtk_vbox_get_type());
		type = g_type_register_static(gtk_vbox_get_type(), "EMFolderView", &info, 0);
	}

	return type;
}

GtkWidget *em_folder_view_new(void)
{
	EMFolderView *emfv = g_object_new(em_folder_view_get_type(), 0);

	return (GtkWidget *)emfv;
}

/* flag all selected messages. Return number flagged */
/* FIXME: Should this be part of message-list instead? */
int
em_folder_view_mark_selected(EMFolderView *emfv, guint32 mask, guint32 set)
{
	GPtrArray *uids;
	int i;

	if (emfv->folder == NULL)
		return 0;
	
	uids = message_list_get_selected(emfv->list);
	camel_folder_freeze(emfv->folder);

	for (i=0; i<uids->len; i++)
		camel_folder_set_message_flags(emfv->folder, uids->pdata[i], mask, set);

	message_list_free_uids(emfv->list, uids);
	camel_folder_thaw(emfv->folder);
	
	return i;
}

/* should this be elsewhere/take a uid list? */
int
em_folder_view_open_selected(EMFolderView *emfv)
{
	GPtrArray *uids;
	int i = 0;

	/* FIXME: handle editing message?  Should be a different method? editing handled by 'Resend' method already */

	uids = message_list_get_selected(emfv->list);

	if (em_utils_folder_is_drafts(emfv->folder, emfv->folder_uri)
	    || em_utils_folder_is_outbox(emfv->folder, emfv->folder_uri)) {
		em_utils_edit_messages((GtkWidget *)emfv, emfv->folder, uids);
	} else {
		/* TODO: have an em_utils_open_messages call? */

		/* FIXME: 'are you sure' for > 10 messages; is this even necessary? */

		for (i=0; i<uids->len; i++) {
			EMMessageBrowser *emmb;

			emmb = (EMMessageBrowser *)em_message_browser_window_new();
			/* FIXME: session needs to be passed easier than this */
			em_format_set_session((EMFormat *)((EMFolderView *)emmb)->preview, ((EMFormat *)emfv->preview)->session);
			em_folder_view_set_folder((EMFolderView *)emmb, emfv->folder, emfv->folder_uri);
			printf("opening message '%s'\n", (char *)uids->pdata[i]);
			em_folder_view_set_message((EMFolderView *)emmb, uids->pdata[i]);
			gtk_widget_show(emmb->window);
		}

		message_list_free_uids(emfv->list, uids);
	}

	return i;
}

/* ********************************************************************** */

static void
emfv_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri)
{
	int isout = (folder && uri
		     && (em_utils_folder_is_drafts(folder, uri)
			 || em_utils_folder_is_sent(folder, uri)
			 || em_utils_folder_is_outbox(folder, uri)));

	message_list_set_folder(emfv->list, folder, uri, isout);
	g_free(emfv->folder_uri);
	emfv->folder_uri = g_strdup(uri);
	if (folder)
		camel_object_ref(folder);
	if (emfv->folder)
		camel_object_unref(emfv->folder);
	emfv->folder = folder;
}

static void
emfv_got_folder(char *uri, CamelFolder *folder, void *data)
{
	EMFolderView *emfv = data;

	em_folder_view_set_folder(emfv, folder, uri);
}

static void
emfv_set_folder_uri(EMFolderView *emfv, const char *uri)
{
	if (emfv->preview)
		em_format_format((EMFormat *)emfv->preview, NULL);

	mail_get_folder(uri, 0, emfv_got_folder, emfv, mail_thread_new);
}

static void
emfv_set_message(EMFolderView *emfv, const char *uid)
{
	message_list_select_uid(emfv->list, uid);
}

/* ********************************************************************** */

/* Popup menu
   In many cases these are the functions called by the bonobo callbacks too */

struct _emfv_label_item {
	EMPopupItem item;

	EMFolderView *emfv;
	const char *label;
};

static void
emfv_popup_open(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_open_selected(emfv);
}

static void
emfv_popup_resend(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids;

	if (!em_utils_check_user_can_send_mail((GtkWidget *)emfv))
		return;
	
	uids = message_list_get_selected(emfv->list);
	em_utils_edit_messages((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_saveas(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids;
	
	uids = message_list_get_selected(emfv->list);
	em_utils_save_messages((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_print(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_print(emfv, FALSE);
}

static void
emfv_popup_reply_sender(GtkWidget *w, EMFolderView *emfv)
{
	emfv_message_reply(emfv, REPLY_MODE_SENDER);
}

static void
emfv_popup_reply_list(GtkWidget *w, EMFolderView *emfv)
{
	emfv_message_reply(emfv, REPLY_MODE_LIST);
}

static void
emfv_popup_reply_all(GtkWidget *w, EMFolderView *emfv)
{
	emfv_message_reply(emfv, REPLY_MODE_ALL);
}

static void
emfv_popup_forward(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids;

	if (!em_utils_check_user_can_send_mail((GtkWidget *)emfv))
		return;

	uids = message_list_get_selected(emfv->list);
	em_utils_forward_messages((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_flag_followup(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids = message_list_get_selected(emfv->list);

	em_utils_flag_for_followup((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_flag_completed(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids;
	
	uids = message_list_get_selected(emfv->list);
	em_utils_flag_for_followup_completed((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_flag_clear(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids = message_list_get_selected(emfv->list);

	em_utils_flag_for_followup_clear((GtkWidget *)emfv, emfv->folder, uids);
}

static void
emfv_popup_mark_read(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

static void
emfv_popup_mark_unread(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED, 0);
	
	if (emfv->priv->seen_id) {
		g_source_remove(emfv->priv->seen_id);
		emfv->priv->seen_id = 0;
	}
}

static void
emfv_popup_mark_important(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_FLAGGED);
}

static void
emfv_popup_mark_unimportant(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_FLAGGED, 0);
}

static void
emfv_popup_delete(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids;
	
	uids = message_list_get_selected (emfv->list);
	em_folder_view_mark_selected (emfv, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED);
	
	if (uids->len == 1)
		message_list_select (emfv->list, MESSAGE_LIST_SELECT_NEXT, 0, 0, FALSE);
	
	em_utils_uids_free (uids);
}

static void
emfv_popup_undelete(GtkWidget *w, EMFolderView *emfv)
{
	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_DELETED, 0);
}

static void
emfv_popup_move(GtkWidget *w, EMFolderView *emfv)
{
	/* FIXME */
}

static void
emfv_popup_copy(GtkWidget *w, EMFolderView *emfv)
{
	/* FIXME */
}

static void
emfv_set_label(EMFolderView *emfv, const char *label)
{
	GPtrArray *uids = message_list_get_selected(emfv->list);
	int i;

	for (i=0;i<uids->len;i++)
		camel_folder_set_message_user_tag(emfv->folder, uids->pdata[i], "label", label);

	message_list_free_uids(emfv->list, uids);
}

static void
emfv_popup_label_clear(GtkWidget *w, EMFolderView *emfv)
{
	emfv_set_label(emfv, NULL);
}

static void
emfv_popup_label_set(GtkWidget *w, struct _emfv_label_item *item)
{
	emfv_set_label(item->emfv, item->label);
}

static void
emfv_popup_add_sender(GtkWidget *w, EMFolderView *emfv)
{
	/* FIXME */
}

static void
emfv_popup_apply_filters(GtkWidget *w, EMFolderView *emfv)
{
	GPtrArray *uids = message_list_get_selected(emfv->list);

	mail_filter_on_demand(emfv->folder, uids);
}

/* filter callbacks, this will eventually be a wizard, see
   filter_type_current/vfolder_type_current for implementation */

#define EMFV_POPUP_AUTO_TYPE(autotype, name, type)	\
static void						\
name(GtkWidget *w, EMFolderView *emfv)			\
{							\
	autotype(emfv, type);				\
}

EMFV_POPUP_AUTO_TYPE(vfolder_type_current, emfv_popup_vfolder_subject, AUTO_SUBJECT)
EMFV_POPUP_AUTO_TYPE(vfolder_type_current, emfv_popup_vfolder_sender, AUTO_FROM)
EMFV_POPUP_AUTO_TYPE(vfolder_type_current, emfv_popup_vfolder_recipients, AUTO_TO)
EMFV_POPUP_AUTO_TYPE(vfolder_type_current, emfv_popup_vfolder_mlist, AUTO_MLIST)

EMFV_POPUP_AUTO_TYPE(filter_type_current, emfv_popup_filter_subject, AUTO_SUBJECT)
EMFV_POPUP_AUTO_TYPE(filter_type_current, emfv_popup_filter_sender, AUTO_FROM)
EMFV_POPUP_AUTO_TYPE(filter_type_current, emfv_popup_filter_recipients, AUTO_TO)
EMFV_POPUP_AUTO_TYPE(filter_type_current, emfv_popup_filter_mlist, AUTO_MLIST)

enum {
	CAN_SELECT_ONE              = 1<<1,
	CAN_MARK_READ              = 1<<2,
	CAN_MARK_UNREAD            = 1<<3,
	CAN_DELETE                 = 1<<4,
	CAN_UNDELETE               = 1<<5,
	CAN_MAILING_LIST            = 1<<6,
	CAN_RESEND                 = 1<<7,
	CAN_MARK_IMPORTANT         = 1<<8,
	CAN_MARK_UNIMPORTANT       = 1<<9,
	CAN_FLAG_FOLLOWUP      = 1<<10,
	CAN_FLAG_COMPLETED         = 1<<11,
	CAN_FLAG_CLEAR             = 1<<12,
	CAN_ADD_SENDER             = 1<<13
};

static EMPopupItem emfv_popup_menu[] = {
	{ EM_POPUP_ITEM, "00.emfv.00", N_("_Open"), G_CALLBACK(emfv_popup_open), NULL, NULL, 0 },
	{ EM_POPUP_ITEM, "00.emfv.01", N_("_Edit as New Message..."), G_CALLBACK(emfv_popup_resend), NULL, NULL, CAN_RESEND },
	{ EM_POPUP_ITEM, "00.emfv.02", N_("_Save As..."), G_CALLBACK(emfv_popup_saveas), NULL, "save-as-16.png", 0 },
	{ EM_POPUP_ITEM, "00.emfv.03", N_("_Print"), G_CALLBACK(emfv_popup_print), NULL, "print.xpm", 0 },

	{ EM_POPUP_BAR, "10.emfv" },
	{ EM_POPUP_ITEM, "10.emfv.00", N_("_Reply to Sender"), G_CALLBACK(emfv_popup_reply_sender), NULL, "reply.xpm" },
	{ EM_POPUP_ITEM, "10.emfv.01", N_("Reply to _List"), G_CALLBACK(emfv_popup_reply_list) },
	{ EM_POPUP_ITEM, "10.emfv.02", N_("Reply to _All"), G_CALLBACK(emfv_popup_reply_all), NULL, "reply_to_all.xpm" },
	{ EM_POPUP_ITEM, "10.emfv.03", N_("_Forward"), G_CALLBACK(emfv_popup_forward), NULL, "forward.xpm" },

	{ EM_POPUP_BAR, "20.emfv", NULL, NULL, NULL, NULL, CAN_FLAG_FOLLOWUP|CAN_FLAG_COMPLETED|CAN_FLAG_CLEAR },
	{ EM_POPUP_ITEM, "20.emfv.00", N_("Follo_w Up..."), G_CALLBACK(emfv_popup_flag_followup), NULL, "flag-for-followup-16.png",  CAN_FLAG_FOLLOWUP },
	{ EM_POPUP_ITEM, "20.emfv.01", N_("Fla_g Completed"), G_CALLBACK(emfv_popup_flag_completed), NULL, NULL, CAN_FLAG_COMPLETED },
	{ EM_POPUP_ITEM, "20.emfv.02", N_("Cl_ear Flag"), G_CALLBACK(emfv_popup_flag_clear), NULL, NULL, CAN_FLAG_CLEAR },
	
	{ EM_POPUP_BAR, "30.emfv" },
	{ EM_POPUP_ITEM, "30.emfv.00", N_("Mar_k as Read"), G_CALLBACK(emfv_popup_mark_read), NULL, "mail-read.xpm", CAN_MARK_READ },
	{ EM_POPUP_ITEM,  "30.emfv.01", N_("Mark as _Unread"), G_CALLBACK(emfv_popup_mark_unread), NULL, "mail-new.xpm", CAN_MARK_UNREAD },
	{ EM_POPUP_ITEM, "30.emfv.02", N_("Mark as _Important"), G_CALLBACK(emfv_popup_mark_important), NULL, "priority-high.xpm", CAN_MARK_IMPORTANT },
	{ EM_POPUP_ITEM, "30.emfv.03", N_("_Mark as Unimportant"), G_CALLBACK(emfv_popup_mark_unimportant), NULL, NULL, CAN_MARK_UNIMPORTANT },
	
	{ EM_POPUP_BAR, "40.emfv" },
	{ EM_POPUP_ITEM, "40.emfv.00", N_("_Delete"), G_CALLBACK(emfv_popup_delete), NULL, "evolution-trash-mini.png", CAN_DELETE },
	{ EM_POPUP_ITEM, "40.emfv.01", N_("U_ndelete"), G_CALLBACK(emfv_popup_undelete), NULL, "undelete_message-16.png", CAN_UNDELETE },

	{ EM_POPUP_BAR, "50.emfv" },
	{ EM_POPUP_ITEM, "50.emfv.00", N_("Mo_ve to Folder..."), G_CALLBACK(emfv_popup_move) },
	{ EM_POPUP_ITEM, "50.emfv.01", N_("_Copy to Folder..."), G_CALLBACK(emfv_popup_copy) },

	{ EM_POPUP_BAR, "60.label" },
	{ EM_POPUP_SUBMENU, "60.label.00", N_("Label") },
	{ EM_POPUP_IMAGE, "60.label.00/00.label", N_("None"), G_CALLBACK(emfv_popup_label_clear) },
	{ EM_POPUP_BAR, "60.label.00/00.label.00" },

	{ EM_POPUP_BAR, "70.emfv", NULL, NULL, NULL, NULL, CAN_SELECT_ONE|CAN_ADD_SENDER },	
	{ EM_POPUP_ITEM, "70.emfv.00", N_("Add Sender to Address_book"), G_CALLBACK(emfv_popup_add_sender), NULL, NULL, CAN_SELECT_ONE|CAN_ADD_SENDER },

	{ EM_POPUP_BAR, "80.emfv" },	
	{ EM_POPUP_ITEM, "80.emfv.00", N_("Appl_y Filters"), G_CALLBACK(emfv_popup_apply_filters) },

	{ EM_POPUP_BAR, "90.filter" },
	{ EM_POPUP_SUBMENU, "90.filter.00", N_("Crea_te Rule From Message"), NULL, NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.00", N_("VFolder on _Subject"), G_CALLBACK(emfv_popup_vfolder_subject), NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.01", N_("VFolder on Se_nder"), G_CALLBACK(emfv_popup_vfolder_sender), NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.02", N_("VFolder on _Recipients"), G_CALLBACK(emfv_popup_vfolder_recipients), NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.03", N_("VFolder on Mailing _List"),
	  G_CALLBACK(emfv_popup_vfolder_mlist), NULL, NULL, CAN_SELECT_ONE|CAN_MAILING_LIST },

	{ EM_POPUP_BAR, "90.filter.00/10" },
	{ EM_POPUP_ITEM, "90.filter.00/10.00", N_("Filter on Sub_ject"), G_CALLBACK(emfv_popup_filter_subject), NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.01", N_("Filter on Sen_der"), G_CALLBACK(emfv_popup_filter_sender), NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.02", N_("Filter on Re_cipients"), G_CALLBACK(emfv_popup_filter_recipients),  NULL, NULL, CAN_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.03", N_("Filter on _Mailing List"),
	  G_CALLBACK(emfv_popup_filter_mlist), NULL, NULL, CAN_SELECT_ONE|CAN_MAILING_LIST },
};

static void
emfv_popup_labels_free(void *data)
{
	GSList *l = data;

	while (l) {
		GSList *n = l->next;
		struct _emfv_label_item *item = l->data;

		g_free(item->item.path);
		g_free(item);

		g_slist_free_1(l);
		l = n;
	}
}
       
static void
emfv_popup(EMFolderView *emfv, GdkEvent *event)
{
	GSList *menus = NULL, *l, *label_list = NULL;
	GPtrArray *uids;
	CamelMessageInfo *info;
	int disable_mask = 0, hide_mask;
	GtkMenu *menu;
	EMPopup *emp;
	int i;
	const char *tmp;

	emp = em_popup_new();

	for (i=0;i<sizeof(emfv_popup_menu)/sizeof(emfv_popup_menu[0]);i++) {
		EMPopupItem *item = &emfv_popup_menu[i];

		item->activate_data = emfv;
		menus = g_slist_prepend(menus, item);
	}

	em_popup_add_items(emp, menus, (GDestroyNotify)g_slist_free);

	i = 1;
	for (l = mail_config_get_labels(); l; l = l->next) {
		struct _emfv_label_item *item;
		MailConfigLabel *label = l->data;
		GdkPixmap *pixmap;
		GdkColor colour;
		GdkGC *gc;
		
		item = g_malloc0(sizeof(*item));
		item->item.type = EM_POPUP_IMAGE;
		item->item.path = g_strdup_printf("60.label.00/00.label.%02d", i++);
		item->item.label = label->name;
		item->item.activate = G_CALLBACK(emfv_popup_label_set);
		item->item.activate_data = item;
		item->emfv = emfv;
		item->label = label->tag;

		gdk_color_parse(label->colour, &colour);
		gdk_color_alloc(gdk_colormap_get_system(), &colour);
		
		pixmap = gdk_pixmap_new(((GtkWidget *)emfv)->window, 16, 16, -1);
		gc = gdk_gc_new(((GtkWidget *)emfv)->window);
		gdk_gc_set_foreground(gc, &colour);
		gdk_draw_rectangle(pixmap, gc, TRUE, 0, 0, 16, 16);
		gdk_gc_unref(gc);

		item->item.image = gtk_image_new_from_pixmap(pixmap, NULL);
		gtk_widget_show(item->item.image);

		label_list = g_slist_prepend(label_list, item);
	}

	em_popup_add_items(emp, label_list, emfv_popup_labels_free);

	disable_mask = CAN_SELECT_ONE | CAN_MAILING_LIST
		| CAN_MARK_READ | CAN_MARK_UNREAD
		| CAN_DELETE | CAN_UNDELETE
		| CAN_MARK_IMPORTANT | CAN_MARK_UNIMPORTANT
		| CAN_FLAG_CLEAR | CAN_FLAG_FOLLOWUP | CAN_FLAG_COMPLETED;

	if (em_utils_folder_is_sent(emfv->folder, emfv->folder_uri))
		disable_mask |= CAN_ADD_SENDER;
	else
		disable_mask |= CAN_RESEND;

	if (em_utils_folder_is_drafts(emfv->folder, emfv->folder_uri))
		disable_mask |= CAN_ADD_SENDER;
	
	if (em_utils_folder_is_outbox(emfv->folder, emfv->folder_uri))
		disable_mask |= CAN_ADD_SENDER;

	uids = message_list_get_selected(emfv->list);

	if (uids->len == 1)
		disable_mask &= ~CAN_SELECT_ONE;

	for (i = 0; i < uids->len; i++) {
		info = camel_folder_get_message_info(emfv->folder, uids->pdata[i]);
		if (info == NULL)
			continue;

		if (info->flags & CAMEL_MESSAGE_SEEN)
			disable_mask &= ~CAN_MARK_UNREAD;
		else
			disable_mask &= ~CAN_MARK_READ;
		
		if (info->flags & CAMEL_MESSAGE_DELETED)
			disable_mask &= ~CAN_UNDELETE;
		else
			disable_mask &= ~CAN_DELETE;

		if (info->flags & CAMEL_MESSAGE_FLAGGED)
			disable_mask &= ~CAN_MARK_UNIMPORTANT;
		else
			disable_mask &= ~CAN_MARK_IMPORTANT;
			
		tmp = camel_tag_get (&info->user_tags, "follow-up");
		if (tmp && *tmp) {
			disable_mask &= ~CAN_FLAG_CLEAR;
			tmp = camel_tag_get(&info->user_tags, "completed-on");
			if (tmp == NULL || *tmp == 0)
				disable_mask &= ~CAN_FLAG_COMPLETED;
		} else
			disable_mask &= ~CAN_FLAG_FOLLOWUP;

		if (i == 0 && uids->len == 1
		    && (tmp = camel_message_info_mlist(info))
		    && tmp[0] != 0)
			disable_mask &= ~CAN_MAILING_LIST;

		camel_folder_free_message_info(emfv->folder, info);
	}

	/* uh, hide_mask probably just = disable_mask & ~SELECTED */
	hide_mask = disable_mask & (CAN_ADD_SENDER|CAN_RESEND
				    |CAN_MARK_READ|CAN_MARK_UNREAD
				    |CAN_DELETE|CAN_UNDELETE
				    |CAN_MARK_IMPORTANT|CAN_MARK_UNIMPORTANT
				    |CAN_FLAG_CLEAR|CAN_FLAG_FOLLOWUP|CAN_FLAG_COMPLETED);

	menu = em_popup_create_menu_once(emp, hide_mask, disable_mask);

	if (event == NULL ||  event->type == GDK_KEY_PRESS) {
		/* FIXME: menu pos function */
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL, 0, event->key.time);
	} else {
		gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button.button, event->button.time);
	}
}

/* ********************************************************************** */

/* Bonobo menu's */

/* a lot of stuff maps directly to the popup menu equivalent */
#define EMFV_MAP_CALLBACK(from, to)				\
static void							\
from(BonoboUIComponent *uid, void *data, const char *path)	\
{								\
	to(NULL, (EMFolderView *)data);				\
}

EMFV_MAP_CALLBACK(emfv_message_apply_filters, emfv_popup_apply_filters)
EMFV_MAP_CALLBACK(emfv_message_copy, emfv_popup_copy)
EMFV_MAP_CALLBACK(emfv_message_move, emfv_popup_move)
EMFV_MAP_CALLBACK(emfv_message_forward, emfv_popup_forward)
EMFV_MAP_CALLBACK(emfv_message_reply_all, emfv_popup_reply_all)
EMFV_MAP_CALLBACK(emfv_message_reply_list, emfv_popup_reply_list)
EMFV_MAP_CALLBACK(emfv_message_reply_sender, emfv_popup_reply_sender)
EMFV_MAP_CALLBACK(emfv_message_mark_read, emfv_popup_mark_read)
EMFV_MAP_CALLBACK(emfv_message_mark_unread, emfv_popup_mark_unread)
EMFV_MAP_CALLBACK(emfv_message_mark_important, emfv_popup_mark_important)
EMFV_MAP_CALLBACK(emfv_message_mark_unimportant, emfv_popup_mark_unimportant)
EMFV_MAP_CALLBACK(emfv_message_delete, emfv_popup_delete)
EMFV_MAP_CALLBACK(emfv_message_undelete, emfv_popup_undelete)
EMFV_MAP_CALLBACK(emfv_message_followup_flag, emfv_popup_flag_followup)
/*EMFV_MAP_CALLBACK(emfv_message_followup_clear, emfv_popup_flag_clear)
  EMFV_MAP_CALLBACK(emfv_message_followup_completed, emfv_popup_flag_completed)*/
EMFV_MAP_CALLBACK(emfv_message_open, emfv_popup_open)
EMFV_MAP_CALLBACK(emfv_message_resend, emfv_popup_resend)
EMFV_MAP_CALLBACK(emfv_message_saveas, emfv_popup_saveas)
EMFV_MAP_CALLBACK(emfv_print_message, emfv_popup_print)

static void
emfv_edit_cut(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (message_list_has_primary_selection(emfv->list))
		message_list_copy(emfv->list, TRUE);
	else
		em_format_html_display_cut(emfv->preview);
}

static void
emfv_edit_copy(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (message_list_has_primary_selection(emfv->list))
		message_list_copy(emfv->list, FALSE);
	else
		em_format_html_display_copy(emfv->preview);
}

static void
emfv_edit_paste(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_paste(emfv->list);
}

static void
emfv_mail_next(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_NEXT, 0, 0, FALSE);
}

static void
emfv_mail_next_flagged(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_NEXT, CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, FALSE);
}

static void
emfv_mail_next_unread(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_NEXT, 0, CAMEL_MESSAGE_SEEN, TRUE);
}

static void
emfv_mail_next_thread(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select_next_thread(emfv->list);
}

static void
emfv_mail_previous(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_PREVIOUS, 0, 0, FALSE);
}

static void
emfv_mail_previous_flagged(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_PREVIOUS, CAMEL_MESSAGE_FLAGGED, CAMEL_MESSAGE_FLAGGED, TRUE);
}

static void
emfv_mail_previous_unread(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_select(emfv->list, MESSAGE_LIST_SELECT_PREVIOUS, 0, CAMEL_MESSAGE_SEEN, TRUE);
}

static void
emfv_add_sender_addressbook(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	emfv = emfv;
	/* FIXME: need to find out what the new addressbook API is for this... */
}

static void
emfv_message_forward_attached (BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	GPtrArray *uids;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;
	
	uids = message_list_get_selected (emfv->list);
	em_utils_forward_attached ((GtkWidget *) emfv, emfv->folder, uids);
}

static void
emfv_message_forward_inline (BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	GPtrArray *uids;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;
	
	uids = message_list_get_selected (emfv->list);
	em_utils_forward_inline ((GtkWidget *) emfv, emfv->folder, uids);
}

static void
emfv_message_forward_quoted (BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	GPtrArray *uids;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;
	
	uids = message_list_get_selected (emfv->list);
	em_utils_forward_quoted ((GtkWidget *) emfv, emfv->folder, uids);
}

static void
emfv_message_redirect (BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	if (emfv->list->cursor_uid == NULL)
		return;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;
	
	em_utils_redirect_message_by_uid ((GtkWidget *) emfv, emfv->folder, emfv->list->cursor_uid);
}

static void
emfv_message_post_reply (BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	if (emfv->list->cursor_uid == NULL)
		return;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;
	
	em_utils_post_reply_to_message_by_uid ((GtkWidget *) emfv, emfv->folder, emfv->list->cursor_uid);
}

static void
emfv_message_reply(EMFolderView *emfv, int mode)
{
	/* GtkClipboard *clip; */

	if (emfv->list->cursor_uid == NULL)
		return;
	
	if (!em_utils_check_user_can_send_mail ((GtkWidget *) emfv))
		return;

	/*  Look away!  Look away! */

	/* HACK: Nasty internal gtkhtml poking going on here */

	/* Disabled since there's no simple way to find out if
	   gtkhtml has the primary selection right now */

	/* Ugh, to use the clipboard we need to request the selection
	   and have an async callback - painful to deal with */

	/*clip = gtk_clipboard_get(GDK_SELECTION_PRIMARY);*/
	if (FALSE /*gtk_clipboard_get_owner(clip) == (GObject *)emfv->preview*/
	    && ((EMFormatHTML *)emfv->preview)->html->engine->primary) {
		CamelMimeMessage *msg, *src;
		struct _header_raw *header;
		HTMLEngineSaveState *state;

		src = (CamelMimeMessage *)((EMFormat *)emfv->preview)->message;
		msg = camel_mime_message_new();

		header = ((CamelMimePart *)src)->headers;
		while (header) {
			/* FIXME: shouldn't we strip out *all* Content-* headers? */
			if (g_ascii_strcasecmp(header->name, "content-type") != 0)
				camel_medium_add_header((CamelMedium *)msg, header->name, header->value);
			header = header->next;
		}

		state = html_engine_save_buffer_new(((EMFormatHTML *)emfv->preview)->html->engine, TRUE);
		html_object_save(((EMFormatHTML *)emfv->preview)->html->engine->primary, state);
		camel_mime_part_set_content((CamelMimePart *)msg,
					    ((GString *)state->user_data)->str,
					    ((GString *)state->user_data)->len,
					    "text/html");

		html_engine_save_buffer_free(state);

		em_utils_reply_to_message((GtkWidget *)emfv, msg, mode);
		camel_object_unref(msg);
	} else {
		em_utils_reply_to_message_by_uid ((GtkWidget *) emfv, emfv->folder, emfv->list->cursor_uid, mode);
	}

	/*g_object_unref(clip);*/
}

static void
emfv_message_search(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_format_html_display_search(emfv->preview);
}

static void
emfv_print_preview_message(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_print(emfv, TRUE);
}

static void
emfv_text_zoom_in(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (emfv->preview)
		em_format_html_display_zoom_in(emfv->preview);
}

static void
emfv_text_zoom_out(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (emfv->preview)
		em_format_html_display_zoom_out(emfv->preview);
}

static void
emfv_text_zoom_reset(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (emfv->preview)
		em_format_html_display_zoom_reset(emfv->preview);
}

/* ********************************************************************** */

struct _filter_data {
	CamelFolder *folder;
	const char *source;
	char *uid;
	int type;
	char *uri;
	char *mlist;
};

static void
filter_data_free (struct _filter_data *fdata)
{
	g_free (fdata->uid);
	g_free (fdata->uri);
	if (fdata->folder)
		camel_object_unref (fdata->folder);
	g_free (fdata->mlist);
	g_free (fdata);
}

static void
filter_type_got_message (CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *user_data)
{
	struct _filter_data *data = user_data;
	
	if (msg)
		filter_gui_add_from_message (msg, data->source, data->type);
	
	filter_data_free (data);
}

static void
filter_type_uid (CamelFolder *folder, const char *uid, const char *source, int type)
{
	struct _filter_data *data;
	
	data = g_malloc0 (sizeof (*data));
	data->type = type;
	data->source = source;
	
	mail_get_message (folder, uid, filter_type_got_message, data, mail_thread_new);
}

static void
filter_type_current (EMFolderView *emfv, int type)
{
	const char *source;
	GPtrArray *uids;
	
	if (em_utils_folder_is_sent (emfv->folder, emfv->folder_uri)
	    || em_utils_folder_is_outbox (emfv->folder, emfv->folder_uri))
		source = FILTER_SOURCE_OUTGOING;
	else
		source = FILTER_SOURCE_INCOMING;
	
	uids = message_list_get_selected (emfv->list);
	
	if (uids->len == 1)
		filter_type_uid (emfv->folder, (char *) uids->pdata[0], source, type);
	
	em_utils_uids_free (uids);
}

EMFV_MAP_CALLBACK(emfv_tools_filter_subject, emfv_popup_filter_subject)
EMFV_MAP_CALLBACK(emfv_tools_filter_sender, emfv_popup_filter_sender)
EMFV_MAP_CALLBACK(emfv_tools_filter_recipient, emfv_popup_filter_recipients)
EMFV_MAP_CALLBACK(emfv_tools_filter_mlist, emfv_popup_filter_mlist)

static void
vfolder_type_got_message (CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *user_data)
{
	struct _filter_data *data = user_data;
	
	if (msg)
		vfolder_gui_add_from_message (msg, data->type, data->uri);
	
	filter_data_free (data);
}

static void
vfolder_type_uid (CamelFolder *folder, const char *uid, const char *uri, int type)
{
	struct _filter_data *data;
	
	data = g_malloc0 (sizeof (*data));
	data->type = type;
	data->uri = g_strdup (uri);
	
	mail_get_message (folder, uid, vfolder_type_got_message, data, mail_thread_new);
}

static void
vfolder_type_current (EMFolderView *emfv, int type)
{
	GPtrArray *uids;
	
	uids = message_list_get_selected (emfv->list);
	
	if (uids->len == 1)
		vfolder_type_uid (emfv->folder, (char *) uids->pdata[0], emfv->folder_uri, type);
	
	em_utils_uids_free (uids);
}

EMFV_MAP_CALLBACK(emfv_tools_vfolder_subject, emfv_popup_vfolder_subject)
EMFV_MAP_CALLBACK(emfv_tools_vfolder_sender, emfv_popup_vfolder_sender)
EMFV_MAP_CALLBACK(emfv_tools_vfolder_recipient, emfv_popup_vfolder_recipients)
EMFV_MAP_CALLBACK(emfv_tools_vfolder_mlist, emfv_popup_vfolder_mlist)

/* ********************************************************************** */

static void
emfv_view_load_images(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (emfv->preview)
		em_format_html_load_http((EMFormatHTML *)emfv->preview);
}

static BonoboUIVerb emfv_message_verbs[] = {
	BONOBO_UI_UNSAFE_VERB ("EditCut", emfv_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", emfv_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", emfv_edit_paste),

	BONOBO_UI_UNSAFE_VERB ("MailNext", emfv_mail_next),
	BONOBO_UI_UNSAFE_VERB ("MailNextFlagged", emfv_mail_next_flagged),
	BONOBO_UI_UNSAFE_VERB ("MailNextUnread", emfv_mail_next_unread),
	BONOBO_UI_UNSAFE_VERB ("MailNextThread", emfv_mail_next_thread),
	BONOBO_UI_UNSAFE_VERB ("MailPrevious", emfv_mail_previous),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousFlagged", emfv_mail_previous_flagged),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousUnread", emfv_mail_previous_unread),
	BONOBO_UI_UNSAFE_VERB ("AddSenderToAddressbook", emfv_add_sender_addressbook),
	BONOBO_UI_UNSAFE_VERB ("MessageApplyFilters", emfv_message_apply_filters),
	BONOBO_UI_UNSAFE_VERB ("MessageCopy", emfv_message_copy),
	BONOBO_UI_UNSAFE_VERB ("MessageDelete", emfv_message_delete),
	BONOBO_UI_UNSAFE_VERB ("MessageForward", emfv_message_forward),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardAttached", emfv_message_forward_attached),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardInline", emfv_message_forward_inline),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardQuoted", emfv_message_forward_quoted),
	BONOBO_UI_UNSAFE_VERB ("MessageRedirect", emfv_message_redirect),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsRead", emfv_message_mark_read),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnRead", emfv_message_mark_unread),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsImportant", emfv_message_mark_important),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnimportant", emfv_message_mark_unimportant),
	BONOBO_UI_UNSAFE_VERB ("MessageFollowUpFlag", emfv_message_followup_flag),
	BONOBO_UI_UNSAFE_VERB ("MessageMove", emfv_message_move),
	BONOBO_UI_UNSAFE_VERB ("MessageOpen", emfv_message_open),
	BONOBO_UI_UNSAFE_VERB ("MessagePostReply", emfv_message_post_reply),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyAll", emfv_message_reply_all),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyList", emfv_message_reply_list),
	BONOBO_UI_UNSAFE_VERB ("MessageReplySender", emfv_message_reply_sender),
	BONOBO_UI_UNSAFE_VERB ("MessageResend", emfv_message_resend),
	BONOBO_UI_UNSAFE_VERB ("MessageSaveAs", emfv_message_saveas),
	BONOBO_UI_UNSAFE_VERB ("MessageSearch", emfv_message_search),
	BONOBO_UI_UNSAFE_VERB ("MessageUndelete", emfv_message_undelete),
	BONOBO_UI_UNSAFE_VERB ("PrintMessage", emfv_print_message),
	BONOBO_UI_UNSAFE_VERB ("PrintPreviewMessage", emfv_print_preview_message),
	BONOBO_UI_UNSAFE_VERB ("TextZoomIn", emfv_text_zoom_in),
	BONOBO_UI_UNSAFE_VERB ("TextZoomOut", emfv_text_zoom_out),
	BONOBO_UI_UNSAFE_VERB ("TextZoomReset", emfv_text_zoom_reset),
	/* TODO: This stuff should just be 1 item that runs a wizard */
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterMailingList", emfv_tools_filter_mlist),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterRecipient", emfv_tools_filter_recipient),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterSender", emfv_tools_filter_sender),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterSubject", emfv_tools_filter_subject),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderMailingList", emfv_tools_vfolder_mlist),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderRecipient", emfv_tools_vfolder_recipient),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderSender", emfv_tools_vfolder_sender),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderSubject", emfv_tools_vfolder_subject),

	BONOBO_UI_UNSAFE_VERB ("ViewLoadImages", emfv_view_load_images),
	/* ViewHeaders stuff is a radio */
	/* CaretMode is a toggle */

	BONOBO_UI_VERB_END
};
static EPixmap emfv_message_pixmaps[] = {
	E_PIXMAP ("/commands/EditCut", "16_cut.png"),
	E_PIXMAP ("/commands/EditCopy", "16_copy.png"),
	E_PIXMAP ("/commands/EditPaste", "16_paste.png"),

	E_PIXMAP ("/commands/PrintMessage", "print.xpm"),
	E_PIXMAP ("/commands/PrintPreviewMessage", "print-preview.xpm"),
	E_PIXMAP ("/commands/MessageDelete", "evolution-trash-mini.png"),
	E_PIXMAP ("/commands/MessageUndelete", "undelete_message-16.png"),
	E_PIXMAP ("/commands/MessageCopy", "copy_16_message.xpm"),
	E_PIXMAP ("/commands/MessageMove", "move_message.xpm"),
	E_PIXMAP ("/commands/MessageReplyAll", "reply_to_all.xpm"),
	E_PIXMAP ("/commands/MessageReplySender", "reply.xpm"),
	E_PIXMAP ("/commands/MessageForward", "forward.xpm"),
	E_PIXMAP ("/commands/MessageApplyFilters", "apply-filters-16.xpm"),
	E_PIXMAP ("/commands/MessageSearch", "search-16.png"),
	E_PIXMAP ("/commands/MessageSaveAs", "save-as-16.png"),
	E_PIXMAP ("/commands/MessageMarkAsRead", "mail-read.xpm"),
	E_PIXMAP ("/commands/MessageMarkAsUnRead", "mail-new.xpm"),
	E_PIXMAP ("/commands/MessageMarkAsImportant", "priority-high.xpm"),
	E_PIXMAP ("/commands/MessageFollowUpFlag", "flag-for-followup-16.png"),
	
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageReplySender", "buttons/reply.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageReplyAll", "buttons/reply-to-all.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageForward", "buttons/forward.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/PrintMessage", "buttons/print.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageMove", "buttons/move-message.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageCopy", "buttons/copy-message.png"),
	E_PIXMAP ("/Toolbar/MailMessageToolbar/MessageDelete", "buttons/delete-message.png"),
	
	E_PIXMAP ("/Toolbar/MailNextButtons/MailNext", "buttons/next-message.png"),
	E_PIXMAP ("/Toolbar/MailNextButtons/MailPrevious", "buttons/previous-message.png"),

	E_PIXMAP_END
};

/* must match em_format_mode_t order */
static const char * const emfv_display_styles[] = {
	"/commands/ViewNormal",
	"/commands/ViewFullHeaders",
	"/commands/ViewSource"
};

static void
emfv_view_mode(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	EMFolderView *emfv = data;
	int i;

	if (type != Bonobo_UIComponent_STATE_CHANGED
	    || state[0] == '0')
		return;

	/* TODO: I don't like this stuff much, is there any way we can move listening for such events
	   elsehwere?  Probably not I guess, unless there's a EMFolderViewContainer for bonobo usage
	   of a folder view */

	printf("view mode enabled '%s'\n", path);

	for (i=0;i<= EM_FORMAT_SOURCE;i++) {
		if (strcmp(emfv_display_styles[i]+strlen("/commands/"), path) == 0) {
			em_format_set_mode((EMFormat *)emfv->preview, i);

			if (TRUE /* set preferences but not for EMMessageBrowser? */) {
				GConfClient *gconf = mail_config_get_gconf_client ();
				
				gconf_client_set_int (gconf, "/apps/evolution/mail/display/message_style", i, NULL);
			}
			break;
		}
	}
}

static void
emfv_caret_mode(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	gconf = mail_config_get_gconf_client ();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/caret_mode", state[0] != '0', NULL);
}

static void
emfv_charset_changed(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	/* menu items begin with "Charset-" = 8 characters */
	if (state[0] != '0' && strlen(path) > 8) {
		path += 8;
		/* default charset used in mail view */
		if (!strcmp(path, _("Default")))
			path = NULL;

		em_format_set_charset((EMFormat *)emfv->preview, path);
	}
}

static void
emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state)
{
	if (state) {
		GConfClient *gconf = mail_config_get_gconf_client ();
		em_format_mode_t style;
		gboolean state;
		GSList *l;

		printf("activate folder viewer %p\n", emfv);

		for (l = emfv->ui_files;l;l = l->next)
			bonobo_ui_util_set_ui(uic, PREFIX, (char *)l->data, emfv->ui_app_name, NULL);

		bonobo_ui_component_add_verb_list_with_data(uic, emfv_message_verbs, emfv);
		e_pixmaps_update(uic, emfv_message_pixmaps);

		/* FIXME: global menu's? */
		/* FIXME: toggled states, hidedeleted, view threaded */
		/* FIXME: view menu's? */

		/* FIXME: Need to implement or monitor in em-format-html-display */
		state = gconf_client_get_bool(gconf, "/apps/evolution/mail/display/caret_mode", NULL);
		bonobo_ui_component_set_prop(uic, "/commands/CaretMode", "state", state?"1":"0", NULL);
		bonobo_ui_component_add_listener(uic, "CaretMode", emfv_caret_mode, emfv);

		style = gconf_client_get_int(gconf, "/apps/evolution/mail/display/message_style", NULL);
		if (style < EM_FORMAT_NORMAL || style > EM_FORMAT_SOURCE)
			style = EM_FORMAT_NORMAL;
		bonobo_ui_component_set_prop(uic, emfv_display_styles[style], "state", "1", NULL);
		bonobo_ui_component_add_listener(uic, "ViewNormal", emfv_view_mode, emfv);
		bonobo_ui_component_add_listener(uic, "ViewFullHeaders", emfv_view_mode, emfv);
		bonobo_ui_component_add_listener(uic, "ViewSource", emfv_view_mode, emfv);
		em_format_set_mode((EMFormat *)emfv->preview, style);

		if (emfv->folder && !em_utils_folder_is_sent(emfv->folder, emfv->folder_uri))
			bonobo_ui_component_set_prop(uic, "/commands/MessageResend", "sensitive", "0", NULL);
#if 0	
	
		/* sensitivity of message-specific commands */
		prev_state = fb->selection_state;
		fb->selection_state = FB_SELSTATE_UNDEFINED;
		folder_browser_ui_set_selection_state (fb, prev_state);
#endif
		/* default charset used in mail view */
		e_charset_picker_bonobo_ui_populate (uic, "/menu/View", _("Default"), emfv_charset_changed, emfv);
	} else {
		const BonoboUIVerb *v;

		printf("de-activate folder viewer %p\n", emfv);

		/* TODO: Should this just rm /? */
		for (v = &emfv_message_verbs[0]; v->cname; v++)
			bonobo_ui_component_remove_verb(uic, v->cname);

		if (emfv->folder)
			mail_sync_folder(emfv->folder, NULL, NULL);
	}
}

int em_folder_view_print(EMFolderView *emfv, int preview)
{
	/*struct _EMFolderViewPrivate *p = emfv->priv;*/
	EMFormatHTMLPrint *print;
	GnomePrintConfig *config = NULL;
	int res;
	struct _CamelMedium *msg;

	/* FIXME: need to load the message first */
	if (emfv->preview == NULL)
		return 0;

	msg = emfv->preview->formathtml.format.message;
	if (msg == NULL)
		return 0;
	
	if (!preview) {
		GtkDialog *dialog = (GtkDialog *)gnome_print_dialog_new(NULL, _("Print Message"), GNOME_PRINT_DIALOG_COPIES);

		gtk_dialog_set_default_response(dialog, GNOME_PRINT_DIALOG_RESPONSE_PRINT);
		e_dialog_set_transient_for ((GtkWindow *) dialog, (GtkWidget *) emfv);
		
		switch (gtk_dialog_run(dialog)) {
		case GNOME_PRINT_DIALOG_RESPONSE_PRINT:
			break;
		case GNOME_PRINT_DIALOG_RESPONSE_PREVIEW:
			preview = TRUE;
			break;
		default:
			gtk_widget_destroy((GtkWidget *)dialog);
			return 0;
		}
		
		config = gnome_print_dialog_get_config((GnomePrintDialog *)dialog);
		gtk_widget_destroy((GtkWidget *)dialog);
	}

	print = em_format_html_print_new();
	res = em_format_html_print_print(print, msg, (EMFormatHTML *)emfv->preview, config, preview);
	g_object_unref(print);
	if (config)
		g_object_unref(config);

	return res;
}

struct mst_t {
	EMFolderView *emfv;
	char *uid;
};

static void
mst_free (struct mst_t *mst)
{
	mst->emfv->priv->seen_id = 0;
	
	g_free (mst->uid);
	g_free (mst);
}

static int
do_mark_seen (gpointer user_data)
{
	struct mst_t *mst = user_data;
	EMFolderView *emfv = mst->emfv;
	MessageList *list = emfv->list;
	
	if (mst->uid && list->cursor_uid && !strcmp (mst->uid, list->cursor_uid))
		camel_folder_set_message_flags (emfv->folder, mst->uid, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	
	return FALSE;
}

static void
emfv_list_done_message_selected(CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *data)
{
	EMFolderView *emfv = data;
	GConfClient *gconf;
	gboolean mark_seen;
	int timeout;
	
	em_format_format((EMFormat *) emfv->preview, (struct _CamelMedium *)msg);
	
	gconf = mail_config_get_gconf_client ();
	mark_seen = gconf_client_get_bool (gconf, "/apps/evolution/mail/display/mark_seen", NULL);
	
	if (emfv->priv->seen_id)
		gtk_timeout_remove (emfv->priv->seen_id);
	
	if (msg && mark_seen) {
		struct mst_t *mst;
		
		mst = g_new (struct mst_t, 1);
		mst->emfv = emfv;
		mst->uid = g_strdup (uid);
		
		timeout = gconf_client_get_int (gconf, "/apps/evolution/mail/display/mark_seen_timeout", NULL);
		
		if (timeout > 0) {
			emfv->priv->seen_id = gtk_timeout_add_full (timeout, do_mark_seen, NULL, mst, (GtkDestroyNotify) mst_free);
		} else {
			do_mark_seen (mst);
			mst_free (mst);
		}
	}
	
	/* FIXME: asynchronous stuff */
	/* FIXME: enable/disable menu's */
}

static void
emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv)
{
	printf("message selected '%s'\n", uid?uid:"<none>");

	/* FIXME: ui stuff based on messageinfo, if available */

	if (emfv->preview) {
		if (uid)
			mail_get_message(emfv->folder, uid, emfv_list_done_message_selected, emfv, mail_thread_new);
		else
			em_format_format((EMFormat *)emfv->preview, NULL);
	}
}

static int emfv_list_right_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv)
{
	emfv_popup(emfv, event);

	return TRUE;
}

static void
emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *emfv)
{
	if (!strncasecmp (uri, "mailto:", 7)) {
		em_utils_compose_new_message_with_mailto ((GtkWidget *) efhd, uri);
	} else if (*uri == '#') {
		gtk_html_jump_to_anchor (((EMFormatHTML *) efhd)->html, uri + 1);
	} else if (!strncasecmp (uri, "thismessage:", 12)) {
		/* ignore */
	} else if (!strncasecmp (uri, "cid:", 4)) {
		/* ignore */
	} else {
		GError *err = NULL;
		
		gnome_url_show (uri, &err);
		
		if (err) {
			g_warning ("gnome_url_show: %s", err->message);
			g_error_free (err);
		}
	}
}

static int
emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *emfv)
{
	printf("popup event, uri '%s', part '%p'\n", uri, part);

	return FALSE;
}
