/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * folder-browser-ui.c: Sets up the Bonobo UI for FolderBrowsers
 *
 * Author:
 *   Peter Williams <peterw@ximian.com>
 *
 * (C) 2001 Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-util.h> /* gnome_util_prepend_user_home */

#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>

#include "widgets/menus/gal-view-menus.h" /* GalView stuff */
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>

#include "mail-callbacks.h" /* almost all the verbs */
#include "mail-session.h" /* mail_session_forget_passwords */

#include "folder-browser-ui.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff */
#include "camel/camel-vtrash-folder.h"       /* vtrash checking */

/*
 * Add with 'folder_browser'
 */

static BonoboUIVerb message_verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("MailNext", next_msg),
	BONOBO_UI_UNSAFE_VERB ("MailNextFlagged", next_flagged_msg),
	BONOBO_UI_UNSAFE_VERB ("MailNextUnread", next_unread_msg),
/*	BONOBO_UI_UNSAFE_VERB ("MailNextThread", next_thread),*/
	BONOBO_UI_UNSAFE_VERB ("MailPrevious", previous_msg),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousFlagged", previous_flagged_msg),
	BONOBO_UI_UNSAFE_VERB ("MailPreviousUnread", previous_unread_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageApplyFilters", apply_filters),
	BONOBO_UI_UNSAFE_VERB ("MessageCopy", copy_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageDelete", delete_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageForward", forward),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardAttached", forward_attached),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardInline", forward_inline),
	BONOBO_UI_UNSAFE_VERB ("MessageForwardQuoted", forward_quoted),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsRead", mark_as_seen),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnRead", mark_as_unseen),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsImportant", mark_as_important),
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAsUnimportant", mark_as_unimportant),
	BONOBO_UI_UNSAFE_VERB ("MessageMove", move_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageOpen", open_message),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyAll", reply_to_all),
	BONOBO_UI_UNSAFE_VERB ("MessageReplyList", reply_to_list),
	BONOBO_UI_UNSAFE_VERB ("MessageReplySender", reply_to_sender),
	BONOBO_UI_UNSAFE_VERB ("MessageResend", resend_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageSaveAs", save_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageSearch", search_msg),
	BONOBO_UI_UNSAFE_VERB ("MessageUndelete", undelete_msg),
	BONOBO_UI_UNSAFE_VERB ("PrintMessage", print_msg),
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
	BONOBO_UI_UNSAFE_VERB ("MailGetSend", send_receive_mail),
	BONOBO_UI_UNSAFE_VERB ("MailStop", stop_threads),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilters", filter_edit),
	BONOBO_UI_UNSAFE_VERB ("ToolsSettings", providers_config),
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
	E_PIXMAP ("/commands/MailGetSend", "send-receive.xpm"),
	E_PIXMAP ("/commands/ToolsSettings", "configure_16_mail.xpm"),
	
	E_PIXMAP ("/Toolbar/MailGetSend", "buttons/send-24-receive.png"),
	E_PIXMAP ("/Toolbar/MailCompose", "buttons/compose-message.png"),

	E_PIXMAP_END
};

static void ui_add (FolderBrowser *fb,
		    const gchar *name,
		    BonoboUIVerb verb[],
		    EPixmap pixcache[])
{
	BonoboUIComponent *uic = fb->uicomp;
	gchar *file;

	bonobo_ui_component_add_verb_list_with_data (uic, verb, fb);

	bonobo_ui_component_freeze (uic, NULL);

	file = g_strconcat ("evolution-mail-", name, ".xml", NULL);
	bonobo_ui_util_set_ui (uic, EVOLUTION_DATADIR, file, "evolution-mail");
	g_free (file);

	e_pixmaps_update (uic, pixcache);
	
	bonobo_ui_component_thaw (uic, NULL);
}

/* more complex stuff */

static void
display_view(GalViewCollection *collection,
	     GalView *view,
	     gpointer data)
{
	FolderBrowser *fb = data;
	if (GAL_IS_VIEW_ETABLE(view)) {
		e_tree_set_state_object(fb->message_list->tree, GAL_VIEW_ETABLE(view)->state);
	}
}

static void
folder_browser_setup_view_menus (FolderBrowser *fb,
				 BonoboUIComponent *uic)
{
	GalViewFactory *factory;
	ETableSpecification *spec;
	char *local_dir;

	g_assert (fb->view_collection == NULL);
	g_assert (fb->view_menus == NULL);

	fb->view_collection = gal_view_collection_new();

	local_dir = gnome_util_prepend_user_home ("/evolution/views/mail/");
	gal_view_collection_set_storage_directories(
		fb->view_collection,
		EVOLUTION_DATADIR "/evolution/views/mail/",
		local_dir);
	g_free (local_dir);

	spec = e_table_specification_new();
	e_table_specification_load_from_file(spec, EVOLUTION_ETSPECDIR "/message-list.etspec");

	factory = gal_view_factory_etable_new (spec);
	gtk_object_unref (GTK_OBJECT (spec));
	gal_view_collection_add_factory (fb->view_collection, factory);
	gtk_object_unref (GTK_OBJECT (factory));

	gal_view_collection_load(fb->view_collection);

	fb->view_menus = gal_view_menus_new(fb->view_collection);
	gal_view_menus_apply(fb->view_menus, uic, NULL);
	gtk_signal_connect(GTK_OBJECT(fb->view_collection), "display_view",
			   display_view, fb);
}

/* Gets rid of the view collection and view menus objects */
static void
folder_browser_discard_view_menus (FolderBrowser *fb)
{
	g_assert (fb->view_collection != NULL);
	g_assert (fb->view_menus != NULL);

	gtk_object_unref (GTK_OBJECT (fb->view_collection));
	fb->view_collection = NULL;

	gtk_object_unref (GTK_OBJECT (fb->view_menus));
	fb->view_menus = NULL;
}

static void
folder_browser_setup_property_menu (FolderBrowser *fb,
				    BonoboUIComponent *uic)
{
	char *name, *base = NULL;

	if (fb->uri)
		base = g_basename (fb->uri);

	if (base && base [0] != 0)
		name = g_strdup_printf (_("Properties for \"%s\""), base);
	else
		name = g_strdup (_("Properties"));

	bonobo_ui_component_set_prop (
		uic, "/menu/File/Folder/ComponentPlaceholder/ChangeFolderProperties",
		"label", name, NULL);
	g_free (name);
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
		bonobo_ui_component_set_prop (uic, "/commands/MessageResend", "sensitive", "0", NULL);

	/* sensitivity of message-specific commands */

	prev_state = fb->selection_state;
	fb->selection_state = FB_SELSTATE_UNDEFINED;
	folder_browser_ui_set_selection_state (fb, prev_state);
}

/*
void 
folder_browser_ui_rm_message (FolderBrowser *fb)
{
	ui_rm (fb, "message", message_verbs, message_pixcache);
}
*/

void 
folder_browser_ui_add_list (FolderBrowser *fb)
{
	int state;
	BonoboUIComponent *uic = fb->uicomp;
	
	ui_add (fb, "list", list_verbs, list_pixcache);
	
	/* Hide Deleted */
	if (fb->folder && CAMEL_IS_VTRASH_FOLDER (fb->folder)) {
		bonobo_ui_component_set_prop (uic, "/commands/HideDeleted", "sensitive", "0", NULL);
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
	folder_browser_setup_view_menus (fb, fb->uicomp);
}

void 
folder_browser_ui_rm_list (FolderBrowser *fb)
{
	/* View menu */
	folder_browser_discard_view_menus (fb);
}

void 
folder_browser_ui_add_global (FolderBrowser *fb)
{
	int state;
	BonoboUIComponent *uic = fb->uicomp;

	ui_add (fb, "global", global_verbs, global_pixcache);

	/* (Pre)view toggle */

	state = mail_config_get_show_preview (FOLDER_BROWSER (fb)->uri);
	bonobo_ui_component_set_prop (uic, "/commands/ViewPreview", "state", state ? "1" : "0", NULL);
	bonobo_ui_component_add_listener (uic, "ViewPreview", folder_browser_toggle_preview, fb);
	/* FIXME: this kind of bypasses bonobo but seems the only way when we change components */
	folder_browser_toggle_preview (uic, "", Bonobo_UIComponent_STATE_CHANGED, state ? "1" : "0", fb);
	
	/* Stop button */

	bonobo_ui_component_set_prop(uic, "/commands/MailStop", "sensitive", "0", NULL);
}

/*
void 
folder_browser_ui_rm_global (FolderBrowser *fb)
{
}
*/

void 
folder_browser_ui_rm_all (FolderBrowser *fb)
{
	BonoboUIComponent *uic = fb->uicomp;

	bonobo_ui_component_rm (uic, "/", NULL);
 	bonobo_ui_component_unset_container (uic);
}

struct sensitize_data {
	const char **items;
	gboolean enable;
};

static void
fbui_real_sensitize_items (BonoboUIComponent *uic, const char **items, gboolean enable)
{
	int i;
	char name_buf[256]; /* this should really be large enough */
	char *value;

	if (enable)
		value = "1";
	else
		value = "0";

	for (i = 0; items[i]; i++) {
		sprintf (name_buf, "/commands/%s", items[i]);
		bonobo_ui_component_set_prop (uic, name_buf, "sensitive", value, NULL);
	}
}

static gboolean
fbui_sensitize_timeout (gpointer data)
{
	FolderBrowser *fb = FOLDER_BROWSER (data);
	BonoboUIComponent *uic = fb->uicomp;
	GSList *iter;
	struct sensitize_data *sd;

	if (uic)
		bonobo_ui_component_freeze (uic, NULL);
	for (iter = fb->sensitize_changes; iter; iter = iter->next) {
		sd = (struct sensitize_data *) iter->data;

		/* if UIC == NULL, we could just return TRUE, but we don't
		 * know when UIC is going to come back... when it does,
		 * the UI stuff will be reset, so no problem.
		 */

		if (uic)
			fbui_real_sensitize_items (uic, sd->items, sd->enable);
		g_free (sd);
	}
	if (uic)
		bonobo_ui_component_thaw (uic, NULL);

	g_slist_free (fb->sensitize_changes);
	fb->sensitize_changes = NULL;
	fb->sensitize_timeout_id = 0;
	return FALSE;
}

static void
fbui_sensitize_items (FolderBrowser *fb, const char **items, gboolean enable)
{
	struct sensitize_data *sd;
	GSList *iter;

	/* If we're already updating these items, save an update by
	 * changing the item in the list. */

	for (iter = fb->sensitize_changes; iter; iter = iter->next) {
		sd = (struct sensitize_data *) iter->data;

		if (sd->items == items)
			break;
	}

	if (iter == NULL) {
		sd = g_new (struct sensitize_data, 1);
		sd->items = items;
		sd->enable = enable;

		fb->sensitize_changes = g_slist_prepend (fb->sensitize_changes, sd);
	} else {
		/* Redundant, but shuts up the compiler. */
		sd = (struct sensitize_data *) iter->data;
		sd->enable = enable;
	}

	if (fb->sensitize_timeout_id == 0)
		fb->sensitize_timeout_id = g_timeout_add (110, fbui_sensitize_timeout, fb);
}

static const char *message_pane_enables[] = {
	/* these only work if there's a message in the message pane
	 * (preview pane). This state is independent of how many are
	 * selected. */
	"PrintMessage", "PrintPreviewMessage",
	"ViewFullHeaders", "ViewLoadImages", "ViewNormal", "ViewSource",
	
	NULL
};

void 
folder_browser_ui_set_selection_state (FolderBrowser *fb, FolderBrowserSelectionState state)
{
	/* We'd like to keep the number of changes to be minimal cause
	 * this is a lot of corba traffic. So we break these sets of commands into bits:
	 *
	 * Also remember that everything defaults to sensitized
	 *
	 * Disable:
	 *      NONE = none_disables + multiple_disables
	 *    SINGLE = [nothing disabled]
	 *  MULTIPLE = multiple_disables
	 * UNDEFINED = [nothing disabled]
	 */

	static const char *none_disables[] = {
		/* actions that work on > 0 messages */
		"MessageApplyFilters", 
		"MessageCopy", "MessageMove", 
		"MessageDelete", "MessageUndelete",
		"MessageMarkAsRead", "MessageMarkAsUnRead",
		"MessageMarkAsImportant", "MessageMarkAsUnimportant",
		"MessageOpen", "MessageSaveAs", 
		"MessageForward", "MessageForwardAttached",

		"EditCut", "EditCopy", "EditPaste", "ViewHideSelected",

		NULL
	};

	static const char *multiple_disables[] = {
		/* actions that work on exactly 1 message */
		"MessageReplyAll", "MessageReplyList", "MessageReplySender", "MessageResend", 
		"MessageForwardInline", "MessageForwardQuoted", "MessageSearch",

		"ToolsFilterMailingList", "ToolsFilterRecipient", "ToolsFilterSender",
		"ToolsFilterSubject", "ToolsVFolderMailingList", "ToolsVFolderRecipient", 
		"ToolsVFolderSender", "ToolsVFolderSubject",

		/* moving around -- if we have more than one message selected, it
		 * doesn't behave very. If people complain, it isn't a problem
		 * to put these commands in none_disables tho. */
		"MailNext", "MailNextFlagged", "MailNextUnread", "MailNextThread",
		"MailPrevious", "MailPreviousFlagged", "MailPreviousUnread",

		NULL
	};

	/* assumes that all the appropriate XML's have been loaded */

	if (state == fb->selection_state)
		return;

	switch (state) {
	case FB_SELSTATE_NONE:
		fbui_sensitize_items (fb, none_disables, FALSE);
		if (fb->selection_state != FB_SELSTATE_MULTIPLE)
			fbui_sensitize_items (fb, multiple_disables, FALSE);
		break;
	case FB_SELSTATE_SINGLE:
		if (fb->selection_state != FB_SELSTATE_UNDEFINED)
			fbui_sensitize_items (fb, multiple_disables, TRUE);
		if (fb->selection_state == FB_SELSTATE_NONE)
			fbui_sensitize_items (fb, none_disables, TRUE);
		break;
	case FB_SELSTATE_MULTIPLE:
		if (fb->selection_state == FB_SELSTATE_NONE)
			fbui_sensitize_items (fb, none_disables, TRUE);
		else
			fbui_sensitize_items (fb, multiple_disables, FALSE);
		break;
	case FB_SELSTATE_UNDEFINED:
		printf ("changing to undefined selection state? hah!\n");
		return;
	}

	if (fb->loaded_uid == NULL)
		fbui_sensitize_items (fb, message_pane_enables, FALSE);

	fb->selection_state = state;
}

void
folder_browser_ui_message_loaded (FolderBrowser *fb)
{
	BonoboUIComponent *uic = fb->uicomp;

	if (fb->loaded_uid == NULL && uic)
		fbui_sensitize_items (fb, message_pane_enables, TRUE);
}
