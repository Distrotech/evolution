/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * folder-browser.c: Folder browser top level component
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 2000 Helix Code, Inc.
 */
#include <config.h>
#include <ctype.h>
#include <gnome.h>
#include "e-util/e-util.h"
#include "e-util/e-sexp.h"
#include "folder-browser.h"
#include "mail.h"
#include "message-list.h"
#include "mail-threads.h"
#include <widgets/e-paned/e-vpaned.h>

#define PARENT_TYPE (gtk_table_get_type ())

static GtkObjectClass *folder_browser_parent_class;

static void 
real_mail_uri_to_folder (gpointer data);
static void 
cleanup_mail_uri_to_folder (gpointer data);
static void 
load_folder_done (CamelFolder *result, gpointer user_data);

static void
folder_browser_destroy (GtkObject *object)
{
	FolderBrowser *folder_browser = FOLDER_BROWSER (object);

	camel_folder_sync (folder_browser->folder, FALSE, NULL);

	if (folder_browser->shell) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);
		Bonobo_Unknown_unref (folder_browser->shell, &ev);
		CORBA_exception_free (&ev);
	}
	
	if (folder_browser->uri)
		g_free (folder_browser->uri);

	if (folder_browser->folder)
		camel_object_unref (CAMEL_OBJECT (folder_browser->folder));
	
	if (folder_browser->message_list)
		bonobo_object_unref (BONOBO_OBJECT (folder_browser->message_list));

	folder_browser_parent_class->destroy (object);
}

static void
folder_browser_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = folder_browser_destroy;

	folder_browser_parent_class = gtk_type_class (PARENT_TYPE);
}

typedef struct _uri2folder_data {
	const char *name;
	void (*callback) (CamelFolder *, gpointer);
	gpointer user_data;
	CamelFolder *result;
} uri2folder_data;

void
mail_uri_to_folder (const char *name, void (*callback) (CamelFolder *, gpointer), gpointer user_data)
{
	uri2folder_data *ufd;

	ufd = g_new (uri2folder_data, 1);
	ufd->name = name;
	ufd->callback = callback;
	ufd->user_data = user_data;
	ufd->result = NULL;

	mail_operation_queue ("Open folder", real_mail_uri_to_folder, cleanup_mail_uri_to_folder, ufd);
}

CamelFolder *
mail_uri_to_folder_sync (const char *name)
{
	uri2folder_data ufd;

	ufd.name = name;
	ufd.callback = NULL;
	ufd.user_data = NULL;
	ufd.result = NULL;

	real_mail_uri_to_folder (&ufd);
	return ufd.result;
}

static void 
real_mail_uri_to_folder (gpointer data)
{
	uri2folder_data *ufd = (uri2folder_data *) data;
	const char *name;
	char *store_name /*, *msg*/;
	CamelStore *store = NULL;
	CamelFolder *folder = NULL;
	CamelException *ex;

	name = ufd->name;
	ex = camel_exception_new ();

	if (!strncmp (name, "vfolder:", 8)) {
		char *query, *newquery;
		store_name = g_strdup (name);
		query = strchr (store_name, '?');
		if (query) {
			*query++ = 0;
		} else {
			query = "";
		}
		newquery = g_strdup_printf("mbox?%s", query);
		store = camel_session_get_store (session, store_name, ex);

		if (store) {
			folder = camel_store_get_folder (store, newquery, TRUE, ex);
			/* FIXME: do this properly rather than hardcoding */
			/* FIXME: Now we REALLY need to do this properly... */
#warning "Find a way not to hardcode vfolder source"
			{
				char *source_name;
				extern char *evolution_dir;
				uri2folder_data ufd;

				source_name = g_strdup_printf ("file://%s/local/Inbox", evolution_dir);
				ufd.name = source_name;
				ufd.result = NULL;
				real_mail_uri_to_folder (&ufd);
				g_free (source_name);
				if (ufd.result)
					camel_vee_folder_add_folder (folder, ufd.result);
			}
		}
		g_free (newquery);
		g_free (store_name);

	} else if (!strncmp (name, "imap:", 5)) {
		char *service, *ptr;
		
		service = g_strdup_printf ("%s/", name);
		for (ptr = service + 7; *ptr && *ptr != '/'; ptr++);
		ptr++;
		*ptr = '\0';
		store = camel_session_get_store (session, service, ex);
		g_free (service);
		if (store) {
			CamelURL *url = CAMEL_SERVICE (store)->url;
			char *folder_name;

			for (ptr = (char *)(name + 7); *ptr && *ptr != '/'; ptr++);
			if (*ptr == '/') {
				if (url && url->path) {
					ptr += strlen (url->path);
					if (*ptr == '/')
						ptr++;
				}

				if (*ptr == '/')
					ptr++;
				/*for ( ; *ptr && *ptr == '/'; ptr++);*/

				folder_name = g_strdup (ptr);
				
				folder = camel_store_get_folder (store, folder_name, TRUE, ex);
				g_free (folder_name);
			}
		}
	} else if (!strncmp(name, "news:", 5)) {
		store = camel_session_get_store (session, name, ex);
		if (store) {
			const char *folder_name;

			folder_name = name + 5;

			folder = camel_store_get_folder (store, folder_name, FALSE, ex);
		}
	} else if (!strncmp (name, "file:", 5)) {
		/* Change "file:" to "mbox:". */
		store_name = g_strdup_printf ("mbox:%s", name + 5);
		store = camel_session_get_store (session, store_name, ex);
		g_free (store_name);
		if (store) {
			folder = camel_store_get_folder (store, "mbox", FALSE, ex);
		}
	} else {
		/*msg = g_strdup_printf ("Can't open URI %s", name);
		 *gnome_error_dialog (msg);
		 *g_free (msg);
		 */
		mail_op_error ("Can't open URI %s", name);
	}

	if (camel_exception_get_id (ex)) {
		/*msg = g_strdup_printf ("Unable to get folder %s: %s\n", name,
		 *	       camel_exception_get_description (ex));
		 * gnome_error_dialog (msg);
		 */

		mail_op_error ("Unable to get folder %s: %s", name,
			       camel_exception_get_description (ex));

		if (folder) {
			camel_object_unref (CAMEL_OBJECT (folder));
			folder = NULL;
		}
	}
	camel_exception_free (ex);

	if (store)
		camel_object_unref (CAMEL_OBJECT (store));

	ufd->result = folder;
}

static void 
cleanup_mail_uri_to_folder (gpointer data)
{
	uri2folder_data *ufd = (uri2folder_data *) data;

	if (ufd->callback)
		(ufd->callback) (ufd->result, ufd->user_data);
	g_free (ufd);
}

typedef struct load_folder_data_s {
	FolderBrowser *fb;
	void (*callback)( gboolean, gpointer );
	gpointer user_data;
} load_folder_data;

static gboolean
folder_browser_load_folder_sync (FolderBrowser *fb, const char *name)
{
	CamelFolder *new_folder;

	new_folder = mail_uri_to_folder_sync (name);

	if (!new_folder)
		return FALSE;

	if (fb->folder)
		camel_object_unref (CAMEL_OBJECT (fb->folder));
	fb->folder = new_folder;
	message_list_set_folder (fb->message_list, new_folder);
	return TRUE;
}

static void
folder_browser_load_folder (FolderBrowser *fb, const char *name, 
			    void (*callback)( gboolean, gpointer ),
			    gpointer user_data)
{
	load_folder_data *lfd;

	lfd = g_new (load_folder_data, 1);
	lfd->fb = fb;
	lfd->callback = callback;
	lfd->user_data = user_data;

	mail_uri_to_folder (name, load_folder_done, lfd);
}

static void load_folder_done (CamelFolder *result, gpointer user_data)
{
	load_folder_data *lfd = (load_folder_data *) user_data;

	if (!result) {
		if (lfd->callback)
			(lfd->callback) (FALSE, lfd->user_data);
		g_free (lfd);
		return;
	}

	if (lfd->fb->folder)
		camel_object_unref (CAMEL_OBJECT (lfd->fb->folder));
	lfd->fb->folder = result;

	message_list_set_folder (lfd->fb->message_list, result);
	if (lfd->callback)
		(lfd->callback) (TRUE, lfd->user_data);
	g_free (lfd);
}


#define EQUAL(a,b) (strcmp (a,b) == 0)

void
folder_browser_set_uri (FolderBrowser *folder_browser, const char *uri, 
			void (*callback) (gboolean, gpointer),
			gpointer user_data)
{
	if (folder_browser->uri)
		g_free (folder_browser->uri);

	folder_browser->uri = g_strdup (uri);
	folder_browser_load_folder (folder_browser, folder_browser->uri, callback, user_data);
}

gboolean folder_browser_set_uri_sync (FolderBrowser *folder_browser, const char *uri)
{
	if (folder_browser->uri)
		g_free (folder_browser->uri);

	folder_browser->uri = g_strdup (uri);
	return folder_browser_load_folder_sync (folder_browser, folder_browser->uri);
}

void
folder_browser_set_message_preview (FolderBrowser *folder_browser, gboolean show_message_preview)
{
	if (folder_browser->preview_shown == show_message_preview)
		return;

	g_warning ("FIXME: implement me");
}

static char * search_options[] = {
	"Body or subject contains",
	"Body contains",
	"Subject contains",
	"Body does not contain",
	"Subject does not contain",
	NULL
};

/* %s is replaced by the whole search string in quotes ...
   possibly could split the search string into words as well ? */
static char * search_string[] = {
	"(or (body-contains %s) (match-all (header-contains \"Subject\" %s)))",
	"(body-contains %s)",
	"(match-all (header-contains \"Subject\" %s)",
	"(match-all (not (body-contains %s)))",
	"(match-all (not (header-contains \"Subject\" %s)))"
};

static void
search_set(FolderBrowser *fb)
{
	GtkWidget *widget;
	GString *out;
	char *str;
	int index;
	char *text;

	text = gtk_entry_get_text((GtkEntry *)fb->search_entry);

	if (text == NULL || text[0] == 0) {
		message_list_regenerate (fb->message_list, NULL);
		return;
	}

	widget = gtk_menu_get_active (GTK_MENU(GTK_OPTION_MENU(fb->search_menu)->menu));
	index = (int)gtk_object_get_data((GtkObject *)widget, "search_option");
	if (index > sizeof(search_string)/sizeof(search_string[0]))
		index = 0;
	str = search_string[index];

	out = g_string_new("");
	while (*str) {
		if (str[0] == '%' && str[1]=='s') {
			str+=2;
			e_sexp_encode_string(out, text);
		} else {
			g_string_append_c(out, *str);
			str++;
		}
	}
	message_list_regenerate (fb->message_list, out->str);
	g_string_free(out, TRUE);
}

static void
search_menu_deactivate(GtkWidget *menu, FolderBrowser *fb)
{
	search_set(fb);
}

static GtkWidget *
create_option_menu (char **menu_list, int item, void *data)
{
	GtkWidget *omenu;
	GtkWidget *menu;
	int i = 0;
       
	omenu = gtk_option_menu_new ();
	menu = gtk_menu_new ();
	while (*menu_list){
		GtkWidget *entry;

		entry = gtk_menu_item_new_with_label (*menu_list);
		gtk_widget_show (entry);
		gtk_object_set_data((GtkObject *)entry, "search_option", (void *)i);
		gtk_menu_append (GTK_MENU (menu), entry);
		menu_list++;
		i++;
	}
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), menu);
	gtk_option_menu_set_history (GTK_OPTION_MENU (omenu), item);
	gtk_widget_show (omenu);

	gtk_signal_connect (GTK_OBJECT (menu), 
			    "deactivate",
			    GTK_SIGNAL_FUNC (search_menu_deactivate), data);

	return omenu;
}

static void
search_activate(GtkEntry *entry, FolderBrowser *fb)
{
	search_set(fb);
}

void
folder_browser_clear_search (FolderBrowser *fb)
{
	gtk_entry_set_text (GTK_ENTRY (fb->search_entry), "");
	gtk_option_menu_set_history (GTK_OPTION_MENU (fb->search_menu), 0);
	message_list_regenerate (fb->message_list, NULL);
}

static int
etable_key (ETable *table, int row, int col, GdkEvent *ev, FolderBrowser *fb)
{
	if ((ev->key.state & !(GDK_SHIFT_MASK | GDK_LOCK_MASK)) != 0)
		return FALSE;

	if (ev->key.keyval == GDK_space || ev->key.keyval == GDK_BackSpace) {
		GtkAdjustment *vadj;
		gfloat page_size;

		vadj = e_scroll_frame_get_vadjustment (fb->mail_display->scroll);
		page_size = vadj->page_size - vadj->step_increment;

		if (ev->key.keyval == GDK_BackSpace) {
			if (vadj->value > vadj->lower + page_size)
				vadj->value -= page_size;
			else
				vadj->value = vadj->lower;
		} else {
			if (vadj->value < vadj->upper - vadj->page_size - page_size)
				vadj->value += page_size;
			else
				vadj->value = vadj->upper - vadj->page_size;
		}

		gtk_adjustment_value_changed (vadj);
		return TRUE;
	} else if (ev->key.keyval == GDK_Delete ||
		   ev->key.keyval == GDK_KP_Delete) {
		delete_msg (NULL, fb);
		message_list_select (fb->message_list, row,
				     MESSAGE_LIST_SELECT_NEXT,
				     0, CAMEL_MESSAGE_DELETED);
		return TRUE;
	} else if (ev->key.keyval == 'n' || ev->key.keyval == 'N' ||
		   ev->key.keyval == 'p' || ev->key.keyval == 'P') {
		message_list_select (fb->message_list, row,
				     tolower (ev->key.keyval) == 'p' ?
				     MESSAGE_LIST_SELECT_PREVIOUS :
				     MESSAGE_LIST_SELECT_NEXT,
				     0, CAMEL_MESSAGE_SEEN);
	}

	return FALSE;
}

static void
folder_browser_gui_init (FolderBrowser *fb)
{
	GtkWidget *hbox, *label;

	/*
	 * The panned container
	 */
	fb->vpaned = e_vpaned_new ();
	gtk_widget_show (fb->vpaned);

	gtk_table_attach (
		GTK_TABLE (fb), fb->vpaned,
		0, 1, 1, 3,
		GTK_FILL | GTK_EXPAND,
		GTK_FILL | GTK_EXPAND,
		0, 0);

	/* quick-search entry */
	hbox = gtk_hbox_new(FALSE, 3);
	gtk_widget_show(hbox);
	fb->search_entry = gtk_entry_new();
	gtk_widget_show(fb->search_entry);
	gtk_signal_connect(GTK_OBJECT (fb->search_entry), "activate", search_activate, fb);
	/* gtk_signal_connect(fb->search_entry, "changed", search_activate, fb); */
	label = gtk_label_new("Search");
	gtk_widget_show(label);
	fb->search_menu = create_option_menu(search_options, 0, fb);
	gtk_box_pack_end((GtkBox *)hbox, fb->search_entry, FALSE, FALSE, 3);
	gtk_box_pack_end((GtkBox *)hbox, fb->search_menu, FALSE, FALSE, 3);
	gtk_box_pack_end((GtkBox *)hbox, label, FALSE, FALSE, 3);
	gtk_table_attach (
		GTK_TABLE (fb), hbox,
		0, 1, 0, 1,
		GTK_FILL | GTK_EXPAND,
		0,
		0, 0);

	fb->message_list_w = message_list_get_widget (fb->message_list);
	e_paned_add1 (E_PANED (fb->vpaned), fb->message_list_w);
	gtk_widget_show (fb->message_list_w);

	e_paned_add2 (E_PANED (fb->vpaned), GTK_WIDGET (fb->mail_display));
	e_paned_set_position (E_PANED (fb->vpaned), 200);

	gtk_widget_show (GTK_WIDGET (fb->mail_display));
	gtk_widget_show (GTK_WIDGET (fb));

}

static void
folder_browser_init (GtkObject *object)
{
}

static void
my_folder_browser_init (GtkObject *object)
{
	FolderBrowser *fb = FOLDER_BROWSER (object);

	/*
	 * Setup parent class fields.
	 */ 
	GTK_TABLE (fb)->homogeneous = FALSE;
	gtk_table_resize (GTK_TABLE (fb), 1, 2);

	/*
	 * Our instance data
	 */
	fb->message_list = MESSAGE_LIST (message_list_new (fb));
	fb->mail_display = MAIL_DISPLAY (mail_display_new (fb));

	gtk_signal_connect (GTK_OBJECT (fb->message_list->etable),
			    "key_press", GTK_SIGNAL_FUNC (etable_key), fb);

	folder_browser_gui_init (fb);
}

GtkWidget *
folder_browser_new (void)
{
	static int serial;
	FolderBrowser *folder_browser = gtk_type_new (folder_browser_get_type ());

	my_folder_browser_init (GTK_OBJECT (folder_browser));
	folder_browser->uri = NULL;
	folder_browser->serial = serial++;

	return GTK_WIDGET (folder_browser);
}


E_MAKE_TYPE (folder_browser, "FolderBrowser", FolderBrowser, folder_browser_class_init, folder_browser_init, PARENT_TYPE);




