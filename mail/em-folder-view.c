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
#include <gtk/gtkbutton.h>
#include <gtk/gtkvpaned.h>
#include <gdk/gdkkeysyms.h>

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

static void emfv_folder_changed(CamelFolder *folder, CamelFolderChangeInfo *changes, EMFolderView *emfv);

static void emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv);
static int emfv_list_right_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv);
static void emfv_list_double_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv);
static int emfv_list_key_press(ETree *tree, int row, ETreePath path, int col, GdkEvent *ev, EMFolderView *emfv);

static void emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *);
static int emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *);

static void emfv_enable_menus(EMFolderView *emfv);

static void emfv_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri);
static void emfv_set_folder_uri(EMFolderView *emfv, const char *uri);
static void emfv_set_message(EMFolderView *emfv, const char *uid);
static void emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state);

static void emfv_message_reply(EMFolderView *emfv, int mode);
static void vfolder_type_current (EMFolderView *emfv, int type);
static void filter_type_current (EMFolderView *emfv, int type);

static void emfv_setting_setup(EMFolderView *emfv);

static const EMFolderViewEnable emfv_enable_map[];

struct _EMFolderViewPrivate {
	guint seen_id;
	guint setting_notify_id;
	
	char *loaded_uid;
	char *loading_uid;
	
	CamelObjectHookID folder_changed_id;

	GtkWidget *invisible;
	char *selection_uri;
};

static GtkVBoxClass *emfv_parent;

static void emfv_selection_get(GtkWidget *widget, GtkSelectionData *data, guint info, guint time_stamp, EMFolderView *emfv);
static void emfv_selection_clear_event(GtkWidget *widget, GdkEventSelection *event, EMFolderView *emfv);

static void
emfv_init(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;
	struct _EMFolderViewPrivate *p;

	gtk_box_set_homogeneous (GTK_BOX (emfv), FALSE);

	p = emfv->priv = g_malloc0(sizeof(struct _EMFolderViewPrivate));

	emfv->ui_files = g_slist_append(NULL, EVOLUTION_UIDIR "/evolution-mail-message.xml");
	emfv->ui_app_name = "evolution-mail";

	emfv->enable_map = g_slist_prepend(NULL, (void *)emfv_enable_map);

	emfv->list = (MessageList *)message_list_new();
	g_signal_connect(emfv->list, "message_selected", G_CALLBACK(emfv_list_message_selected), emfv);

	/* FIXME: should this hang off message-list instead? */
	g_signal_connect(emfv->list->tree, "right_click", G_CALLBACK(emfv_list_right_click), emfv);
	g_signal_connect(emfv->list->tree, "double_click", G_CALLBACK(emfv_list_double_click), emfv);
	g_signal_connect(emfv->list->tree, "key_press", G_CALLBACK(emfv_list_key_press), emfv);

	emfv->preview = (EMFormatHTMLDisplay *)em_format_html_display_new();
	g_signal_connect(emfv->preview, "link_clicked", G_CALLBACK(emfv_format_link_clicked), emfv);
	g_signal_connect(emfv->preview, "popup_event", G_CALLBACK(emfv_format_popup_event), emfv);

	p->invisible = gtk_invisible_new();
	g_object_ref(p->invisible);
	gtk_object_sink((GtkObject *)p->invisible);
	g_signal_connect(p->invisible, "selection_get", G_CALLBACK(emfv_selection_get), emfv);
	g_signal_connect(p->invisible, "selection_clear_event", G_CALLBACK(emfv_selection_clear_event), emfv);
	gtk_selection_add_target(p->invisible, GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 0);
	gtk_selection_add_target(p->invisible, GDK_SELECTION_PRIMARY, GDK_SELECTION_TYPE_STRING, 1);

	emfv->async = mail_async_event_new();

	emfv_setting_setup(emfv);
}

static void
emfv_finalise(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;
	struct _EMFolderViewPrivate *p = emfv->priv;

	if (emfv->async)
		mail_async_event_destroy(emfv->async);

	if (emfv->folder) {
		if (p->folder_changed_id)
			camel_object_remove_event(emfv->folder, p->folder_changed_id);
		camel_object_unref(emfv->folder);
		g_free(emfv->folder_uri);
	}

	g_slist_free(emfv->ui_files);
	g_slist_free(emfv->enable_map);
	
	g_free (p->loaded_uid);
	g_free (p->loading_uid);
	
	g_free(p);

	((GObjectClass *)emfv_parent)->finalize(o);
}

static void
emfv_destroy (GtkObject *o)
{
	EMFolderView *emfv = (EMFolderView *) o;
	struct _EMFolderViewPrivate *p = emfv->priv;

	if (p->seen_id) {
		g_source_remove(p->seen_id);
		p->seen_id = 0;
	}

	if (p->setting_notify_id) {
		GConfClient *gconf = gconf_client_get_default();

		gconf_client_notify_remove(gconf, p->setting_notify_id);
		p->setting_notify_id = 0;
		g_object_unref(gconf);
	}

	if (p->invisible) {
		g_object_unref(p->invisible);
		p->invisible = NULL;
	}

	emfv->preview = NULL;
	emfv->list = NULL;
	emfv->preview_active = FALSE;
	emfv->uic = NULL;

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
	if (folder != emfv->folder) {
		if (emfv->folder) {
			if (emfv->priv->folder_changed_id)
				camel_object_remove_event(emfv->folder, emfv->priv->folder_changed_id);
			camel_object_unref(emfv->folder);
		}
		emfv->folder = folder;
		if (folder) {
			emfv->priv->folder_changed_id = camel_object_hook_event(folder, "folder_changed",
										(CamelObjectEventHookFunc)emfv_folder_changed, emfv);
			camel_object_ref(folder);
		}
	}

	emfv_enable_menus(emfv);
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

static void
emfv_selection_get(GtkWidget *widget, GtkSelectionData *data, guint info, guint time_stamp, EMFolderView *emfv)
{
	struct _EMFolderViewPrivate *p = emfv->priv;

	if (p->selection_uri == NULL)
		return;

	gtk_selection_data_set(data, data->target, 8, p->selection_uri, strlen(p->selection_uri));
}

static void
emfv_selection_clear_event(GtkWidget *widget, GdkEventSelection *event, EMFolderView *emfv)
{
#if 0 /* do i care? */
	struct _EMFolderViewPrivate *p = emfv->priv;

	g_free(p->selection_uri);
	p->selection_uri = NULL;
#endif
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
	printf("UNIMPLEMENTED: add sender to addressbook\n");
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

/* TODO: Move some of these to be 'standard' menu's */

static EMPopupItem emfv_popup_menu[] = {
	{ EM_POPUP_ITEM, "00.emfv.00", N_("_Open"), G_CALLBACK(emfv_popup_open), NULL, NULL, 0 },
	{ EM_POPUP_ITEM, "00.emfv.01", N_("_Edit as New Message..."), G_CALLBACK(emfv_popup_resend), NULL, NULL, EM_POPUP_SELECT_RESEND },
	{ EM_POPUP_ITEM, "00.emfv.02", N_("_Save As..."), G_CALLBACK(emfv_popup_saveas), NULL, "save-as-16.png", 0 },
	{ EM_POPUP_ITEM, "00.emfv.03", N_("_Print"), G_CALLBACK(emfv_popup_print), NULL, "print.xpm", 0 },

	{ EM_POPUP_BAR, "10.emfv" },
	{ EM_POPUP_ITEM, "10.emfv.00", N_("_Reply to Sender"), G_CALLBACK(emfv_popup_reply_sender), NULL, "reply.xpm", EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "10.emfv.01", N_("Reply to _List"), G_CALLBACK(emfv_popup_reply_list), NULL, NULL, EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_MAILING_LIST },
	{ EM_POPUP_ITEM, "10.emfv.02", N_("Reply to _All"), G_CALLBACK(emfv_popup_reply_all), NULL, "reply_to_all.xpm", EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "10.emfv.03", N_("_Forward"), G_CALLBACK(emfv_popup_forward), NULL, "forward.xpm", EM_POPUP_SELECT_MANY },

	{ EM_POPUP_BAR, "20.emfv", NULL, NULL, NULL, NULL, EM_POPUP_SELECT_FLAG_FOLLOWUP|EM_POPUP_SELECT_FLAG_COMPLETED|EM_POPUP_SELECT_FLAG_CLEAR },
	{ EM_POPUP_ITEM, "20.emfv.00", N_("Follo_w Up..."), G_CALLBACK(emfv_popup_flag_followup), NULL, "flag-for-followup-16.png",  EM_POPUP_SELECT_FLAG_FOLLOWUP },
	{ EM_POPUP_ITEM, "20.emfv.01", N_("Fla_g Completed"), G_CALLBACK(emfv_popup_flag_completed), NULL, NULL, EM_POPUP_SELECT_FLAG_COMPLETED },
	{ EM_POPUP_ITEM, "20.emfv.02", N_("Cl_ear Flag"), G_CALLBACK(emfv_popup_flag_clear), NULL, NULL, EM_POPUP_SELECT_FLAG_CLEAR },
	
	{ EM_POPUP_BAR, "30.emfv" },
	{ EM_POPUP_ITEM, "30.emfv.00", N_("Mar_k as Read"), G_CALLBACK(emfv_popup_mark_read), NULL, "mail-read.xpm", EM_POPUP_SELECT_MARK_READ },
	{ EM_POPUP_ITEM,  "30.emfv.01", N_("Mark as _Unread"), G_CALLBACK(emfv_popup_mark_unread), NULL, "mail-new.xpm", EM_POPUP_SELECT_MARK_UNREAD },
	{ EM_POPUP_ITEM, "30.emfv.02", N_("Mark as _Important"), G_CALLBACK(emfv_popup_mark_important), NULL, "priority-high.xpm", EM_POPUP_SELECT_MARK_IMPORTANT },
	{ EM_POPUP_ITEM, "30.emfv.03", N_("_Mark as Unimportant"), G_CALLBACK(emfv_popup_mark_unimportant), NULL, NULL, EM_POPUP_SELECT_MARK_UNIMPORTANT },
	
	{ EM_POPUP_BAR, "40.emfv" },
	{ EM_POPUP_ITEM, "40.emfv.00", N_("_Delete"), G_CALLBACK(emfv_popup_delete), NULL, "evolution-trash-mini.png", EM_POPUP_SELECT_DELETE },
	{ EM_POPUP_ITEM, "40.emfv.01", N_("U_ndelete"), G_CALLBACK(emfv_popup_undelete), NULL, "undelete_message-16.png", EM_POPUP_SELECT_UNDELETE },

	{ EM_POPUP_BAR, "50.emfv" },
	{ EM_POPUP_ITEM, "50.emfv.00", N_("Mo_ve to Folder..."), G_CALLBACK(emfv_popup_move) },
	{ EM_POPUP_ITEM, "50.emfv.01", N_("_Copy to Folder..."), G_CALLBACK(emfv_popup_copy) },

	{ EM_POPUP_BAR, "60.label" },
	{ EM_POPUP_SUBMENU, "60.label.00", N_("Label") },
	{ EM_POPUP_IMAGE, "60.label.00/00.label", N_("None"), G_CALLBACK(emfv_popup_label_clear) },
	{ EM_POPUP_BAR, "60.label.00/00.label.00" },

	{ EM_POPUP_BAR, "70.emfv", NULL, NULL, NULL, NULL, EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_ADD_SENDER },	
	{ EM_POPUP_ITEM, "70.emfv.00", N_("Add Sender to Address_book"), G_CALLBACK(emfv_popup_add_sender), NULL, NULL, EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_ADD_SENDER },

	{ EM_POPUP_BAR, "80.emfv" },	
	{ EM_POPUP_ITEM, "80.emfv.00", N_("Appl_y Filters"), G_CALLBACK(emfv_popup_apply_filters) },

	{ EM_POPUP_BAR, "90.filter" },
	{ EM_POPUP_SUBMENU, "90.filter.00", N_("Crea_te Rule From Message"), NULL, NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.00", N_("VFolder on _Subject"), G_CALLBACK(emfv_popup_vfolder_subject), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.01", N_("VFolder on Se_nder"), G_CALLBACK(emfv_popup_vfolder_sender), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.02", N_("VFolder on _Recipients"), G_CALLBACK(emfv_popup_vfolder_recipients), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/00.03", N_("VFolder on Mailing _List"),
	  G_CALLBACK(emfv_popup_vfolder_mlist), NULL, NULL, EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_MAILING_LIST },

	{ EM_POPUP_BAR, "90.filter.00/10" },
	{ EM_POPUP_ITEM, "90.filter.00/10.00", N_("Filter on Sub_ject"), G_CALLBACK(emfv_popup_filter_subject), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.01", N_("Filter on Sen_der"), G_CALLBACK(emfv_popup_filter_sender), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.02", N_("Filter on Re_cipients"), G_CALLBACK(emfv_popup_filter_recipients),  NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "90.filter.00/10.03", N_("Filter on _Mailing List"),
	  G_CALLBACK(emfv_popup_filter_mlist), NULL, NULL, EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_MAILING_LIST },
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
	GtkMenu *menu;
	EMPopup *emp;
	EMPopupTarget *target;
	int i;

	emp = em_popup_new("com.ximian.mail.folderview.popup.select");
	target = em_folder_view_get_popup_target(emfv);

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

	menu = em_popup_create_menu_once(emp, target, target->mask, target->mask);

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
	else if (emfv->preview_active)
		em_format_html_display_cut(emfv->preview);
}

static void
emfv_edit_copy(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (message_list_has_primary_selection(emfv->list))
		message_list_copy(emfv->list, FALSE);
	else if (emfv->preview_active)
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
		struct _camel_header_raw *header;
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

/* this is added to emfv->enable_map in :init() */
static const EMFolderViewEnable emfv_enable_map[] = {
	{ "EditCut",                  EM_POPUP_SELECT_MANY },
	{ "EditCopy",                 EM_POPUP_SELECT_MANY },
	{ "EditPaste",                0 },

	/* FIXME: should these be single-selection? */
	{ "MailNext",                 EM_POPUP_SELECT_MANY },
	{ "MailNextFlagged",          EM_POPUP_SELECT_MANY },
	{ "MailNextUnread",           EM_POPUP_SELECT_MANY },
	{ "MailNextThread",           EM_POPUP_SELECT_MANY },
	{ "MailPrevious",             EM_POPUP_SELECT_MANY },
	{ "MailPreviousFlagged",      EM_POPUP_SELECT_MANY },
	{ "MailPreviousUnread",       EM_POPUP_SELECT_MANY },

	{ "AddSenderToAddressbook",   EM_POPUP_SELECT_ADD_SENDER },

	{ "MessageApplyFilters",      EM_POPUP_SELECT_MANY },
	{ "MessageCopy",              EM_POPUP_SELECT_MANY },
	{ "MessageDelete",            EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_DELETE },
	{ "MessageForward",           EM_POPUP_SELECT_MANY },
	{ "MessageForwardAttached",   EM_POPUP_SELECT_MANY },
	{ "MessageForwardInline",     EM_POPUP_SELECT_ONE },
	{ "MessageForwardQuoted",     EM_POPUP_SELECT_ONE },
	{ "MessageRedirect",          EM_POPUP_SELECT_ONE },
	{ "MessageMarkAsRead",        EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_MARK_READ },
	{ "MessageMarkAsUnRead",      EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_MARK_UNREAD },
	{ "MessageMarkAsImportant",   EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_MARK_IMPORTANT },
	{ "MessageMarkAsUnimportant", EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_MARK_UNIMPORTANT },
	{ "MessageFollowUpFlag",      EM_POPUP_SELECT_MANY },
	{ "MessageMove",              EM_POPUP_SELECT_MANY },
	{ "MessageOpen",              EM_POPUP_SELECT_MANY },
	{ "MessagePostReply",         EM_POPUP_SELECT_ONE },
	{ "MessageReplyAll",          EM_POPUP_SELECT_ONE },
	{ "MessageReplyList",         EM_POPUP_SELECT_ONE|EM_POPUP_SELECT_MAILING_LIST },
	{ "MessageReplySender",       EM_POPUP_SELECT_ONE },
	{ "MessageResend",            EM_POPUP_SELECT_RESEND },
	{ "MessageSaveAs",            EM_POPUP_SELECT_MANY },
	{ "MessageSearch",            EM_POPUP_SELECT_ONE },
	{ "MessageUndelete",          EM_POPUP_SELECT_MANY|EM_POPUP_SELECT_UNDELETE },
	{ "PrintMessage",             EM_POPUP_SELECT_ONE },
	{ "PrintPreviewMessage",      EM_POPUP_SELECT_ONE },

	{ "TextZoomIn",		      EM_POPUP_SELECT_ONE },
	{ "TextZoomOut",	      EM_POPUP_SELECT_ONE },
	{ "TextZoomReset",	      EM_POPUP_SELECT_ONE },

	{ "ToolsFilterMailingList",   EM_POPUP_SELECT_ONE },
	{ "ToolsFilterRecipient",     EM_POPUP_SELECT_ONE },
	{ "ToolsFilterSender",        EM_POPUP_SELECT_ONE },
	{ "ToolsFilterSubject",       EM_POPUP_SELECT_ONE },	
	{ "ToolsVFolderMailingList",  EM_POPUP_SELECT_ONE },
	{ "ToolsVFolderRecipient",    EM_POPUP_SELECT_ONE },
	{ "ToolsVFolderSender",       EM_POPUP_SELECT_ONE },
	{ "ToolsVFolderSubject",      EM_POPUP_SELECT_ONE },

	{ "ViewLoadImages",	      EM_POPUP_SELECT_ONE },

	{ NULL },

	/* always enabled

	{ "ViewFullHeaders", IS_0MESSAGE, 0 },
	{ "ViewNormal",      IS_0MESSAGE, 0 },
	{ "ViewSource",      IS_0MESSAGE, 0 },
	{ "CaretMode",       IS_0MESSAGE, 0 }, */
};

static void
emfv_enable_menus(EMFolderView *emfv)
{
	guint32 disable_mask;
	GString *name;
	GSList *l;
	EMPopupTarget *t;

	if (emfv->uic == NULL)
		return;

	if (emfv->folder) {
		t = em_folder_view_get_popup_target(emfv);
		disable_mask = t->mask;
		em_popup_target_free(t);
	} else {
		disable_mask = ~0;
	}

	name = g_string_new("");
	for (l = emfv->enable_map; l; l = l->next) {
		EMFolderViewEnable *map = l->data;
		int i;

		for (i=0;map[i].name;i++) {
			int state = (map[i].mask & disable_mask) == 0;

			g_string_printf(name, "/commands/%s", map[i].name);
			bonobo_ui_component_set_prop(emfv->uic, name->str, "sensitive", state?"1":"0", NULL);
		}
	}

	g_string_free(name, TRUE);
}

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
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	em_format_html_display_set_caret_mode(emfv->preview, state[0] != '0');

	gconf_client_set_bool(mail_config_get_gconf_client(), "/apps/evolution/mail/display/caret_mode", state[0] != '0', NULL);
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
emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int act)
{
	if (act) {
		em_format_mode_t style;
		gboolean state;
		GSList *l;

		emfv->uic = uic;

		for (l = emfv->ui_files;l;l = l->next)
			bonobo_ui_util_set_ui(uic, PREFIX, (char *)l->data, emfv->ui_app_name, NULL);

		bonobo_ui_component_add_verb_list_with_data(uic, emfv_message_verbs, emfv);
		e_pixmaps_update(uic, emfv_message_pixmaps);

		state = emfv->preview->caret_mode;
		bonobo_ui_component_set_prop(uic, "/commands/CaretMode", "state", state?"1":"0", NULL);
		bonobo_ui_component_add_listener(uic, "CaretMode", emfv_caret_mode, emfv);

		style = ((EMFormat *)emfv->preview)->mode;
		bonobo_ui_component_set_prop(uic, emfv_display_styles[style], "state", "1", NULL);
		bonobo_ui_component_add_listener(uic, "ViewNormal", emfv_view_mode, emfv);
		bonobo_ui_component_add_listener(uic, "ViewFullHeaders", emfv_view_mode, emfv);
		bonobo_ui_component_add_listener(uic, "ViewSource", emfv_view_mode, emfv);
		em_format_set_mode((EMFormat *)emfv->preview, style);

		if (emfv->folder && !em_utils_folder_is_sent(emfv->folder, emfv->folder_uri))
			bonobo_ui_component_set_prop(uic, "/commands/MessageResend", "sensitive", "0", NULL);

		/* default charset used in mail view */
		e_charset_picker_bonobo_ui_populate (uic, "/menu/View", _("Default"), emfv_charset_changed, emfv);

		emfv_enable_menus(emfv);
	} else {
		const BonoboUIVerb *v;

		/* TODO: Should this just rm /? */
		for (v = &emfv_message_verbs[0]; v->cname; v++)
			bonobo_ui_component_remove_verb(uic, v->cname);

		if (emfv->folder)
			mail_sync_folder(emfv->folder, NULL, NULL);

		emfv->uic = NULL;
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
	if (!emfv->preview_active)
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

EMPopupTarget *
em_folder_view_get_popup_target(EMFolderView *emfv)
{
	EMPopupTarget *t;

	t = em_popup_target_new_select(emfv->folder, emfv->folder_uri, message_list_get_selected(emfv->list));
	t->widget = (GtkWidget *)emfv;

	if (emfv->list->threaded)
		t->mask &= ~EM_FOLDER_VIEW_SELECT_THREADED;

	if (message_list_hidden(emfv->list) != 0)
		t->mask &= ~EM_FOLDER_VIEW_SELECT_HIDDEN;

	return t;
}

/* ********************************************************************** */

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
	
	g_free (emfv->priv->loaded_uid);
	if (emfv->priv->loading_uid && !strcmp (emfv->priv->loading_uid, uid)) {
		emfv->priv->loaded_uid = emfv->priv->loading_uid;
		emfv->priv->loading_uid = NULL;
	} else {
		emfv->priv->loaded_uid = g_strdup (uid);
	}
	
	em_format_format((EMFormat *) emfv->preview, (struct _CamelMedium *)msg);
	
	if (emfv->priv->seen_id)
		g_source_remove(emfv->priv->seen_id);
	
	if (msg && emfv->mark_seen) {
		if (emfv->mark_seen_timeout > 0) {
			struct mst_t *mst;
			
			mst = g_new (struct mst_t, 1);
			mst->emfv = emfv;
			mst->uid = g_strdup (uid);
			
			emfv->priv->seen_id = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, emfv->mark_seen_timeout,
								 (GSourceFunc)do_mark_seen, mst, (GDestroyNotify)mst_free);
		} else {
			camel_folder_set_message_flags(emfv->folder, uid, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
		}
	}
}

static void
emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv)
{
	/* FIXME: ui stuff based on messageinfo, if available */
	if (emfv->preview_active) {
		if (uid != NULL) {
			if (emfv->priv->loading_uid != NULL) {
				if (!strcmp (emfv->priv->loading_uid, uid))
					return;
			} else if (emfv->priv->loaded_uid != NULL) {
				if (!strcmp (emfv->priv->loaded_uid, uid))
					return;
			}
			
			g_free (emfv->priv->loading_uid);
			emfv->priv->loading_uid = g_strdup (uid);
			
			mail_get_message(emfv->folder, uid, emfv_list_done_message_selected, emfv, mail_thread_new);
		} else {
			g_free (emfv->priv->loaded_uid);
			emfv->priv->loaded_uid = NULL;
			
			g_free (emfv->priv->loading_uid);
			emfv->priv->loading_uid = NULL;
			
			em_format_format((EMFormat *)emfv->preview, NULL);
		}
	} else {
		g_free (emfv->priv->loaded_uid);
		emfv->priv->loaded_uid = NULL;
		
		g_free (emfv->priv->loading_uid);
		emfv->priv->loading_uid = NULL;
	}

	emfv_enable_menus(emfv);
}

static void
emfv_list_double_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv)
{
	/* Ignore double-clicks on columns that handle thier own state */
	if (MESSAGE_LIST_COLUMN_IS_ACTIVE (col))
		return;

	em_folder_view_open_selected(emfv);
}

static int
emfv_list_right_click(ETree *tree, gint row, ETreePath path, gint col, GdkEvent *event, EMFolderView *emfv)
{
	emfv_popup(emfv, event);

	return TRUE;
}

static int
emfv_list_key_press(ETree *tree, int row, ETreePath path, int col, GdkEvent *ev, EMFolderView *emfv)
{
	GPtrArray *uids;
	int i;
	guint32 flags;

	if ((ev->key.state & GDK_CONTROL_MASK) != 0)
		return FALSE;

	switch (ev->key.keyval) {
	case GDK_Return:
	case GDK_KP_Enter:
	case GDK_ISO_Enter:
		em_folder_view_open_selected(emfv);
		break;
	case GDK_Delete:
	case GDK_KP_Delete:
		/* If any messages are undeleted, run delete, if all are deleted, run undelete */
		flags = 0;
		uids = message_list_get_selected(emfv->list);
		for (i = 0; i < uids->len; i++) {
			if ((camel_folder_get_message_flags(emfv->folder, uids->pdata[i]) & CAMEL_MESSAGE_DELETED) == 0)
				break;
		}
		message_list_free_uids(emfv->list, uids);
		if (i == uids->len)
			emfv_popup_undelete(NULL, emfv);
		else
			emfv_popup_delete(NULL, emfv);
		break;
	case GDK_Menu:
		/* FIXME: location of popup */
		emfv_popup(emfv, NULL);
		break;
	case '!':
		uids = message_list_get_selected(emfv->list);

		camel_folder_freeze(emfv->folder);
		for (i = 0; i < uids->len; i++) {
			flags = camel_folder_get_message_flags(emfv->folder, uids->pdata[i]) ^ CAMEL_MESSAGE_FLAGGED;
			if (flags & CAMEL_MESSAGE_FLAGGED)
				flags &= ~CAMEL_MESSAGE_DELETED;
			camel_folder_set_message_flags(emfv->folder, uids->pdata[i],
						       CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_DELETED, flags);
		}
		camel_folder_thaw(emfv->folder);

		message_list_free_uids(emfv->list, uids);
		break;
	default:
		return FALSE;
	}
	
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

struct _EMFVPopupItem {
	EMPopupItem item;

	EMFolderView *emfv;
	char *uri;
};

static void
emp_uri_popup_link_copy(GtkWidget *w, struct _EMFVPopupItem *item)
{
	struct _EMFolderViewPrivate *p = item->emfv->priv;

	g_free(p->selection_uri);
	p->selection_uri = g_strdup(item->uri);

	gtk_selection_owner_set(p->invisible, GDK_SELECTION_PRIMARY, gtk_get_current_event_time());
	gtk_selection_owner_set(p->invisible, GDK_SELECTION_CLIPBOARD, gtk_get_current_event_time());
}

static struct _EMFVPopupItem emfv_uri_popups[] = {
	{ { EM_POPUP_ITEM, "00.uri.01", N_("_Copy Link Location"), G_CALLBACK(emp_uri_popup_link_copy), NULL, NULL, EM_POPUP_URI_NOT_MAILTO }, },
};

static void
emfv_uri_popup_free(GSList *list)
{
	while (list) {
		GSList *n = list->next;
		struct _EMFVPopupItem *item = list->data;

		g_free(item->uri);
		g_object_unref(item->emfv);
		g_slist_free_1(list);

		list = n;
	}
}

static int
emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *emfv)
{
	EMPopup *emp;
	EMPopupTarget *target;
	GtkMenu *menu;

	/* FIXME: this maybe should just fit on em-html-display, it has access to the
	   snooped part type */

	emp = em_popup_new("com.ximian.mail.folderview.popup.uri");
	if (part)
		target = em_popup_target_new_part(part, NULL);
	else {
		GSList *menus = NULL;
		int i;

		target = em_popup_target_new_uri(uri);

		for (i=0;i<sizeof(emfv_uri_popups)/sizeof(emfv_uri_popups[0]);i++) {
			emfv_uri_popups[i].item.activate_data = &emfv_uri_popups[i];
			emfv_uri_popups[i].emfv = emfv;
			g_object_ref(emfv);
			emfv_uri_popups[i].uri = g_strdup(target->data.uri);
			menus = g_slist_prepend(menus, &emfv_uri_popups[i]);
		}
		em_popup_add_items(emp, menus, (GDestroyNotify)emfv_uri_popup_free);
	}

	menu = em_popup_create_menu_once(emp, target, target->mask, target->mask);
	gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button, event->time);

	return TRUE;
}

static void
emfv_gui_folder_changed(CamelFolder *folder, void *dummy, EMFolderView *emfv)
{
	emfv_enable_menus(emfv);
	g_object_unref(emfv);
}

static void
emfv_folder_changed(CamelFolder *folder, CamelFolderChangeInfo *changes, EMFolderView *emfv)
{
	g_object_ref(emfv);
	mail_async_event_emit(emfv->async, MAIL_ASYNC_GUI, (MailAsyncFunc)emfv_gui_folder_changed, folder, NULL, emfv);
}

/* keep these two tables in sync */
enum {
	EMFV_ANIMATE_IMAGES = 1,
	EMFV_CITATION_COLOUR,
	EMFV_CITATION_MARK,
	EMFV_CARET_MODE,
	EMFV_MESSAGE_STYLE,
	EMFV_MARK_SEEN,
	EMFV_MARK_SEEN_TIMEOUT,
	EMFV_LOAD_HTTP,
	EMFV_SETTINGS		/* last, for loop count */
};

/* IF these get too long, update key field */
static const char * const emfv_display_keys[] = {
	"animate_images",
	"citation_colour",
	"mark_citations",
	"caret_mode",
	"message_style",
	"mark_seen",
	"mark_seen_timeout",
	"load_http_images"
};

static GHashTable *emfv_setting_key;

static void
emfv_setting_notify(GConfClient *gconf, guint cnxn_id, GConfEntry *entry, EMFolderView *emfv)
{
	char *tkey;

	g_return_if_fail (gconf_entry_get_key (entry) != NULL);
	g_return_if_fail (gconf_entry_get_value (entry) != NULL);
	
	tkey = strrchr(entry->key, '/');
	g_return_if_fail (tkey != NULL);

	switch(GPOINTER_TO_INT(g_hash_table_lookup(emfv_setting_key, tkey+1))) {
	case EMFV_ANIMATE_IMAGES:
		em_format_html_display_set_animate(emfv->preview, gconf_value_get_bool(gconf_entry_get_value(entry)));
		break;
	case EMFV_CITATION_COLOUR: {
		const char *s;
		GdkColor colour;
		guint32 rgb;

		s = gconf_value_get_string(gconf_entry_get_value(entry));
		gdk_color_parse(s?s:"#737373", &colour);
		rgb = ((colour.red & 0xff00) << 8) | (colour.green & 0xff00) | ((colour.blue & 0xff00) >> 8);
		em_format_html_set_mark_citations((EMFormatHTML *)emfv->preview,
						  ((EMFormatHTML *)emfv->preview)->mark_citations, rgb);
		break; }
	case EMFV_CITATION_MARK:
		em_format_html_set_mark_citations((EMFormatHTML *)emfv->preview,
						  gconf_value_get_bool(gconf_entry_get_value(entry)),
						  ((EMFormatHTML *)emfv->preview)->citation_colour);
		break;
	case EMFV_CARET_MODE:
		em_format_html_display_set_caret_mode(emfv->preview, gconf_value_get_bool(gconf_entry_get_value(entry)));
		break;
	case EMFV_MESSAGE_STYLE: {
		int style = gconf_value_get_int(gconf_entry_get_value(entry));
		
		if (style < EM_FORMAT_NORMAL || style > EM_FORMAT_SOURCE)
			style = EM_FORMAT_NORMAL;
		em_format_set_mode((EMFormat *)emfv->preview, style);
		break; }
	case EMFV_MARK_SEEN:
		emfv->mark_seen = gconf_value_get_bool(gconf_entry_get_value(entry));
		break;
	case EMFV_MARK_SEEN_TIMEOUT:
		emfv->mark_seen_timeout = gconf_value_get_int(gconf_entry_get_value(entry));
		break;
	case EMFV_LOAD_HTTP: {
		int style = gconf_value_get_int(gconf_entry_get_value(entry));

		/* FIXME: this doesn't handle the 'sometimes' case, only the always case */
		em_format_html_set_load_http((EMFormatHTML *)emfv->preview, style == 2);
		break; }
	}
}

static void
emfv_setting_setup(EMFolderView *emfv)
{
	GConfClient *gconf = gconf_client_get_default();
	GConfEntry *entry;
	GError *err = NULL;
	int i;
	char key[64];

	if (emfv_setting_key == NULL) {
		emfv_setting_key = g_hash_table_new(g_str_hash, g_str_equal);
		for (i=1;i<EMFV_SETTINGS;i++)
			g_hash_table_insert(emfv_setting_key, (void *)emfv_display_keys[i-1], GINT_TO_POINTER(i));
	}

	gconf_client_add_dir(gconf, "/apps/evolution/mail/display", GCONF_CLIENT_PRELOAD_NONE, NULL);

	for (i=1;err == NULL && i<EMFV_SETTINGS;i++) {
		sprintf(key, "/apps/evolution/mail/display/%s", emfv_display_keys[i-1]);
		entry = gconf_client_get_entry(gconf, key, NULL, TRUE, &err);
		if (entry) {
			emfv_setting_notify(gconf, 0, entry, emfv);
			gconf_entry_free(entry);
		}
	}

	if (err) {
		g_warning("Could not load display settings: %s", err->message);
		g_error_free(err);
	}

	emfv->priv->setting_notify_id = gconf_client_notify_add(gconf, "/apps/evolution/mail/display",
								(GConfClientNotifyFunc)emfv_setting_notify,
								emfv, NULL, NULL);
	g_object_unref(gconf);
}
