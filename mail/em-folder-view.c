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

#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-view.h"
#include "em-message-browser.h"
#include "message-list.h"

#include "mail-mt.h"
#include "mail-ops.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff, sigh */

static void emfv_list_message_selected(MessageList *ml, const char *uid, EMFolderView *emfv);

static void emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *);
static int emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *);

static void emfv_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri);
static void emfv_set_folder_uri(EMFolderView *emfv, const char *uri);
static void emfv_set_message(EMFolderView *emfv, const char *uid);
static void emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state);

struct _EMFolderViewPrivate {
	int dummy;
};

static GtkVBoxClass *emfv_parent;

static void
emfv_init(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;
	struct _EMFolderViewPrivate *p;

	printf("em folder view init\n");

	gtk_box_set_homogeneous (GTK_BOX (emfv), FALSE);

	p = emfv->priv = g_malloc0(sizeof(struct _EMFolderViewPrivate));

	emfv->list = (MessageList *)message_list_new();
	g_signal_connect(emfv->list, "message_selected", G_CALLBACK(emfv_list_message_selected), emfv);

	/* setup selection?  etc? */
#if 0
	/* duh, preview is never setup yet ... */
	g_signal_connect(emfv->preview, "link_clicked", G_CALLBACK(emfv_format_link_clicked), emfv);
	g_signal_connect(emfv->preview, "popup_event", G_CALLBACK(emfv_format_popup_event), emfv);
#endif
}

static void
emfv_finalise(GObject *o)
{
	EMFolderView *emfv = (EMFolderView *)o;

	g_free(emfv->priv);

	((GObjectClass *)emfv_parent)->finalize(o);
}

static void
emfv_class_init(GObjectClass *klass)
{
	klass->finalize = emfv_finalise;

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
	int i;

	/* FIXME: handle editing message?  Should be a different method? */

	uids = message_list_get_selected(emfv->list);

	/* FIXME: 'are you sure' for > 10 messages */

	for (i=0; i<uids->len; i++) {
		EMMessageBrowser *emmb;

		emmb = (EMMessageBrowser *)em_message_browser_window_new();
		/* FIXME: session needs to be passed easier than this */
		em_format_set_session((EMFormat *)((EMFolderView *)emmb)->preview, ((EMFormat *)emfv->preview)->session);
		em_folder_view_set_folder((EMFolderView *)emmb, emfv->folder, emfv->folder_uri);
		printf("opening message '%s'\n", uids->pdata[i]);
		em_folder_view_set_message((EMFolderView *)emmb, uids->pdata[i]);
		gtk_widget_show(emmb->window);
	}

	return i;
}

/* ********************************************************************** */

static void
emfv_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri)
{
	/* FIXME: outgoing folder type? */
	message_list_set_folder(emfv->list, folder, FALSE);
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

/* FIXME: do i really need both api's? */
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
}

static void
emfv_message_apply_filters(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_copy(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_delete(BonoboUIComponent *uic, void *data, const char *path)
{
	/* FIXME: make a 'mark messages' function? */
	EMFolderView *emfv = data;

	printf("messagedelete\n");

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED);

	/* FIXME: select the next message if we just deleted 1 message */
}

static void
emfv_message_forward(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_forward_attached(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_forward_inline(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_forward_quoted(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_redirect(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_mark_read(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
}

static void
emfv_message_mark_unread(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_SEEN|CAMEL_MESSAGE_DELETED, 0);

	/* FIXME: mark-as-read timer */
}

static void
emfv_message_mark_important(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_FLAGGED|CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_FLAGGED);
}

static void
emfv_message_mark_unimportant(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_FLAGGED, 0);
}

static void
emfv_message_followup_flag(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;

	/* FIXME: code needs refactoring to use here, maybe some helper class methods */
}

static void
emfv_message_move(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_open(BonoboUIComponent *uic, void *data, const char *path)
{
	em_folder_view_open_selected((EMFolderView *)data);
}

static void
emfv_message_post_reply(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_reply_all(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_reply_list(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_reply_sender(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_resend(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_message_saveas(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;

	/* FIXME: needs code refactor */
}

static void
emfv_message_search(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;

	/* FIXME: new search code in formathtmldisplay ? */
}

static void
emfv_message_undelete(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_mark_selected(emfv, CAMEL_MESSAGE_DELETED, 0);
}

static void
emfv_print_message(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	em_folder_view_print(emfv, FALSE);
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

static void
emfv_tools_filter_mlist(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_filter_recipient(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_filter_sender(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_filter_subject(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_vfolder_mlist(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_vfolder_recipient(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_vfolder_sender(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_tools_vfolder_subject(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
}

static void
emfv_view_load_images(BonoboUIComponent *uic, void *data, const char *path)
{
	EMFolderView *emfv = data;

	if (emfv->preview)
		em_format_html_load_http((EMFormatHTML *)emfv->preview);
}

static BonoboUIVerb emfv_message_verbs[] = {
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

static void
emfv_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state)
{
	if (state) {
		printf("activate folder viewer %p\n", emfv);

		bonobo_ui_component_add_verb_list_with_data(uic, emfv_message_verbs, emfv);
		bonobo_ui_util_set_ui(uic, PREFIX, EVOLUTION_UIDIR "/evolution-mail-message.xml", "evolution-mail", NULL);
		e_pixmaps_update(uic, emfv_message_pixmaps);

		/* FIXME: global menu's? */
		/* FIXME: toggled states, hidedeleted, view threaded */
		/* FIXME: view menu's? */
	} else {
		const BonoboUIVerb *v;

		printf("de-activate folder viewer %p\n", emfv);

		/* TODO: Should this just rm /? */
		for (v = &emfv_message_verbs[0]; v->cname; v++)
			bonobo_ui_component_remove_verb(uic, v->cname);
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
		gtk_window_set_transient_for((GtkWindow *)dialog, (GtkWindow *)gtk_widget_get_toplevel((GtkWidget *)emfv));
		
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

	/* FIXME: We need to know when the formatting is completely finished ... damn! */

	em_format_format_clone((EMFormat *)print, msg, (EMFormat *)emfv->preview);
	res = em_format_html_print_print(print, config, preview);
	g_object_unref(print);

	return res;
}

static void
emfv_list_done_message_selected(CamelFolder *folder, const char *uid, CamelMimeMessage *msg, void *data)
{
	EMFolderView *emfv = data;

	/* FIXME: mark_seen timeout */
	/* FIXME: asynchronous stuff */
	/* FIXME: enable/disable menu's */

	em_format_format((EMFormat *) emfv->preview, (struct _CamelMedium *)msg);
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

static void
emfv_format_link_clicked(EMFormatHTMLDisplay *efhd, const char *uri, EMFolderView *emfv)
{
	printf("Link clicked: %s\n", uri);
}

static int
emfv_format_popup_event(EMFormatHTMLDisplay *efhd, GdkEventButton *event, const char *uri, CamelMimePart *part, EMFolderView *emfv)
{
	printf("popup event, uri '%s', part '%p'\n", uri, part);

	return FALSE;
}
