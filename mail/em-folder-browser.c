/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#include <libgnomeprintui/gnome-print-dialog.h>

#include "mail-mt.h"
#include "mail-tools.h"
#include "mail-config.h"

#include <e-util/e-passwords.h>

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

/* for efilterbar stuff */
#include "filter/vfolder-rule.h"
#include <widgets/misc/e-filter-bar.h>
#include <camel/camel-search-private.h>

#include "em-utils.h"
#include "em-format-html-display.h"
#include "em-format-html-print.h"
#include "em-folder-browser.h"
#include "em-subscribe-editor.h"
#include "message-list.h"

#include "evolution-shell-component-utils.h" /* Pixmap stuff, sigh */

#define d(x)

struct _EMFolderBrowserPrivate {
	GtkWidget *preview;	/* container for message display */

	GtkWidget *subscribe_editor;

	int show_preview:1;
	int show_list:1;
};

static void emfb_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state);
static void emfb_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri);

/* FilterBar stuff ... */
static void emfb_search_config_search(EFilterBar *efb, FilterRule *rule, int id, const char *query, void *data);
static void emfb_search_menu_activated(ESearchBar *esb, int id, EMFolderBrowser *fb);
static void emfb_search_search_activated(ESearchBar *esb, EMFolderBrowser *emfb);
static void emfb_search_query_changed(ESearchBar *esb, EMFolderBrowser *fb);

enum {
	ESB_SAVE,
};

static ESearchBarItem emfb_search_items[] = {
	E_FILTERBAR_ADVANCED,
	{ NULL, 0, NULL },
	E_FILTERBAR_SAVE,
	E_FILTERBAR_EDIT,
	{ NULL, 0, NULL },
	{ N_("Create _Virtual Folder From Search..."), ESB_SAVE, NULL  },
	{ NULL, -1, NULL }
};

static EMFolderViewClass *emfb_parent;

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
	/* FIXME ... */
	extern RuleContext *search_context;

	printf("em folder browser init\n");

	p = emfb->priv = g_malloc0(sizeof(struct _EMFolderBrowserPrivate));

	g_slist_free(emfb->view.ui_files);
	emfb->view.ui_files = g_slist_append(NULL, EVOLUTION_UIDIR "/evolution-mail-global.xml");
	emfb->view.ui_files = g_slist_append(emfb->view.ui_files, EVOLUTION_UIDIR "/evolution-mail-list.xml");
	emfb->view.ui_files = g_slist_append(emfb->view.ui_files, EVOLUTION_UIDIR "/evolution-mail-message.xml");

	if (search_context) {
		const char *systemrules = g_object_get_data (G_OBJECT (search_context), "system");
		const char *userrules = g_object_get_data (G_OBJECT (search_context), "user");
		
		emfb->search = e_filter_bar_new(search_context, systemrules, userrules, emfb_search_config_search, emfb);
		e_search_bar_set_menu ((ESearchBar *)emfb->search, emfb_search_items);
		gtk_widget_show((GtkWidget *)emfb->search);

		g_signal_connect(emfb->search, "menu_activated", G_CALLBACK(emfb_search_menu_activated), emfb);
		g_signal_connect(emfb->search, "search_activated", G_CALLBACK(emfb_search_search_activated), emfb);
		g_signal_connect(emfb->search, "query_changed", G_CALLBACK(emfb_search_query_changed), emfb);

		gtk_box_pack_start((GtkBox *)emfb, (GtkWidget *)emfb->search, FALSE, TRUE, 0);
	}

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
	((EMFolderViewClass *)klass)->set_folder = emfb_set_folder;
	((EMFolderViewClass *)klass)->activate = emfb_activate;
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
	if ((emfb->priv->show_preview ^ state) == 0
	    || emfb->view.list == NULL)
		return;
	
	emfb->priv->show_preview = state;
	
	if (state) {
		GConfClient *gconf = mail_config_get_gconf_client ();
		int paned_size /*, y*/;

		paned_size = gconf_client_get_int(gconf, "/apps/evolution/mail/display/paned_size", NULL);

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

/* ********************************************************************** */

/* FIXME: Need to separate system rules from user ones */
/* FIXME: Ugh! */

static void
emfb_search_menu_activated(ESearchBar *esb, int id, EMFolderBrowser *emfb)
{
	EFilterBar *efb = (EFilterBar *)esb;
	
	d(printf("menu activated\n"));
	
	switch (id) {
	case ESB_SAVE:
		d(printf("Save vfolder\n"));
		if (efb->current_query) {
			FilterRule *rule = vfolder_clone_rule(efb->current_query);			
			char *name, *text;
			
			text = e_search_bar_get_text(esb);
			name = g_strdup_printf("%s %s", rule->name, (text&&text[0])?text:"''");
			g_free (text);
			filter_rule_set_name(rule, name);
			g_free (name);
			
			filter_rule_set_source(rule, FILTER_SOURCE_INCOMING);
			vfolder_rule_add_source((VfolderRule *)rule, emfb->view.folder_uri);
			vfolder_gui_add_rule((VfolderRule *)rule);
		}
		break;
	}
}

static void
emfb_search_config_search(EFilterBar *efb, FilterRule *rule, int id, const char *query, void *data)
{
	EMFolderBrowser *emfb = data;
	GList *partl;
	struct _camel_search_words *words;
	int i;
	GSList *strings = NULL;

	/* we scan the parts of a rule, and set all the types we know about to the query string */
	partl = rule->parts;
	while (partl) {
		FilterPart *part = partl->data;
		
		if (!strcmp(part->name, "subject")) {
			FilterInput *input = (FilterInput *)filter_part_find_element(part, "subject");
			if (input)
				filter_input_set_value(input, query);
		} else if (!strcmp(part->name, "body")) {
			FilterInput *input = (FilterInput *)filter_part_find_element(part, "word");
			if (input)
				filter_input_set_value(input, query);
			
			words = camel_search_words_split(query);
			for (i=0;i<words->len;i++)
				strings = g_slist_prepend(strings, g_strdup(words->words[i]->word));
			camel_search_words_free (words);
		} else if(!strcmp(part->name, "sender")) {
			FilterInput *input = (FilterInput *)filter_part_find_element(part, "sender");
			if (input)
				filter_input_set_value(input, query);
		} else if(!strcmp(part->name, "to")) {
			FilterInput *input = (FilterInput *)filter_part_find_element(part, "recipient");
			if (input)
				filter_input_set_value(input, query);
		}
		
		partl = partl->next;
	}

	em_format_html_display_set_search(emfb->view.preview,
					  EM_FORMAT_HTML_DISPLAY_SEARCH_SECONDARY|EM_FORMAT_HTML_DISPLAY_SEARCH_ICASE,
					  strings);
	while (strings) {
		GSList *n = strings->next;

		g_free(strings->data);
		g_slist_free_1(strings);
		strings = n;
	}
}

static void
emfb_search_search_activated(ESearchBar *esb, EMFolderBrowser *emfb)
{
	char *search_word;
	
	if (emfb->view.list == NULL)
		return;
	
	g_object_get (esb, "query", &search_word, NULL);
	message_list_set_search(emfb->view.list, search_word);
	g_free(search_word);
}

static void
emfb_search_query_changed(ESearchBar *esb, EMFolderBrowser *emfb)
{
	int id;
	
	id = e_search_bar_get_item_id(esb);
	if (id == E_FILTERBAR_ADVANCED_ID)
		emfb_search_search_activated(esb, emfb);
}

/* ********************************************************************** */

static void
emfb_edit_invert_selection(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	message_list_invert_selection(emfv->list);
}

static void
emfb_edit_select_all(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	message_list_select_all(emfv->list);
}

static void
emfb_edit_select_thread(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	message_list_select_thread(emfv->list);
}

static void
emfb_folder_properties(BonoboUIComponent *uid, void *data, const char *path)
{
	/* If only we could remove this ... */
	/* Should it be part of the factory? */
	printf("folderproperties\n");
}

static void
emfb_folder_expunge(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderBrowser *emfb = data;

	/* TODO: The old code did a LOT more than this, but was it reqiured? */
	mail_expunge_folder(emfb->view.folder, NULL, NULL);
}

static void
emfb_mark_all_read(BonoboUIComponent *uid, void *data, const char *path)
{
	/* FIXME: make a 'mark messages' function? */
	EMFolderView *emfv = data;
	GPtrArray *uids;
	int i;
	
	uids = camel_folder_get_uids(emfv->folder);
	camel_folder_freeze(emfv->folder);
	for (i=0;i<uids->len;i++)
		camel_folder_set_message_flags(emfv->folder, uids->pdata[i], CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN);
	camel_folder_thaw(emfv->folder);
	camel_folder_free_uids(emfv->folder, uids);
}

static void
emfb_view_hide_read(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	
	message_list_hide_add(emfv->list, "(match-all (system-flag \"seen\"))", ML_HIDE_SAME, ML_HIDE_SAME);
}

static void
emfb_view_hide_selected(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	GPtrArray *uids;

	/* TODO: perhaps this should sit directly on message_list? */
	/* is it worth it, it's so trivial */
	uids = message_list_get_selected(emfv->list);
	message_list_hide_uids(emfv->list, uids);
	message_list_free_uids(emfv->list, uids);
}

static void
emfb_view_show_all(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;

	message_list_hide_clear(emfv->list);
}

/* ********************************************************************** */

static void
emfb_empty_trash(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	emfv = emfv;
	/* FIXME: rename/refactor empty_trash to empty_trash(parent window) */
}

static void
emfb_forget_passwords(BonoboUIComponent *uid, void *data, const char *path)
{
	e_passwords_forget_passwords();
}

static void
emfb_mail_compose(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderBrowser *emfb = data;
	
	em_utils_compose_new_message ((GtkWidget *) emfb);
}

static void
emfb_mail_stop(BonoboUIComponent *uid, void *data, const char *path)
{
	camel_operation_cancel(NULL);
}

static void
emfb_mail_post(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderView *emfv = data;
	char *url;
	
	url = mail_tools_folder_to_url (emfv->folder);
	em_utils_post_to_url ((GtkWidget *) emfv, url);
	g_free (url);
}

static void
emfb_tools_filters(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderBrowser *emfb = data;
	
	em_utils_edit_filters ((GtkWidget *) emfb);
}

static void
emfb_subscribe_editor_destroy(GtkWidget *w, EMFolderBrowser *emfb)
{
	emfb->priv->subscribe_editor = NULL;
}

static void
emfb_tools_subscriptions(BonoboUIComponent *uid, void *data, const char *path)
{
	EMFolderBrowser *emfb = data;

	if (emfb->priv->subscribe_editor) {
		gdk_window_show(emfb->priv->subscribe_editor->window);
	} else {
		emfb->priv->subscribe_editor = (GtkWidget *)em_subscribe_editor_new();
		e_dialog_set_transient_for((GtkWindow *)emfb->priv->subscribe_editor, (GtkWidget *)emfb);
		g_signal_connect(emfb->priv->subscribe_editor, "destroy", G_CALLBACK(emfb_subscribe_editor_destroy), emfb);
		gtk_widget_show(emfb->priv->subscribe_editor);
	}
}

static void
emfb_tools_vfolders(BonoboUIComponent *uid, void *data, const char *path)
{
	/* FIXME: rename/refactor this */
	vfolder_edit();
}

static BonoboUIVerb emfb_verbs[] = {
	BONOBO_UI_UNSAFE_VERB ("EditInvertSelection", emfb_edit_invert_selection),
	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", emfb_edit_select_all),
        BONOBO_UI_UNSAFE_VERB ("EditSelectThread", emfb_edit_select_thread),
	BONOBO_UI_UNSAFE_VERB ("ChangeFolderProperties", emfb_folder_properties),
	BONOBO_UI_UNSAFE_VERB ("FolderExpunge", emfb_folder_expunge),
	/* HideDeleted is a toggle */
	BONOBO_UI_UNSAFE_VERB ("MessageMarkAllAsRead", emfb_mark_all_read),
	BONOBO_UI_UNSAFE_VERB ("ViewHideRead", emfb_view_hide_read),
	BONOBO_UI_UNSAFE_VERB ("ViewHideSelected", emfb_view_hide_selected),
	BONOBO_UI_UNSAFE_VERB ("ViewShowAll", emfb_view_show_all),
	/* ViewThreaded is a toggle */

	BONOBO_UI_UNSAFE_VERB ("EmptyTrash", emfb_empty_trash),
	BONOBO_UI_UNSAFE_VERB ("ForgetPasswords", emfb_forget_passwords),
	BONOBO_UI_UNSAFE_VERB ("MailCompose", emfb_mail_compose),
	BONOBO_UI_UNSAFE_VERB ("MailPost", emfb_mail_post),
	BONOBO_UI_UNSAFE_VERB ("MailStop", emfb_mail_stop),
	BONOBO_UI_UNSAFE_VERB ("ToolsFilters", emfb_tools_filters),
	BONOBO_UI_UNSAFE_VERB ("ToolsSubscriptions", emfb_tools_subscriptions),
	BONOBO_UI_UNSAFE_VERB ("ToolsVFolders", emfb_tools_vfolders),
	/* ViewPreview is a toggle */

	BONOBO_UI_VERB_END
};

static EPixmap emfb_pixmaps[] = {
	E_PIXMAP ("/commands/ChangeFolderProperties", "configure_16_folder.xpm"),
	E_PIXMAP ("/commands/ViewHideRead", "hide_read_messages.xpm"),
	E_PIXMAP ("/commands/ViewHideSelected", "hide_selected_messages.xpm"),
	E_PIXMAP ("/commands/ViewShowAll", "show_all_messages.xpm"),
	
	E_PIXMAP ("/commands/MailCompose", "new-message.xpm"),

	E_PIXMAP_END
};

static void
emfb_hide_deleted(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	gconf = mail_config_get_gconf_client ();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/show_deleted", state[0] == '0', NULL);
	if (!(emfv->folder && (emfv->folder->folder_flags & CAMEL_FOLDER_IS_TRASH)))
		message_list_set_hidedeleted(emfv->list, state[0] != '0');
}

static void
emfb_view_threaded(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	gconf = mail_config_get_gconf_client ();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/thread_list", state[0] != '0', NULL);

	if (camel_object_meta_set(emfv->folder, "evolution:thread_list", state))
		camel_object_state_write(emfv->folder);

	/* FIXME: do set_threaded via meta-data listener on folder? */
	message_list_set_threaded(emfv->list, state[0] != '0');
	
	/* FIXME: update selection state? */
}

static void
emfb_view_preview(BonoboUIComponent *uic, const char *path, Bonobo_UIComponent_EventType type, const char *state, void *data)
{
	GConfClient *gconf;
	EMFolderView *emfv = data;

	if (type != Bonobo_UIComponent_STATE_CHANGED)
		return;

	gconf = mail_config_get_gconf_client ();
	gconf_client_set_bool(gconf, "/apps/evolution/mail/display/show_preview", state[0] != '0', NULL);

	if (camel_object_meta_set(emfv->folder, "evolution:show_preview", state))
		camel_object_state_write(emfv->folder);

	/* FIXME: do this via folder listener */
	em_folder_browser_show_preview((EMFolderBrowser *)emfv, state[0] != '0');
}

static void
emfb_set_folder(EMFolderView *emfv, CamelFolder *folder, const char *uri)
{
	printf("set folder called\n");

	/* This is required since we get activated the first time
	   before the folder is open and need to override the
	   defaults */
	if (folder) {
		char *sstate;

		if ((sstate = camel_object_meta_get(folder, "evolution:show_preview"))) {
			printf("forcing folder show_preview to '%s'\n", sstate);
			em_folder_browser_show_preview((EMFolderBrowser *)emfv, sstate[0] != '0');
		}

		if ((sstate = camel_object_meta_get(folder, "evolution:thread_list"))) {
			printf("forcing folder thread_list to '%s'\n", sstate);
			message_list_set_threaded(emfv->list, sstate[0] == '1');
		}
	}

	emfb_parent->set_folder(emfv, folder, uri);
}

static void
emfb_activate(EMFolderView *emfv, BonoboUIComponent *uic, int state)
{
	if (state) {
		GConfClient *gconf;
		gboolean state;
		char *sstate;

		gconf = mail_config_get_gconf_client ();

		/* parent loads all ui files via ui_files */
		emfb_parent->activate(emfv, uic, state);

		bonobo_ui_component_add_verb_list_with_data(uic, emfb_verbs, emfv);
		e_pixmaps_update(uic, emfb_pixmaps);

#if 0
		/* FIXME: finish */
		/* (Pre)view pane size (do this first because it affects the
	           preview settings - see folder_browser_set_message_preview()
	           internals for details) */
		g_signal_handler_block(emfb->vpane, emfb->priv->vpane_resize_id);
		gtk_paned_set_position((GtkPaned *)emfb->vpane, gconf_client_get_int (gconf, "/apps/evolution/mail/display/paned_size", NULL));
		g_signal_handler_unblock(emfb->vpane, emfb->priv->vpane_resize_id);
#endif
	
		/* (Pre)view toggle */
		if (emfv->folder
		    && (sstate = camel_object_meta_get(emfv->folder, "evolution:show_preview"))) {
			state = sstate[0] == '1';
			g_free(sstate);
		} else {
			state = gconf_client_get_bool(gconf, "/apps/evolution/mail/display/show_preview", NULL);
		}

		bonobo_ui_component_set_prop(uic, "/commands/ViewPreview", "state", state?"1":"0", NULL);
		em_folder_browser_show_preview((EMFolderBrowser *)emfv, state);
		bonobo_ui_component_add_listener(uic, "ViewPreview", emfb_view_preview, emfv);
	
		/* Stop button */
		state = mail_msg_active((unsigned int)-1);
		bonobo_ui_component_set_prop(uic, "/commands/MailStop", "sensitive", state?"1":"0", NULL);

		/* HideDeleted */
		state = !gconf_client_get_bool(gconf, "/apps/evolution/mail/display/show_deleted", NULL);
		bonobo_ui_component_set_prop(uic, "/commands/HideDeleted", "state", state ? "1" : "0", NULL);
		bonobo_ui_component_add_listener(uic, "HideDeleted", emfb_hide_deleted, emfv);
		if (!(emfv->folder && (emfv->folder->folder_flags & CAMEL_FOLDER_IS_TRASH)))
			message_list_set_hidedeleted (emfv->list, state);
		else
			bonobo_ui_component_set_prop(uic, "/commands/HideDeleted", "sensitive", state?"1":"0", NULL);

		/* FIXME: If we have no folder, we can't do a few of the lookups we need,
		   perhaps we should postpone till we can */

		if (emfv->folder == NULL)
			g_warning("Have activate called before folder is loaded\n");

		/* ViewThreaded */
		if (emfv->folder
		    && (sstate = camel_object_meta_get(emfv->folder, "evolution:thread_list"))) {
			state = sstate[0] == '1';
			g_free(sstate);
		} else {
			state = gconf_client_get_bool(gconf, "/apps/evolution/mail/display/thread_list", NULL);
		}

		bonobo_ui_component_set_prop(uic, "/commands/ViewThreaded", "state", state?"1":"0", NULL);
		bonobo_ui_component_add_listener(uic, "ViewThreaded", emfb_view_threaded, emfv);
		message_list_set_threaded(emfv->list, state);

		/* FIXME: Selection state */

		/* FIXME: property menu customisation */
		/*folder_browser_setup_property_menu (fb, fb->uicomp);*/
	
		/* FIXME: GalView menus?  Also requires a message_list_display_view(ml, GalView) call */
		/*if (fb->view_instance == NULL)
		  folder_browser_ui_setup_view_menus (fb);
		  FIXME: The galview instance should be setup earlier, when we have the folder, when we setup a meta? */
	} else {
		const BonoboUIVerb *v;
		
		for (v = &emfb_verbs[0]; v->cname; v++)
			bonobo_ui_component_remove_verb(uic, v->cname);

		emfb_parent->activate(emfv, uic, state);
	}
}
