/*
  Copyright 2000 Helix Code Inc.

  Author: Michael Zucchi <notzed@helixcode.com>

  code for managing vfolders

  NOTE: dont run this through fucking indent.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <bonobo.h>

#include "Evolution.h"
#include "evolution-storage.h"

#include "evolution-shell-component.h"
#include "folder-browser.h"
#include "mail-vfolder.h"
#include "mail-tools.h"
#include "mail-autofilter.h"

#include "camel/camel.h"

#include "filter/vfolder-context.h"
#include "filter/vfolder-editor.h"

#define d(x) x

struct _vfolder_info {
	char *name;
	char *query;
};

/* list of vfolders available */
static GList *available_vfolders = NULL;
static VfolderContext *context;
static EvolutionStorage *vfolder_storage;

/* GROSS HACK: for passing to other parts of the program */
EvolutionShellClient *global_shell_client = NULL;

/* more globals ... */
extern char *evolution_dir;
extern CamelSession *session;

static struct _vfolder_info *
vfolder_find(const char *name)
{
	GList *l = available_vfolders;
	struct _vfolder_info *info;

	while (l) {
		info = l->data;
		if (!strcmp(info->name, name))
			return info;
		l = g_list_next(l);
	}
	return NULL;
}

/* go through the list of what we have, what we want, and make
   them match, deleting/reconfiguring as required */
static void
vfolder_refresh(void)
{
	GList *l;
	GList *head = NULL;	/* processed list */
	struct _vfolder_info *info;
	FilterRule *rule;
	GString *expr = g_string_new("");
	char *uri, *path;

	rule = NULL;
	while ( (rule = rule_context_next_rule((RuleContext *)context, rule, NULL)) ) {
		info = vfolder_find(rule->name);
		g_string_truncate(expr, 0);
		filter_rule_build_code(rule, expr);
		if (info) {
			available_vfolders = g_list_remove(available_vfolders, info);

			/* check if the rule has changed ... otherwise, leave it */
			if (strcmp(expr->str, info->query)) {
				d(printf("Must reconfigure vfolder with new rule?\n"));
				g_free(info->query);
				info->query = g_strdup(expr->str);

				uri = g_strdup_printf("vfolder:%s", info->name);
				path = g_strdup_printf("/%s", info->name);
				evolution_storage_removed_folder(vfolder_storage, path);
				evolution_storage_new_folder(vfolder_storage, path, g_basename(path),
							     "mail", uri, info->name, FALSE);
				g_free(uri);
				g_free(path);
			}
		} else {
			info = g_malloc(sizeof(*info));
			info->name = g_strdup(rule->name);
			info->query = g_strdup(expr->str);
			d(printf("Adding new vfolder: %s %s\n", rule->name, expr->str));
			
			uri = g_strdup_printf("vfolder:%s", info->name);
			path = g_strdup_printf("/%s", info->name);
			evolution_storage_new_folder(vfolder_storage, path, g_basename(path),
						     "mail", uri, info->name, FALSE);
			g_free(uri);
			g_free(path);
		}
		head = g_list_append(head, info);
	}
	/* everything in available_vfolders are to be removed ... */
	l = available_vfolders;
	while (l) {
		info = l->data;
		d(printf("removing vfolders %s %s\n", info->name, info->query));
		path = g_strdup_printf("/%s", info->name);
		evolution_storage_removed_folder(vfolder_storage, path);
		g_free(path);
		g_free(info->name);
		g_free(info->query);
		l = g_list_next(l);
	}
	g_list_free(available_vfolders);
	available_vfolders = head;
	g_string_free(expr, TRUE);
}

void
vfolder_create_storage(EvolutionShellComponent *shell_component)
{
	EvolutionShellClient *shell_client;
	GNOME_Evolution_Shell corba_shell;
	EvolutionStorage *storage;
	char *user, *system;

	shell_client = evolution_shell_component_get_owner (shell_component);
	if (shell_client == NULL) {
		g_warning ("We have no shell!?");
		return;
	}
	global_shell_client = shell_client;

	corba_shell = bonobo_object_corba_objref (BONOBO_OBJECT (shell_client));
    
	storage = evolution_storage_new (_("VFolders"), NULL);
	if (evolution_storage_register_on_shell (storage, corba_shell) != EVOLUTION_STORAGE_OK) {
		g_warning ("Cannot register storage");
		return;
	}

	vfolder_storage = storage;

	user = g_strdup_printf ("%s/vfolders.xml", evolution_dir);
	system = g_strdup_printf("%s/evolution/vfoldertypes.xml", EVOLUTION_DATADIR);
	
	context = vfolder_context_new();
	printf("loading rules %s %s\n", system, user);
	if (rule_context_load((RuleContext *)context, system, user) != 0) {
		g_warning("cannot load vfolders: %s\n", ((RuleContext *)context)->error);
	}
	g_free(user);
	g_free(system);
	vfolder_refresh();
}

/* maps the shell's uri to the real vfolder uri and open the folder */
CamelFolder *
vfolder_uri_to_folder(const char *uri, CamelException *ex)
{
	void camel_vee_folder_add_folder(CamelFolder *, CamelFolder *);

	struct _vfolder_info *info;
	char *storeuri, *foldername;
	VfolderRule *rule;
	CamelFolder *folder = NULL, *sourcefolder;
	const char *sourceuri;
	int sources;

	if (strncmp (uri, "vfolder:", 8))
		return NULL;

	info = vfolder_find(uri+8);
	if (info == NULL) {
		g_warning("Shell trying to open unknown vFolder: %s", uri);
		return NULL;
	}

	d(printf("Opening vfolder: %s\n", uri));

	rule = (VfolderRule *)rule_context_find_rule((RuleContext *)context, info->name, NULL);

	storeuri = g_strdup_printf("vfolder:%s/vfolder/%s", evolution_dir, info->name);
	foldername = g_strdup_printf("mbox?%s", info->query);

	/* we dont have indexing on vfolders */
	folder = mail_tool_get_folder_from_urlname (storeuri, foldername, CAMEL_STORE_FOLDER_CREATE, ex);

	sourceuri = NULL;
	sources = 0;
	while ( (sourceuri = vfolder_rule_next_source(rule, sourceuri)) ) {
		d(printf("adding vfolder source: %s\n", sourceuri));
		sourcefolder = mail_tool_uri_to_folder (sourceuri, ex);
		printf("source folder = %p\n", sourcefolder);
		if (sourcefolder) {
			sources++;
			mail_tool_camel_lock_up ();
			camel_vee_folder_add_folder(folder, sourcefolder);
			mail_tool_camel_lock_down ();
		} else {
			/* we'll just silently ignore now-missing sources */
			camel_exception_clear(ex);
		}
	}
	/* if we didn't have any sources, just use Inbox as the default */
	if (sources == 0) {
		char *defaulturi;

		defaulturi = g_strdup_printf("file://%s/local/Inbox", evolution_dir);
		d(printf("No sources configured/found, using default: %s\n", defaulturi));
		sourcefolder = mail_tool_uri_to_folder (defaulturi, ex);
		g_free(defaulturi);
		if (sourcefolder) {
			mail_tool_camel_lock_up ();
			camel_vee_folder_add_folder(folder, sourcefolder);
			mail_tool_camel_lock_down ();
		}
	}

	g_free(foldername);
	g_free(storeuri);

	return folder;
}

static void
vfolder_editor_clicked(GtkWidget *w, int button, void *data)
{
	if (button == 0) {
		char *user;

		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
		vfolder_refresh();
	}
	if (button != -1) {
		gnome_dialog_close((GnomeDialog *)w);
	}
}

void
vfolder_edit(void)
{
	GtkWidget *w;

	w = vfolder_editor_construct(context);
	gtk_signal_connect((GtkObject *)w, "clicked", vfolder_editor_clicked, NULL);
	gtk_widget_show(w);
}

static void
new_rule_clicked(GtkWidget *w, int button, void *data)
{
	if (button == 0) {
		char *user;
		FilterRule *rule = gtk_object_get_data((GtkObject *)w, "rule");

		gtk_object_ref((GtkObject *)rule);
		rule_context_add_rule((RuleContext *)context, rule);
		user = g_strdup_printf("%s/vfolders.xml", evolution_dir);
		rule_context_save((RuleContext *)context, user);
		g_free(user);
		vfolder_refresh();
	}
	if (button != -1) {
		gnome_dialog_close((GnomeDialog *)w);
	}
}

FilterPart *
vfolder_create_part(const char *name)
{
	return rule_context_create_part((RuleContext *)context, name);
}

/* adds a rule with a gui */
void
vfolder_gui_add_rule(VfolderRule *rule)
{
	GtkWidget *w;
	GnomeDialog *gd;

	w = filter_rule_get_widget((FilterRule *)rule, (RuleContext *)context);
	gd = (GnomeDialog *)gnome_dialog_new(_("New VFolder"),
					     GNOME_STOCK_BUTTON_OK,
					     GNOME_STOCK_BUTTON_CANCEL,
					     NULL);
	gtk_window_set_policy(GTK_WINDOW(gd), FALSE, TRUE, FALSE);
	gtk_box_pack_start((GtkBox *)gd->vbox, w, FALSE, TRUE, 0);
	gtk_widget_show((GtkWidget *)gd);
	gtk_object_set_data_full((GtkObject *)gd, "rule", rule, (GtkDestroyNotify)gtk_object_unref);
	gtk_signal_connect((GtkObject *)gd, "clicked", new_rule_clicked, NULL);
	gtk_widget_show((GtkWidget *)gd);
}

void
vfolder_gui_add_from_message(CamelMimeMessage *msg, int flags, const char *source)
{
	VfolderRule *rule;

	rule = (VfolderRule*)vfolder_rule_from_message(context, msg, flags, source);
	vfolder_gui_add_rule(rule);
}

