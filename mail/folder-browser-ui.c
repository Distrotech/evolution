/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Peter Williams <peterw@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
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

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h> /* gnome_util_prepend_user_home */

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>

#include "widgets/misc/e-charset-picker.h"
#include "widgets/menus/gal-view-menus.h" /* GalView stuff */
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>

#include "mail-callbacks.h" /* almost all the verbs */
#include "mail-session.h" /* mail_session_forget_passwords */

#include "folder-browser-ui.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff */


/*
 * Add with 'folder_browser'
 */

static BonoboUIVerb message_verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("MailNext", next_msg),
	BONOBO_UI_UNSAFE_VERB ("MailNextFlagged", next_flagged_msg),
	BONOBO_UI_UNSAFE_VERB ("MailNextUnread", next_unread_msg),
	BONOBO_UI_UNSAFE_VERB ("MailNextThread", next_thread),
	BONOBO_UI_UNSAFE_VERB ("MailPrevious", previous_msg),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousFlagged", previous_flagged_msg),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousUnread", previous_unread_msg),
	BONOBO_UI_UNSAFE_VERB ("AddSenderToAddressbook", add_sender_to_addrbook),
	BONOBO_UI_UNSAFE_VERB ("MessageApplyFilters", apply_filters),
	BONOBO_UI_UNSAFE_VERB ("MessageCopy", copy_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageDelete", delete_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageForward", forward),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardAttached", forward_attached),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardInline", forward_inline),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardQuoted", forward_quoted),
	BONOBO_UI_UNSAFE_VERB ("MessageRedirect", redirect),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsRead", mark_as_seen),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnRead", mark_as_unseen),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsImportant", mark_as_important),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnimportant", mark_as_unimportant),
	BONOBO_UI_UNSAFE_VERB ("MessageFollowUpFlag", flag_for_followup),
	BONOBO_UI_UNSAFE_VERB ("MessageMove", move_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageOpen", open_message),
	BONOBO_UI_UNSAFE_VERB ("MessagePostReply", post_reply),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyAll", reply_to_all),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyList", reply_to_list),
	BONOBO_UI_UNSAFE_VERB ("MessageReplySender", reply_to_sender),
	BONOBO_UI_UNSAFE_VERB ("MessageResend", resend_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageSaveAs", save_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageSearch", search_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageUndelete", undelete_msg),
	BONOBO_UI_UNSAFE_VERB ("PrintMessage", print_msg),
	BONOBO_UI_UNSAFE_VERB ("TextZoomIn", zoom_in),
	BONOBO_UI_UNSAFE_VERB ("TextZoomOut", zoom_out),
	BONOBO_UI_UNSAFE_VERB ("TextZoomReset", zoom_reset),
	BONOBO_UI_UNSAFE_VERB ("PrintPreviewMessage", print_preview_msg),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterMailingList", filter_mlist),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterRecipient", filter_recipient),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterSender", filter_sender),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilterSubject", filter_subject),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderMailingList", vfolder_mlist),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderRecipient", vfolder_recipient),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderSender", vfolder_sender),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolderSubject", vfolder_subject),
	BONOBO_UI_UNSAFE_VERB ("ViewLoadImages", load_images),
	/* ViewHeaders stuff is a radio */

	BONOBO_UI_VERB_END
};

static BonoboUIVerb list_verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("EditCut", folder_browser_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", folder_browser_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", folder_browser_paste),
	BONOBO_UI_UNSAFE_VERB ("EditInvertSelection", invert_selection),
	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", select_all),
        BONOBO_UI_UNSAFE_VERB ("EditSelectThread", select_thread),
	BONOBO_UI_UNSAFE_VERB ("ChangeFolderProperties", configure_folder),
	BONOBO_UI_UNSAFE_VERB ("FolderExpunge", expunge_folder),
	/* HideDeleted is a toggle */
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAllAsRead", mark_all_as_seen),
	BONOBO_UI_UNSAFE_VERB ("ViewHideRead", hide_read),
	BONOBO_UI_UNSAFE_VERB ("ViewHideSelected", hide_selected),
	BONOBO_UI_UNSAFE_VERB ("ViewShowAll", hide_none),
	/* ViewThreaded is a toggle */

	BONOBO_UI_VERB_END
};

static BonoboUIVerb global_verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("EmptyTrash", empty_trash),
	BONOBO_UI_UNSAFE_VERB ("ForgetPasswords", mail_session_forget_passwords),
	BONOBO_UI_UNSAFE_VERB ("MailCompose", compose_msg),
	BONOBO_UI_UNSAFE_VERB ("MailPost", post_message),
	BONOBO_UI_UNSAFE_VERB ("MailStop", stop_threads),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilters", filter_edit),
	BONOBO_UI_UNSAFE_VERB ("ToolsSubscriptions", manage_subscriptions),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolders", vfolder_edit_vfolders),
	/* ViewPreview is a toggle */

	BONOBO_UI_VERB_END
};

static EPixmap message_pixcache [] = {
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

static EPixmap list_pixcache [] = {
	E_PIXMAP ("/commands/ChangeFolderProperties", "configure_16_folder.xpm"),
	E_PIXMAP ("/commands/ViewHideRead", "hide_read_messages.xpm"),
	E_PIXMAP ("/commands/ViewHideSelected", "hide_selected_messages.xpm"),
	E_PIXMAP ("/commands/ViewShowAll", "show_all_messages.xpm"),
	
	E_PIXMAP ("/commands/EditCut", "16_cut.png"),
	E_PIXMAP ("/commands/EditCopy", "16_copy.png"),
	E_PIXMAP ("/commands/EditPaste", "16_paste.png"),

	E_PIXMAP_END
};

static EPixmap global_pixcache [] = {
	E_PIXMAP ("/commands/MailCompose", "new-message.xpm"),
	
	E_PIXMAP_END
};

enum {
	IS_DRAFTS_FOLDER          = (1 << 0),
	IS_OUTBOX_FOLDER          = (1 << 1),
	IS_SENT_FOLDER            = (1 << 2),
	
	IS_OUTGOING_FOLDER        = (IS_DRAFTS_FOLDER | IS_OUTBOX_FOLDER | IS_SENT_FOLDER),
	IS_INCOMING_FOLDER        = (1 << 3),
	
	IS_ANY_FOLDER             = (IS_OUTGOING_FOLDER | IS_INCOMING_FOLDER),

	SELECTION_NONE            = (1 << 4),
	SELECTION_SINGLE          = (1 << 5),
	SELECTION_MULTIPLE        = (1 << 6),
	
	SELECTION_ANYTHING        = (SELECTION_SINGLE | SELECTION_MULTIPLE),

	IS_THREADED		  = (1 << 7),
	NOT_THREADED		  = (1<<8),
	ANY_THREADED		  = (IS_THREADED|NOT_THREADED),

	HAS_UNDELETED             = (1 << 9),
	HAS_DELETED               = (1 << 10),
	HAS_UNREAD                = (1 << 11),
	HAS_READ                  = (1 << 12),
	HAS_UNIMPORTANT           = (1 << 13),
	HAS_IMPORTANT             = (1 << 14)
};

#define HAS_FLAGS (HAS_UNDELETED | HAS_DELETED |  \
		   HAS_UNREAD | HAS_READ | \
		   HAS_UNIMPORTANT | HAS_IMPORTANT)

#define IS_1MESSAGE (IS_ANY_FOLDER | SELECTION_SINGLE | ANY_THREADED | HAS_FLAGS)
#define IS_0MESSAGE (IS_ANY_FOLDER | SELECTION_ANYTHING | SELECTION_NONE | ANY_THREADED | HAS_FLAGS)
#define IS_NMESSAGE (IS_ANY_FOLDER | SELECTION_ANYTHING | ANY_THREADED | HAS_FLAGS)

struct _UINode {
	const char *name;
	guint32 enable_mask;
};

struct _UINode default_ui_nodes[] = {
	{ "ViewLoadImages",  IS_1MESSAGE },
	{ "ViewFullHeaders", IS_0MESSAGE },
	{ "ViewNormal",      IS_0MESSAGE },
	{ "ViewSource",      IS_0MESSAGE },
	
	{ "AddSenderToAddressbook",   IS_INCOMING_FOLDER | SELECTION_SINGLE | ANY_THREADED | HAS_FLAGS },
	
	{ "MessageResend",            IS_SENT_FOLDER | SELECTION_SINGLE | ANY_THREADED | HAS_FLAGS },
	
	/* actions that work on exactly 1 message */
	{ "MessagePostReply",         IS_1MESSAGE },
	{ "MessageReplyAll",          IS_1MESSAGE },
	{ "MessageReplyList",         IS_1MESSAGE },
	{ "MessageReplySender",       IS_1MESSAGE },
	{ "MessageForwardInline",     IS_1MESSAGE },
	{ "MessageForwardQuoted",     IS_1MESSAGE },
	{ "MessageRedirect",          IS_1MESSAGE },
	{ "MessageSearch",            IS_1MESSAGE },
	
	{ "PrintMessage",             IS_1MESSAGE },
	{ "PrintPreviewMessage",      IS_1MESSAGE },
	
	{ "ToolsFilterMailingList",   IS_1MESSAGE },
	{ "ToolsFilterRecipient",     IS_1MESSAGE },
	{ "ToolsFilterSender",        IS_1MESSAGE },
	{ "ToolsFilterSubject",       IS_1MESSAGE },
	
	{ "ToolsVFolderMailingList",  IS_1MESSAGE },
	{ "ToolsVFolderRecipient",    IS_1MESSAGE },
	{ "ToolsVFolderSender",       IS_1MESSAGE },
	{ "ToolsVFolderSubject",      IS_1MESSAGE },
	
	/* actions that work on >= 1 message */
	{ "MessageApplyFilters",      IS_NMESSAGE },
	{ "MessageCopy",              IS_NMESSAGE },
	{ "MessageMove",              IS_NMESSAGE },
	{ "MessageDelete",            IS_NMESSAGE },
	{ "MessageUndelete",          IS_NMESSAGE & ~HAS_DELETED },
	{ "MessageMarkAsRead",        IS_NMESSAGE & ~HAS_UNREAD },
	{ "MessageMarkAsUnRead",      IS_NMESSAGE & ~HAS_READ },
	{ "MessageMarkAsImportant",   IS_NMESSAGE & ~HAS_UNIMPORTANT },
	{ "MessageMarkAsUnimportant", IS_NMESSAGE & ~HAS_IMPORTANT },
	{ "MessageFollowUpFlag",      IS_NMESSAGE },
	{ "MessageOpen",              IS_NMESSAGE },
	{ "MessageSaveAs",            IS_NMESSAGE },
	{ "MessageForward",           IS_NMESSAGE },
	{ "MessageForwardAttached",   IS_NMESSAGE },
	
	{ "EditCut",                  IS_NMESSAGE },
	{ "EditCopy",                 IS_NMESSAGE },
	{ "EditPaste",                IS_NMESSAGE },
	{ "EditSelectThread",	      IS_ANY_FOLDER | SELECTION_ANYTHING | IS_THREADED | HAS_FLAGS},

	{ "ViewHideSelected",         IS_NMESSAGE },
	
	/* FIXME: should these be single-selection? */
	{ "MailNext",                 IS_NMESSAGE },
	{ "MailNextFlagged",          IS_NMESSAGE },
	{ "MailNextUnread",           IS_NMESSAGE },
	{ "MailNextThread",           IS_NMESSAGE },
	{ "MailPrevious",             IS_NMESSAGE },
	{ "MailPreviousFlagged",      IS_NMESSAGE },
	{ "MailPreviousUnread",       IS_NMESSAGE },
};

static int num_default_ui_nodes = sizeof (default_ui_nodes) / sizeof (default_ui_nodes[0]);


static void
ui_add (FolderBrowser *fb, const char *name, BonoboUIVerb verb[], EPixmap pixcache[])
{
	BonoboUIComponent *uic = fb->uicomp;
	char *file;
	
	bonobo_ui_component_add_verb_list_with_data (uic, verb, fb);
	
	/*bonobo_ui_component_freeze (uic, NULL);*/
	
	file = g_strconcat ("evolution-mail-", name, ".xml", NULL);
	bonobo_ui_util_set_ui (uic, EVOLUTION_DATADIR, file, "evolution-mail");
	g_free (file);
	
	e_pixmaps_update (uic, pixcache);
	
	/*bonobo_ui_component_thaw (uic, NULL);*/
}

/* more complex stuff */

static void
display_view (GalViewInstance *instance, GalView *view, gpointer data)
{
	FolderBrowser *fb = data;
	
	if (GAL_IS_VIEW_ETABLE (view)) {
		gal_view_etable_attach_tree (GAL_VIEW_ETABLE (view), fb->message_list->tree);
	}
}

void
folder_browser_ui_setup_view_menus (FolderBrowser *fb)
{
	static GalViewCollection *collection = NULL;
	char *id;
	gboolean outgoing;
	
	if (fb->uicomp == NULL || fb->folder == NULL)
		return;
	
	g_assert (fb->view_instance == NULL);
	g_assert (fb->view_menus == NULL);
	
	outgoing = folder_browser_is_drafts (fb) ||
		folder_browser_is_sent (fb) ||
		folder_browser_is_outbox (fb);
	
	if (collection == NULL) {
		ETableSpecification *spec;
		char *local_dir;
		GalViewFactory *factory;
		
		collection = gal_view_collection_new();
		
		gal_view_collection_set_title (collection, _("Mail"));
		
		local_dir = gnome_util_prepend_user_home ("/evolution/views/mail/");
		gal_view_collection_set_storage_directories
			(collection,
			 EVOLUTION_DATADIR "/evolution/views/mail/",
			 local_dir);
		g_free (local_dir);
		
		spec = e_table_specification_new();
		e_table_specification_load_from_file(spec, EVOLUTION_ETSPECDIR "/message-list.etspec");
		
		factory = gal_view_factory_etable_new (spec);
		gtk_object_unref (GTK_OBJECT (spec));
		gal_view_collection_add_factory (collection, factory);
		gtk_object_unref (GTK_OBJECT (factory));
		
		gal_view_collection_load(collection);
	}
	
	id = mail_config_folder_to_safe_url(fb->folder);
	fb->view_instance = gal_view_instance_new (collection, id);
	g_free (id);
	
	if (outgoing)
		gal_view_instance_set_default_view (fb->view_instance, "As_Sent_Folder");
	
	if (!gal_view_instance_exists (fb->view_instance)) {
		char *path;
		struct stat st;
		
		gal_view_instance_load (fb->view_instance);
		
		path = mail_config_folder_to_cachename (fb->folder, "et-header-");
		if (path && stat (path, &st) == 0 && st.st_size > 0 && S_ISREG (st.st_mode)) {
			ETableSpecification *spec;
			ETableState *state;
			GalView *view;
			
			spec = e_table_specification_new();
			e_table_specification_load_from_file(spec, EVOLUTION_ETSPECDIR "/message-list.etspec");
			view = gal_view_etable_new(spec, "");
			gtk_object_unref (GTK_OBJECT (spec));
			
			state = e_table_state_new ();
			e_table_state_load_from_file (state, path);
			gal_view_etable_set_state (GAL_VIEW_ETABLE (view), state);
			gtk_object_unref (GTK_OBJECT (state));
			
			gal_view_instance_set_custom_view (fb->view_instance, view);
			gtk_object_unref (GTK_OBJECT (view));
		}
		g_free (path);
	}
	
	fb->view_menus = gal_view_menus_new (fb->view_instance);
	gal_view_menus_apply (fb->view_menus, fb->uicomp, NULL);
	
	/* Due to CORBA reentrancy, the view could be gone now. */
	if (fb->view_instance == NULL)
		return;

	gtk_signal_connect (GTK_OBJECT (fb->view_instance), "display_view",
			    display_view, fb);
	display_view (fb->view_instance, gal_view_instance_get_current_view (fb->view_instance), fb);
}

/* Gets rid of the view instance and view menus objects */
void
folder_browser_ui_discard_view_menus (FolderBrowser *fb)
{
	g_assert (fb->view_instance != NULL);
	g_assert (fb->view_menus != NULL);
	
	gtk_object_unref (GTK_OBJECT (fb->view_instance));
	fb->view_instance = NULL;
	
	gtk_object_unref (GTK_OBJECT (fb->view_menus));
	fb->view_menus = NULL;
}

void
folder_browser_ui_message_list_focus (FolderBrowser *fb)
{
	g_assert (fb->uicomp != NULL);
	
	bonobo_ui_component_set_prop (fb->uicomp, "/commands/EditInvertSelection",
				      "sensitive", "1", NULL);
/*	bonobo_ui_component_set_prop (fb->uicomp, "/commands/EditSelectThread",
	"sensitive", "1", NULL);*/
}

void
folder_browser_ui_message_list_unfocus (FolderBrowser *fb)
{
	g_assert (fb->uicomp != NULL);
	
	bonobo_ui_component_set_prop (fb->uicomp, "/commands/EditInvertSelection",
				      "sensitive", "0", NULL);
	/*bonobo_ui_component_set_prop (fb->uicomp, "/commands/EditSelectThread",
	  "sensitive", "0", NULL);*/
}

static void
folder_browser_setup_property_menu (FolderBrowser *fb, BonoboUIComponent *uic)
{
	char *name, *base = NULL;
	CamelURL *url;
	
	url = camel_url_new (fb->uri, NULL);
	if (url) {
		if (url->fragment)
			base = g_basename (url->fragment);
		else
			base = g_basename (url->path);
	}
	
	if (base && base [0] != 0)
		name = g_strdup_printf (_("Properties for \"%s\""), base);
	else
		name = g_strdup (_("Properties"));
	
	bonobo_ui_component_set_prop (
		uic, "/menu/File/Folder/ComponentPlaceholder/ChangeFolderProperties",
		"label", name, NULL);
	g_free (name);
	
	if (url)
		camel_url_free (url);
	
	fbui_sensitise_item (fb, "ChangeFolderProperties",
			     (strncmp (fb->uri, "vfolder:", 8) == 0 || strncmp (fb->uri, "file:", 5) == 0));
}

/* Must be in the same order as MailConfigDisplayStyle */
/* used in folder-browser.c as well (therefore not static) */
char *message_display_styles[] = {
	"/commands/ViewNormal",
	"/commands/ViewFullHeaders",
	"/commands/ViewSource"
};

/* public */

void 
folder_browser_ui_add_message (FolderBrowser *fb)
{
	int state;
	BonoboUIComponent *uic = fb->uicomp;
	FolderBrowserSelectionState prev_state;
	
	if (fb->sensitise_state) {
		g_hash_table_destroy(fb->sensitise_state);
		fb->sensitise_state = NULL;
	}
	
	ui_add (fb, "message", message_verbs, message_pixcache);
	
	/* Display Style */
	state = fb->mail_display->display_style;
	bonobo_ui_component_set_prop (uic, message_display_styles[state],
				      "state", "1", NULL);
	bonobo_ui_component_add_listener (uic, "ViewNormal", folder_browser_set_message_display_style, fb);
	bonobo_ui_component_add_listener (uic, "ViewFullHeaders", folder_browser_set_message_display_style, fb);
	bonobo_ui_component_add_listener (uic, "ViewSource", folder_browser_set_message_display_style, fb);
	/* FIXME: this kind of bypasses bonobo but seems the only way when we change components */
	folder_browser_set_message_display_style (uic, strrchr (message_display_styles[state], '/') + 1,
						  Bonobo_UIComponent_STATE_CHANGED, "1", fb);
	
	/* Resend Message */
	if (fb->folder && !folder_browser_is_sent (fb)) 
		fbui_sensitise_item (fb, "MessageResend", FALSE);
	
	/* sensitivity of message-specific commands */
	prev_state = fb->selection_state;
	fb->selection_state = FB_SELSTATE_UNDEFINED;
	folder_browser_ui_set_selection_state (fb, prev_state);
	
	/* Charset picker */
	e_charset_picker_bonobo_ui_populate (uic, "/menu/View", FB_DEFAULT_CHARSET,
					     folder_browser_charset_changed,
					     fb);
}

void 
folder_browser_ui_add_list (FolderBrowser *fb)
{
	BonoboUIComponent *uic = fb->uicomp;
	int state;
	
	if (fb->sensitise_state) {
		g_hash_table_destroy(fb->sensitise_state);
		fb->sensitise_state = NULL;
	}
	
	ui_add (fb, "list", list_verbs, list_pixcache);
	
	/* Hide Deleted */
	if (fb->folder && (fb->folder->folder_flags & CAMEL_FOLDER_IS_TRASH)) {
		fbui_sensitise_item (fb, "HideDeleted", FALSE);
		state = FALSE;
	} else {
		state = mail_config_get_hide_deleted ();
	}
	bonobo_ui_component_set_prop (uic, "/commands/HideDeleted", "state", state ? "1" : "0", NULL);
	bonobo_ui_component_add_listener (uic, "HideDeleted", folder_browser_toggle_hide_deleted, fb);
	/* FIXME: this kind of bypasses bonobo but seems the only way when we change components */
	folder_browser_toggle_hide_deleted (uic, "", Bonobo_UIComponent_STATE_CHANGED,
					    state ? "1" : "0", fb);
	
	/* Threaded toggle */
	state = mail_config_get_thread_list (FOLDER_BROWSER (fb)->uri);
	bonobo_ui_component_set_prop (uic, "/commands/ViewThreaded", "state", state ? "1" : "0", NULL);
	bonobo_ui_component_add_listener (uic, "ViewThreaded", folder_browser_toggle_threads, fb);
	/* FIXME: this kind of bypasses bonobo but seems the only way when we change components */
	folder_browser_toggle_threads (uic, "", Bonobo_UIComponent_STATE_CHANGED,
				       state ? "1" : "0", fb);
	
	/* Property menu */
	folder_browser_setup_property_menu (fb, fb->uicomp);
	
	/* View menu */
	if (fb->view_instance == NULL)
		folder_browser_ui_setup_view_menus (fb);
}

void 
folder_browser_ui_rm_list (FolderBrowser *fb)
{
	/* View menu */
	if (fb->view_instance != NULL)
		folder_browser_ui_discard_view_menus (fb);
}

void 
folder_browser_ui_add_global (FolderBrowser *fb)
{
	int state;
	BonoboUIComponent *uic = fb->uicomp;

	if (fb->sensitise_state) {
		g_hash_table_destroy(fb->sensitise_state);
		fb->sensitise_state = NULL;
	}
	
	ui_add (fb, "global", global_verbs, global_pixcache);
	
	/* (Pre)view toggle */
	
	state = mail_config_get_show_preview (FOLDER_BROWSER (fb)->uri);
	bonobo_ui_component_set_prop (uic, "/commands/ViewPreview", "state", state ? "1" : "0", NULL);
	bonobo_ui_component_add_listener (uic, "ViewPreview", folder_browser_toggle_preview, fb);
	/* FIXME: this kind of bypasses bonobo but seems the only way when we change components */
	folder_browser_toggle_preview (uic, "", Bonobo_UIComponent_STATE_CHANGED, state ? "1" : "0", fb);
	
	/* Stop button */
	/* TODO: Go through cache, but we can't becaus eof mail-mt.c:set_stop at the moment */
	bonobo_ui_component_set_prop(uic, "/commands/MailStop", "sensitive", "0", NULL);
}

void 
folder_browser_ui_rm_all (FolderBrowser *fb)
{
	BonoboUIComponent *uic = fb->uicomp;
	
	bonobo_ui_component_rm (uic, "/", NULL);
 	bonobo_ui_component_unset_container (uic);
	
	if (fb->sensitise_state) {
		g_hash_table_destroy (fb->sensitise_state);
		fb->sensitise_state = NULL;
	}
}

void
fbui_sensitise_item (FolderBrowser *fb, const char *item, int state)
{
	char *name, *key;
	int val;
	
	/* If this whole caching idea doesn't work, remove it here */
	if (fb->sensitise_state == NULL)
		fb->sensitise_state = g_hash_table_new (g_str_hash, g_str_equal);
	
	if (g_hash_table_lookup_extended (fb->sensitise_state, item, (void **)&key, (void **)&val)) {
		if (val == state)
			return;
	}
	
	if (fb->uicomp) {
		name = alloca (strlen (item) + strlen ("/commands/") + 1);
		sprintf (name, "/commands/%s", item);
		bonobo_ui_component_set_prop (fb->uicomp, name, "sensitive", state ? "1" : "0", NULL);
		g_hash_table_insert (fb->sensitise_state, (char *) item, (gpointer) state);
	}
}

static void
fbui_sensitize_items (FolderBrowser *fb, guint32 enable_mask)
{
	gboolean enable;
	int i;
	
	for (i = 0; i < num_default_ui_nodes; i++) {
		enable = (default_ui_nodes[i].enable_mask & enable_mask) == enable_mask;
		fbui_sensitise_item (fb, default_ui_nodes[i].name, enable);
	}
}

void 
folder_browser_ui_scan_selection (FolderBrowser *fb)
{
	gboolean outgoing = FALSE;
	guint32 enable_mask = 0;
	
	if (fb->selection_state == FB_SELSTATE_SINGLE || 
	    fb->selection_state == FB_SELSTATE_MULTIPLE) {
		GPtrArray *uids;
		CamelMessageInfo *info;
		guint32 temp_mask = 0;
		int i;
		
		uids = g_ptr_array_new ();
		message_list_foreach (fb->message_list, enumerate_msg, uids);

		for (i = 0; i < uids->len; i++) {

			info = camel_folder_get_message_info (fb->folder, uids->pdata[i]);
			if (info == NULL)
				continue;

			if (info->flags & CAMEL_MESSAGE_DELETED)
				temp_mask |= HAS_DELETED;
			else
				temp_mask |= HAS_UNDELETED;

			if (info->flags & CAMEL_MESSAGE_SEEN)
				temp_mask |= HAS_READ;
			else
				temp_mask |= HAS_UNREAD;

			if (info->flags & CAMEL_MESSAGE_FLAGGED)
				temp_mask |= HAS_IMPORTANT;
			else
				temp_mask |= HAS_UNIMPORTANT;


			camel_folder_free_message_info (fb->folder, info);
			g_free (uids->pdata[i]);
		}

		g_ptr_array_free (uids, TRUE);

		/* yeah, the naming is a bit backwards, but we need to support
		 * the case when, say, both a deleted and an undeleted message
		 * are selected. Both the Delete and Undelete menu items should
		 * be sensitized, but the only good way to set the flags is as
		 * above. Anyway, the naming is a bit of a lie but it works out
		 * so that it's sensible both above and in the definition of
		 * the UI items, so deal with it.
		 */

		enable_mask |= (~temp_mask & HAS_FLAGS);
	}

	if (folder_browser_is_drafts (fb)) {
		enable_mask |= IS_DRAFTS_FOLDER;
		outgoing = TRUE;
	}
	
	if (folder_browser_is_outbox (fb)) {
		enable_mask |= IS_OUTBOX_FOLDER;
		outgoing = TRUE;
	}
	
	if (folder_browser_is_sent (fb)) {
		enable_mask |= IS_SENT_FOLDER;
		outgoing = TRUE;
	}
	
	if (fb->message_list && fb->message_list->threaded)
		enable_mask |= IS_THREADED;
	else
		enable_mask |= NOT_THREADED;
	
	if (outgoing == FALSE)
		enable_mask |= IS_INCOMING_FOLDER;
	
	switch (fb->selection_state) {
	case FB_SELSTATE_SINGLE:
		enable_mask |= SELECTION_SINGLE;
		break;
	case FB_SELSTATE_MULTIPLE:
		enable_mask |= SELECTION_MULTIPLE;
		break;
	case FB_SELSTATE_NONE:
	default:
		enable_mask |= SELECTION_NONE;
		break;
	}

	fbui_sensitize_items (fb, enable_mask);
}

void 
folder_browser_ui_set_selection_state (FolderBrowser *fb, FolderBrowserSelectionState state)
{
	/* the state may be the same but with
	 * different messages selected, necessitating
	 * a recheck of the flags of the selected
	 * messages.
	 */

	if (state == fb->selection_state && 
	    state != FB_SELSTATE_SINGLE &&
	    state != FB_SELSTATE_MULTIPLE)
		return;
	
	fb->selection_state = state;
	folder_browser_ui_scan_selection (fb);
}

void
folder_browser_ui_message_loaded (FolderBrowser *fb)
{
	BonoboUIComponent *uic = fb->uicomp;
	
	if (uic) {
		fb->selection_state = FB_SELSTATE_NONE;
		folder_browser_ui_set_selection_state (fb, FB_SELSTATE_SINGLE);
	}
}
