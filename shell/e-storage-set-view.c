/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage-set-view.c
 *
 * Copyright (C) 2000, 2001, 2002 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli
 * Etree-ification: Chris Toshok
 * GtkTree-ification: Mike Kestner
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-storage-set-view.h"
#include "e-storage-set-store.h"

#include "e-util/e-gtk-utils.h"

#include "e-corba-storage.h"
#include "e-folder-dnd-bridge.h"
#include "e-shell-constants.h"
#include "e-shell-marshal.h"

#include <gal/util/e-util.h>
#include <gal/widgets/e-gui-utils.h>

#include <libgnome/gnome-util.h>
#include <libgnomeui/gnome-popup-menu.h>

#include <libxml/tree.h>

#include <bonobo/bonobo-window.h>
#include <bonobo/bonobo-ui-util.h>

#include <gtk/gtkcellrenderer.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>

/* This is used on the source side to define the two basic types that we always
   export.  */
enum _DndTargetTypeIdx {
	E_FOLDER_DND_PATH_TARGET_TYPE_IDX = 0,
	E_SHORTCUT_TARGET_TYPE_IDX = 1
};
typedef enum _DndTargetTypeIdx DndTargetTypeIdx;

#define E_SHORTCUT_TARGET_TYPE     "E-SHORTCUT"


#define PARENT_TYPE GTK_TYPE_TREE_VIEW

static GtkTreeViewClass *parent_class = NULL;

struct _EStorageSetViewPrivate {
	EStorageSet *storage_set;

	BonoboUIComponent *ui_component;
	BonoboUIContainer *ui_container;

	EStorageSetStore *store;

	/* Path of the row selected by a right click.  */
	char *right_click_row_path;

	unsigned int show_folders : 1;
	unsigned int show_checkboxes : 1;
	unsigned int allow_dnd : 1;
	unsigned int search_enabled : 1;

	/* The `Evolution::ShellComponentDnd::SourceFolder' interface for the
	   folder we are dragging from, or CORBA_OBJECT_NIL if no dragging is
	   happening.  */
	GNOME_Evolution_ShellComponentDnd_SourceFolder drag_corba_source_interface;

	/* Source context information.  NULL if no dragging is in progress.  */
	GNOME_Evolution_ShellComponentDnd_SourceFolder_Context *drag_corba_source_context;

	/* The data.  */
	GNOME_Evolution_ShellComponentDnd_Data *drag_corba_data;

	GtkTreeViewColumn *checkbox_column;
};


enum {
	FOLDER_SELECTED,
	FOLDER_OPENED,
	DND_ACTION,
	FOLDER_CONTEXT_MENU_POPPING_UP,
	FOLDER_CONTEXT_MENU_POPPED_DOWN,
	CHECKBOXES_CHANGED,
	LAST_SIGNAL
};

static unsigned int signals[LAST_SIGNAL] = { 0 };


/* Forward declarations.  */

static void setup_folder_changed_callbacks (EStorageSetView *storage_set_view,
					    EFolder *folder,
					    const char *path);


/* DND stuff.  */

enum _DndTargetType {
	DND_TARGET_TYPE_URI_LIST,
	DND_TARGET_TYPE_E_SHORTCUT
};
typedef enum _DndTargetType DndTargetType;

#define URI_LIST_TYPE   "text/uri-list"
#define E_SHORTCUT_TYPE "E-SHORTCUT"


#if 0
static EFolder *
get_folder_at_node (EStorageSetView *storage_set_view,
		    ETreePath path)
{
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	priv = storage_set_view->priv;

	if (path == NULL)
		return NULL;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);
	g_assert (folder_path != NULL);

	return e_storage_set_get_folder (priv->storage_set, folder_path);
}

static EvolutionShellComponentClient *
get_component_at_node (EStorageSetView *storage_set_view,
		       ETreePath path)
{
	EStorageSetViewPrivate *priv;
	EvolutionShellComponentClient *component_client;
	EFolderTypeRegistry *folder_type_registry;
	EFolder *folder;

	priv = storage_set_view->priv;

	folder = get_folder_at_node (storage_set_view, path);
	if (folder == NULL)
		return NULL;

	folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);
	g_assert (folder_type_registry != NULL);

	component_client = e_folder_type_registry_get_handler_for_type (folder_type_registry,
									e_folder_get_type_string (folder));

	return component_client;
}

static GNOME_Evolution_ShellComponentDnd_ActionSet
convert_gdk_drag_action_set_to_corba (GdkDragAction action)
{
	GNOME_Evolution_ShellComponentDnd_Action retval;

	retval = GNOME_Evolution_ShellComponentDnd_ACTION_DEFAULT;

	if (action & GDK_ACTION_COPY)
		retval |= GNOME_Evolution_ShellComponentDnd_ACTION_COPY;
	if (action & GDK_ACTION_MOVE)
		retval |= GNOME_Evolution_ShellComponentDnd_ACTION_MOVE;
	if (action & GDK_ACTION_LINK)
		retval |= GNOME_Evolution_ShellComponentDnd_ACTION_LINK;
	if (action & GDK_ACTION_ASK)
		retval |= GNOME_Evolution_ShellComponentDnd_ACTION_ASK;

	return retval;
}

#endif

/* The weakref callback for priv->ui_component.  */

static void
ui_container_destroy_notify (void *data,
			     GObject *where_the_object_was)
{
	EStorageSetViewPrivate *priv  = (EStorageSetViewPrivate *) data;

	priv->ui_container = NULL;
}	


#if 0
/* DnD selection setup stuff.  */

/* This will create an array of GtkTargetEntries from the specified list of DND
   types.  The type name will *not* be allocated in the list, as this is
   supposed to be used only temporarily to set up the cell as a drag source.  */
static GtkTargetEntry *
create_target_entries_from_dnd_type_list (GList *dnd_types,
					  int *num_entries_return)
{
	GtkTargetEntry *entries;
	GList *p;
	int num_entries;
	int i;

	if (dnd_types == NULL)
		num_entries = 0;
	else
		num_entries = g_list_length (dnd_types);

	/* We always add two entries, one for an Evolution URI type, and one
	   for e-shortcuts.  This will let us do drag & drop within Evolution
	   at least.  */
	num_entries += 2;

	entries = g_new (GtkTargetEntry, num_entries);

	i = 0;

	/* The Evolution URI will always come first.  */
	entries[i].target = E_FOLDER_DND_PATH_TARGET_TYPE;
	entries[i].flags = 0;
	entries[i].info = i;
	g_assert (i == E_FOLDER_DND_PATH_TARGET_TYPE_IDX);
	i ++;

	/* ...Then the shortcut type.  */
	entries[i].target = E_SHORTCUT_TARGET_TYPE;
	entries[i].flags = 0;
	entries[i].info = i;
	g_assert (i == E_SHORTCUT_TARGET_TYPE_IDX);
	i ++;

	for (p = dnd_types; p != NULL; p = p->next, i++) {
		const char *dnd_type;

		g_assert (i < num_entries);

		dnd_type = (const char *) p->data;

		entries[i].target = (char *) dnd_type;
		entries[i].flags  = 0;
		entries[i].info   = i;
	}

	*num_entries_return = num_entries;
	return entries;
}

static void
free_target_entries (GtkTargetEntry *entries)
{
	g_assert (entries != NULL);

	/* The target names are not strdup()ed so a simple free will do.  */
	g_free (entries);
}

static GtkTargetList *
create_target_list_for_node (EStorageSetView *storage_set_view,
			     ETreePath node)
{
	EStorageSetViewPrivate *priv;
	GtkTargetList *target_list;
	EFolderTypeRegistry *folder_type_registry;
	GList *exported_dnd_types;
	GtkTargetEntry *target_entries;
	EFolder *folder;
	const char *folder_type;
	int num_target_entries;

	priv = storage_set_view->priv;

	folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);

	folder = get_folder_at_node (storage_set_view, node);
	folder_type = e_folder_get_type_string (folder);

	exported_dnd_types = e_folder_type_registry_get_exported_dnd_types_for_type (folder_type_registry,
										     folder_type);

	target_entries = create_target_entries_from_dnd_type_list (exported_dnd_types,
								   &num_target_entries);
	g_assert (target_entries != NULL);

	target_list = gtk_target_list_new (target_entries, num_target_entries);

	free_target_entries (target_entries);

	return target_list;
}

static void
set_e_shortcut_selection (EStorageSetView *storage_set_view,
			  GtkSelectionData *selection_data)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;
	EFolder *folder;
	int shortcut_len;
	char *shortcut;
	const char *name;
	const char *folder_path;

	g_assert (storage_set_view != NULL);
	g_assert (selection_data != NULL);

	priv = storage_set_view->priv;

	node = lookup_node_in_hash (storage_set_view, priv->selected_row_path);

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), node);
	g_assert (folder_path != NULL);

	folder = e_storage_set_get_folder (priv->storage_set, folder_path);
	if (folder != NULL)
		name = e_folder_get_name (folder);
	else
		name = NULL;

	/* FIXME: Get `evolution:' from somewhere instead of hardcoding it here.  */

	if (name != NULL)
		shortcut_len = strlen (name);
	else
		shortcut_len = 0;
	
	shortcut_len ++;	/* Separating zero.  */

	shortcut_len += strlen ("evolution:");
	shortcut_len += strlen (priv->selected_row_path);
	shortcut_len ++;	/* Trailing zero.  */

	shortcut = g_malloc (shortcut_len);

	if (name == NULL)
		sprintf (shortcut, "%cevolution:%s", '\0', priv->selected_row_path);
	else
		sprintf (shortcut, "%s%cevolution:%s", name, '\0', priv->selected_row_path);

	gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *) shortcut, shortcut_len);

	g_free (shortcut);
}

static void
set_evolution_path_selection (EStorageSetView *storage_set_view,
			      GtkSelectionData *selection_data)
{
	EStorageSetViewPrivate *priv;

	g_assert (storage_set_view != NULL);
	g_assert (selection_data != NULL);

	priv = storage_set_view->priv;

	gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *) priv->selected_row_path, strlen (priv->selected_row_path) + 1);
}

#endif

/* Folder context menu.  */

struct _FolderPropertyItemsData {
	EStorageSetView *storage_set_view;
	ECorbaStorage *corba_storage;
	int num_items;
};
typedef struct _FolderPropertyItemsData FolderPropertyItemsData;

static void
folder_property_item_verb_callback    (BonoboUIComponent *component,
				       void *user_data,
				       const char *cname)
{
	FolderPropertyItemsData *data;
	GtkWidget *toplevel_widget;
	const char *p, *path;
	int item_number;

	data = (FolderPropertyItemsData *) user_data;

	p = strrchr (cname, ':');
	g_assert (p != NULL);

	item_number = atoi (p + 1) - 1;
	g_assert (item_number >= 0);

	toplevel_widget = gtk_widget_get_toplevel (GTK_WIDGET (data->storage_set_view));

	path = strchr (data->storage_set_view->priv->right_click_row_path + 1, E_PATH_SEPARATOR);
	if (path == NULL)
		path = "/";
	e_corba_storage_show_folder_properties (data->corba_storage, path,
						item_number, toplevel_widget->window);
}

static FolderPropertyItemsData *
setup_folder_properties_items_if_corba_storage_clicked (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;
	EStorage *storage;
	GSList *items, *p;
	GString *xml;
	FolderPropertyItemsData *data;
	const char *slash;
	char *storage_name;
	int num_property_items;
	int i;

	priv = storage_set_view->priv;

	slash = strchr (priv->right_click_row_path + 1, E_PATH_SEPARATOR);
	if (slash == NULL)
		storage_name = g_strdup (priv->right_click_row_path + 1);

	else
		storage_name = g_strndup (priv->right_click_row_path + 1,
					  slash - (priv->right_click_row_path + 1));

	storage = e_storage_set_get_storage (priv->storage_set, storage_name);
	g_free (storage_name);

	if (storage == NULL || ! E_IS_CORBA_STORAGE (storage))
		return 0;

	items = e_corba_storage_get_folder_property_items (E_CORBA_STORAGE (storage));
	if (items == NULL)
		return 0;

	xml = g_string_new ("<placeholder name=\"StorageFolderPropertiesPlaceholder\">");
	g_string_append (xml, "<separator f=\"\" name=\"EStorageSetViewFolderPropertiesSeparator\"/>");

	num_property_items = 0;
	for (p = items; p != NULL; p = p->next) {
		const ECorbaStoragePropertyItem *item;
		char *encoded_label;
		char *encoded_tooltip;

		item = (const ECorbaStoragePropertyItem *) p->data;
		num_property_items ++;

		g_string_sprintfa (xml, "<menuitem name=\"EStorageSetView:FolderPropertyItem:%d\"",
				   num_property_items);
		g_string_sprintfa (xml, " verb=\"EStorageSetView:FolderPropertyItem:%d\"",
				   num_property_items);

		encoded_tooltip = bonobo_ui_util_encode_str (item->tooltip);
		g_string_sprintfa (xml, " tip=\"%s\"", encoded_tooltip);

		encoded_label = bonobo_ui_util_encode_str (item->label);
		g_string_sprintfa (xml, " label=\"%s\"/>", encoded_label);

		g_free (encoded_tooltip);
		g_free (encoded_label);
	}

	g_string_append (xml, "</placeholder>");

	data = g_new (FolderPropertyItemsData, 1);
	data->storage_set_view = storage_set_view;
	data->corba_storage    = E_CORBA_STORAGE (storage);
	data->num_items        = num_property_items;

	g_object_ref (data->storage_set_view);
	g_object_ref (data->corba_storage);

	for (i = 1; i <= num_property_items; i ++) {
		char *verb;

		verb = g_strdup_printf ("EStorageSetView:FolderPropertyItem:%d", i);
		bonobo_ui_component_add_verb (priv->ui_component, verb,
					      folder_property_item_verb_callback,
					      data);
	}

	bonobo_ui_component_set (priv->ui_component, "/popups/FolderPopup", xml->str, NULL);

	g_string_free (xml, TRUE);
	e_corba_storage_free_property_items_list (items);

	return data;
}

static void
remove_property_items (EStorageSetView *storage_set_view,
		       FolderPropertyItemsData *data)
{
	EStorageSetViewPrivate *priv;

	priv = storage_set_view->priv;

	if (data->num_items > 0) {
		int i;

		bonobo_ui_component_rm (priv->ui_component, 
					"/popups/FolderPopup/StorageFolderPropertiesPlaceholder/EStorageSetViewFolderPropertiesSeparator",
					NULL);

		for (i = 1; i <= data->num_items; i ++) {
			char *path;
			char *verb;

			path = g_strdup_printf ("/popups/FolderPopup/StorageFolderPropertiesPlaceholder/EStorageSetView:FolderPropertyItem:%d", i);
			bonobo_ui_component_rm (priv->ui_component, path, NULL);
			g_free (path);

			verb = g_strdup_printf ("EStorageSetView:FolderPropertyItem:%d", i);
			bonobo_ui_component_remove_verb (priv->ui_component, verb);
			g_free (verb);
		}
	}

	g_object_unref (data->storage_set_view);
	g_object_unref (data->corba_storage);

	g_free (data);
}

static void
popup_folder_menu (EStorageSetView *storage_set_view,
		   GdkEventButton *event)
{
	EvolutionShellComponentClient *handler;
	EStorageSetViewPrivate *priv;
	EFolderTypeRegistry *folder_type_registry;
	EFolder *folder;
	GtkWidget *menu;
	FolderPropertyItemsData *folder_property_items_data;

	priv = storage_set_view->priv;

	folder = e_storage_set_get_folder (priv->storage_set, priv->right_click_row_path);
	g_object_ref (folder);

	folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);
	g_assert (folder_type_registry != NULL);

	handler = e_folder_type_registry_get_handler_for_type (folder_type_registry,
							       e_folder_get_type_string (folder));
	menu = gtk_menu_new ();

#if 0
	bonobo_window_add_popup (bonobo_ui_container_get_win (priv->ui_container),
				 GTK_MENU (menu), "/popups/FolderPopup");
#endif

	bonobo_ui_component_set (priv->ui_component,
				 "/popups/FolderPopup/ComponentPlaceholder",
				 "<placeholder name=\"Items\"/>", NULL);

	if (handler != NULL)
		evolution_shell_component_client_populate_folder_context_menu (handler,
									       priv->ui_container,
									       e_folder_get_physical_uri (folder),
									       e_folder_get_type_string (folder));

	folder_property_items_data = setup_folder_properties_items_if_corba_storage_clicked (storage_set_view);

	gtk_widget_show (GTK_WIDGET (menu));

	gnome_popup_menu_do_popup_modal (GTK_WIDGET (menu), NULL, NULL, event, NULL,
					 GTK_WIDGET (storage_set_view));

	if (folder_property_items_data != NULL)
		remove_property_items (storage_set_view, folder_property_items_data);

	if (handler != NULL)
		evolution_shell_component_client_unpopulate_folder_context_menu (handler,
										 priv->ui_container,
										 e_folder_get_physical_uri (folder),
										 e_folder_get_type_string (folder));

	g_object_unref (folder);
	gtk_widget_destroy (GTK_WIDGET (menu));

#if 0
	e_tree_right_click_up (E_TREE (storage_set_view));
#endif
}


/* GtkObject methods.  */

static void
impl_dispose (GObject *object)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (object);
	priv = storage_set_view->priv;

	if (priv->store != NULL) {
		g_object_unref (priv->store);
		priv->store = NULL;
	}

	if (priv->storage_set != NULL) {
		g_object_unref (priv->storage_set);
		priv->storage_set = NULL;
	}

	if (priv->drag_corba_source_interface != CORBA_OBJECT_NIL) {
		CORBA_Environment ev;

		CORBA_exception_init (&ev);

		g_assert (priv->drag_corba_source_context != NULL);

		GNOME_Evolution_ShellComponentDnd_SourceFolder_endDrag (priv->drag_corba_source_interface,
									priv->drag_corba_source_context,
									&ev);

		Bonobo_Unknown_unref (priv->drag_corba_source_interface, &ev);
		CORBA_Object_release (priv->drag_corba_source_interface, &ev);

		CORBA_exception_free (&ev);

		priv->drag_corba_source_interface = CORBA_OBJECT_NIL;
	}

	if (priv->ui_component != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (priv->ui_component));
		priv->ui_component = NULL;
	}

	/* (No unreffing for priv->ui_container since we use a weakref.)  */

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (object);
	priv = storage_set_view->priv;

	if (priv->drag_corba_source_context != NULL)
		CORBA_free (priv->drag_corba_source_context);

	if (priv->drag_corba_data != NULL)
		CORBA_free (priv->drag_corba_data);

	g_free (priv->right_click_row_path);

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

#if 0
/* -- Source-side DnD.  */

static gint
impl_tree_start_drag (ETree *tree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkEvent *event)
{
	GdkDragContext *context;
	GtkTargetList *target_list;
	GdkDragAction actions;
	EStorageSetView *storage_set_view;

	storage_set_view = E_STORAGE_SET_VIEW (tree);

	if (! storage_set_view->priv->allow_dnd)
		return FALSE;

	target_list = create_target_list_for_node (storage_set_view, path);
	if (target_list == NULL)
		return FALSE;

	actions = GDK_ACTION_MOVE | GDK_ACTION_COPY;

	context = e_tree_drag_begin (tree, row, col,
				     target_list,
				     actions,
				     1, event);

	gtk_drag_set_icon_default (context);

	gtk_target_list_unref (target_list);

	return TRUE;
}

static void
impl_tree_drag_begin (ETree *etree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	EFolder *folder;
	EvolutionShellComponentClient *component_client;
	GNOME_Evolution_ShellComponent corba_component;
	GNOME_Evolution_ShellComponentDnd_ActionSet possible_actions;
	GNOME_Evolution_ShellComponentDnd_Action suggested_action;
	CORBA_Environment ev;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	g_free (priv->selected_row_path);
	priv->selected_row_path = g_strdup (e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path));

	g_assert (priv->drag_corba_source_interface == CORBA_OBJECT_NIL);

	folder = get_folder_at_node (storage_set_view, path);
	component_client = get_component_at_node (storage_set_view, path);

	if (component_client == NULL)
		return;

	/* Query the `ShellComponentDnd::SourceFolder' interface on the
	   component.  */
	/* FIXME we could use the new
	   `evolution_shell_component_client_get_dnd_source_interface()'
	   call. */

	CORBA_exception_init (&ev);

	corba_component = evolution_shell_component_client_corba_objref (component_client);
	priv->drag_corba_source_interface = Bonobo_Unknown_queryInterface (corba_component,
									   "IDL:GNOME/Evolution/ShellComponentDnd/SourceFolder:1.0",
									   &ev);
	if (ev._major != CORBA_NO_EXCEPTION
	    || priv->drag_corba_source_interface == CORBA_OBJECT_NIL) {
		priv->drag_corba_source_interface = CORBA_OBJECT_NIL;

		CORBA_exception_free (&ev);
		return;
	}

	GNOME_Evolution_ShellComponentDnd_SourceFolder_beginDrag (priv->drag_corba_source_interface,
								  e_folder_get_physical_uri (folder),
								  e_folder_get_type_string (folder),
								  &possible_actions,
								  &suggested_action,
								  &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		Bonobo_Unknown_unref (priv->drag_corba_source_interface, &ev);
		CORBA_Object_release (priv->drag_corba_source_interface, &ev);

		priv->drag_corba_source_interface = CORBA_OBJECT_NIL;

		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);

	if (priv->drag_corba_source_context != NULL)
		CORBA_free (priv->drag_corba_source_context);

	priv->drag_corba_source_context = GNOME_Evolution_ShellComponentDnd_SourceFolder_Context__alloc ();
	priv->drag_corba_source_context->physicalUri     = CORBA_string_dup (e_folder_get_physical_uri (folder));
	priv->drag_corba_source_context->folderType      = CORBA_string_dup (e_folder_get_type_string (folder));
	priv->drag_corba_source_context->possibleActions = possible_actions;
	priv->drag_corba_source_context->suggestedAction = suggested_action;
}

static void
impl_tree_drag_end (ETree *tree,
		    int row,
		    ETreePath path,
		    int col,
		    GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	CORBA_Environment ev;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	if (priv->drag_corba_source_interface == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_ShellComponentDnd_SourceFolder_endDrag (priv->drag_corba_source_interface,
								priv->drag_corba_source_context,
								&ev);

	CORBA_free (priv->drag_corba_source_context);
	priv->drag_corba_source_context = NULL;

	Bonobo_Unknown_unref (priv->drag_corba_source_interface, &ev);
	CORBA_Object_release (priv->drag_corba_source_interface, &ev);

	CORBA_exception_free (&ev);
}

static void
impl_tree_drag_data_get (ETree *etree,
			 int drag_row,
			 ETreePath drag_path,
			 int drag_col,
			 GdkDragContext *context,
			 GtkSelectionData *selection_data,
			 unsigned int info,
			 guint32 time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	CORBA_Environment ev;
	char *target_type;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	switch (info) {
	case E_SHORTCUT_TARGET_TYPE_IDX:
		set_e_shortcut_selection (storage_set_view, selection_data);
		return;
	case E_FOLDER_DND_PATH_TARGET_TYPE_IDX:
		set_evolution_path_selection (storage_set_view, selection_data);
		return;
	}

	g_assert (info > 0);

	if (priv->drag_corba_source_interface == CORBA_OBJECT_NIL)
		return;

	target_type = gdk_atom_name ((GdkAtom) context->targets->data);

	CORBA_exception_init (&ev);

	GNOME_Evolution_ShellComponentDnd_SourceFolder_getData (priv->drag_corba_source_interface,
								priv->drag_corba_source_context,
								convert_gdk_drag_action_set_to_corba (context->action),
								target_type,
								& priv->drag_corba_data,
								&ev);

#if 0
	if (ev._major != CORBA_NO_EXCEPTION)
		gtk_selection_data_set (selection_data, selection_data->target, 8, "", -1);
	else
		gtk_selection_data_set (selection_data,
					priv->drag_corba_data->target,
					priv->drag_corba_data->format,
					priv->drag_corba_data->bytes._buffer,
					priv->drag_corba_data->bytes._length);
#endif

	g_free (target_type);

	CORBA_exception_free (&ev);
}

static void
impl_tree_drag_data_delete (ETree *tree,
			    int row,
			    ETreePath path,
			    int col,
			    GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	CORBA_Environment ev;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	if (priv->drag_corba_source_interface == CORBA_OBJECT_NIL)
		return;

	CORBA_exception_init (&ev);

	GNOME_Evolution_ShellComponentDnd_SourceFolder_deleteData (priv->drag_corba_source_interface,
								   priv->drag_corba_source_context,
								   &ev);

	CORBA_exception_free (&ev);
}

/* -- Destination-side DnD.  */

static gboolean
impl_tree_drag_motion (ETree *tree,
		       int row,
		       ETreePath path,
		       int col,
		       GdkDragContext *context,
		       int x,
		       int y,
		       unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	storage_set_view = E_STORAGE_SET_VIEW (tree);
	priv = storage_set_view->priv;

	if (! priv->allow_dnd)
		return FALSE;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (folder_path == NULL)
		return FALSE;

	e_tree_drag_highlight (E_TREE (storage_set_view), row, -1);

	return e_folder_dnd_bridge_motion (GTK_WIDGET (storage_set_view), context, time,
					   priv->storage_set, folder_path);
}

static void
impl_tree_drag_leave (ETree *etree,
		      int row,
		      ETreePath path,
		      int col,
		      GdkDragContext *context,
		      unsigned int time)
{
	e_tree_drag_unhighlight (etree);
}

static gboolean
impl_tree_drag_drop (ETree *etree,
		     int row,
		     ETreePath path,
		     int col,
		     GdkDragContext *context,
		     int x,
		     int y,
		     unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	e_tree_drag_unhighlight (etree);

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (folder_path == NULL)
		return FALSE;

	return e_folder_dnd_bridge_drop (GTK_WIDGET (etree), context, time,
					 priv->storage_set, folder_path);
}

static void
impl_tree_drag_data_received (ETree *etree,
			      int row,
			      ETreePath path,
			      int col,
			      GdkDragContext *context,
			      int x,
			      int y,
			      GtkSelectionData *selection_data,
			      unsigned int info,
			      unsigned int time)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	const char *folder_path;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY (priv->etree_model),
						   e_tree_node_at_row (E_TREE (storage_set_view), row));
	if (path == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	e_folder_dnd_bridge_data_received  (GTK_WIDGET (etree),
					    context,
					    selection_data,
					    time,
					    priv->storage_set,
					    folder_path);
}
#endif

static gboolean
impl_button_press_event (GtkTreeView *tree, GdkEventButton *event)
{
	EStorageSetView *storage_set_view = E_STORAGE_SET_VIEW (tree);
	EStorageSetViewPrivate *priv = storage_set_view->priv;
	GtkTreePath *treepath;
	GtkTreeViewColumn *col;

	if (event->button != 3)
		return GTK_WIDGET_CLASS (parent_class)->button_press_event (GTK_WIDGET (tree), event);

	if (!gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (storage_set_view),
					    event->x, event->y, &treepath, &col, NULL, NULL))
		return FALSE;

	if (priv->ui_container) {
		g_signal_emit (storage_set_view, signals[FOLDER_CONTEXT_MENU_POPPING_UP], 0, treepath);

		popup_folder_menu (storage_set_view, event);

		g_signal_emit (storage_set_view, signals[FOLDER_CONTEXT_MENU_POPPED_DOWN], 0);
	}

	return TRUE;
}

static void
impl_row_activated (GtkTreeView *tree, GtkTreePath *treepath, GtkTreeViewColumn *col)
{
	EStorageSetView *view = E_STORAGE_SET_VIEW (tree);
	const gchar *folder_path;

	folder_path = e_storage_set_store_get_folder_path (view->priv->store, treepath);
	g_signal_emit (view, signals[FOLDER_OPENED], 0, folder_path);
}

static void
class_init (EStorageSetViewClass *klass)
{
	GObjectClass *object_class;
	GtkTreeViewClass *treeview_class;

	parent_class = g_type_class_ref(PARENT_TYPE);

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	treeview_class->row_activated  = impl_row_activated;

	signals[FOLDER_SELECTED]
		= gtk_signal_new ("folder_selected",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, folder_selected),
				  e_shell_marshal_NONE__STRING,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[FOLDER_OPENED]
		= gtk_signal_new ("folder_opened",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, folder_opened),
				  e_shell_marshal_NONE__STRING,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[DND_ACTION]
		= gtk_signal_new ("dnd_action",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, dnd_action),
				  e_shell_marshal_NONE__POINTER_POINTER_POINTER_POINTER,
				  GTK_TYPE_NONE, 4,
				  GTK_TYPE_POINTER,
				  GTK_TYPE_POINTER,
				  GTK_TYPE_POINTER,
				  GTK_TYPE_POINTER);

	signals[FOLDER_CONTEXT_MENU_POPPING_UP]
		= gtk_signal_new ("folder_context_menu_popping_up",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, folder_context_menu_popping_up),
				  e_shell_marshal_NONE__STRING,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[FOLDER_CONTEXT_MENU_POPPED_DOWN]
		= gtk_signal_new ("folder_context_menu_popped_down",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, folder_context_menu_popped_down),
				  e_shell_marshal_NONE__NONE,
				  GTK_TYPE_NONE, 0);

	signals[CHECKBOXES_CHANGED]
		= gtk_signal_new ("checkboxes_changed",
				  GTK_RUN_FIRST,
				  GTK_CLASS_TYPE (object_class),
				  G_STRUCT_OFFSET (EStorageSetViewClass, checkboxes_changed),
				  e_shell_marshal_NONE__NONE,
				  GTK_TYPE_NONE, 0);
}

static void
init (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;

	priv = g_new (EStorageSetViewPrivate, 1);

	priv->storage_set                 = NULL;

	priv->ui_component                = NULL;
	priv->ui_container                = NULL;

	priv->right_click_row_path        = NULL;

	priv->show_folders                = TRUE;
	priv->show_checkboxes             = FALSE;
	priv->allow_dnd                   = TRUE;
	priv->search_enabled              = FALSE;

	priv->drag_corba_source_interface = CORBA_OBJECT_NIL;

	priv->drag_corba_source_context   = NULL;
	priv->drag_corba_data             = NULL;

	priv->checkbox_column             = NULL;

	storage_set_view->priv = priv;
}

static void
build_treeview (GtkTreeView *view, GtkTreeModel *model)
{
	gint col_idx;
	GtkCellRenderer *txt_renderer, *pix_renderer;
	GtkTreeViewColumn *col;

	gtk_tree_view_set_model (view, model);

	txt_renderer = gtk_cell_renderer_text_new ();
	pix_renderer = gtk_cell_renderer_pixbuf_new ();
	g_object_set (G_OBJECT (txt_renderer), "xalign", 0.0, NULL);

	col = gtk_tree_view_column_new ();
	gtk_tree_view_column_pack_start (col, pix_renderer, FALSE);
	gtk_tree_view_column_pack_start (col, txt_renderer, TRUE);
	gtk_tree_view_column_set_title (col, "Folder");
	gtk_tree_view_column_set_attributes (col, txt_renderer, 
					     "text", E_STORAGE_SET_STORE_COLUMN_NAME, 
					     "weight", E_STORAGE_SET_STORE_COLUMN_HIGHLIGHT, NULL);
	gtk_tree_view_column_set_attributes (col, pix_renderer, 
					     "pixbuf", E_STORAGE_SET_STORE_COLUMN_ICON, NULL);

	gtk_tree_view_append_column (view, col);
	gtk_tree_view_set_headers_visible (view, FALSE);
	gtk_tree_view_set_rules_hint (view, TRUE);
}

static void
selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
	EStorageSetView *view = E_STORAGE_SET_VIEW (data);
	GtkTreePath *treepath;
	const gchar *folder_path;

	gtk_tree_view_get_cursor (GTK_TREE_VIEW (view), &treepath, NULL);
	folder_path = e_storage_set_store_get_folder_path (view->priv->store, treepath);
	gtk_tree_path_free (treepath);
	g_signal_emit (view, signals[FOLDER_SELECTED], 0, folder_path);
}

void
e_storage_set_view_construct (EStorageSetView   *storage_set_view,
			      EStorageSet       *storage_set,
			      BonoboUIContainer *ui_container)
{
	EStorageSetViewPrivate *priv;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (E_IS_STORAGE_SET (storage_set));

	priv = storage_set_view->priv;

	priv->ui_container = ui_container;
	if (ui_container != NULL) {
		g_object_weak_ref (G_OBJECT (ui_container), ui_container_destroy_notify, priv);

		priv->ui_component = bonobo_ui_component_new_default ();
		bonobo_ui_component_set_container (priv->ui_component,
						   bonobo_object_corba_objref (BONOBO_OBJECT (ui_container)),
						   NULL);
	}

	g_object_ref (storage_set);
	priv->storage_set = storage_set;

	priv->store = e_storage_set_store_new (storage_set, priv->show_folders);

	build_treeview (GTK_TREE_VIEW (storage_set_view), GTK_TREE_MODEL (priv->store));

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (storage_set_view))),
			  "changed", G_CALLBACK (selection_changed_cb), storage_set_view);

#if 0
	e_tree_drag_dest_set (E_TREE (storage_set_view), 0, NULL, 0, GDK_ACTION_MOVE | GDK_ACTION_COPY);
#endif
}

/* DON'T USE THIS. Use e_storage_set_new_view() instead. */
GtkWidget *
e_storage_set_view_new (EStorageSet *storage_set,
			BonoboUIContainer *ui_container)
{
	GtkWidget *new;

	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), NULL);

	new = g_object_new (e_storage_set_view_get_type (), NULL);

	e_storage_set_view_construct (E_STORAGE_SET_VIEW (new), storage_set, ui_container);

	return new;
}

EStorageSet *
e_storage_set_view_get_storage_set (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;

	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	priv = storage_set_view->priv;
	return priv->storage_set;
}

void
e_storage_set_view_set_current_folder (EStorageSetView *storage_set_view, const char *path)
{
	EStorageSetViewPrivate *priv = storage_set_view->priv;
	GtkTreePath *treepath;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (path != NULL && g_path_is_absolute (path));

	treepath = e_storage_set_store_get_tree_path (priv->store, path);

	gtk_tree_view_set_cursor (GTK_TREE_VIEW (storage_set_view), treepath, NULL, FALSE);

	gtk_tree_path_free (treepath);

	g_signal_emit (storage_set_view, signals[FOLDER_SELECTED], 0, path);
}

const char *
e_storage_set_view_get_current_folder (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv = storage_set_view->priv;
	GtkTreePath *treepath;
	const char *path;

	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);


	if (!priv->show_folders)
		return NULL; /* Mmh! */

	gtk_tree_view_get_cursor (GTK_TREE_VIEW (storage_set_view), &treepath, NULL);

	if (treepath == NULL)
		return NULL; /* Mmh? */

	path = e_storage_set_store_get_folder_path (priv->store, treepath);
	gtk_tree_path_free (treepath);

	return path;
}

void
e_storage_set_view_set_show_folders (EStorageSetView *storage_set_view, gboolean show)
{
	EStorageSetViewPrivate *priv;
	EStorageSetStore *new_store;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	priv = storage_set_view->priv;

	if (show == priv->show_folders)
		return;

	new_store = e_storage_set_store_new (priv->storage_set, show);

	gtk_tree_view_set_model (GTK_TREE_VIEW (storage_set_view), GTK_TREE_MODEL (new_store));

	g_object_unref (priv->store);
	priv->store = new_store;
	priv->show_folders = show;
}

gboolean
e_storage_set_view_get_show_folders (EStorageSetView *storage_set_view)
{
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	return storage_set_view->priv->show_folders;
}

void
e_storage_set_view_set_show_checkboxes (EStorageSetView *storage_set_view,
					gboolean show,
					EStorageSetViewHasCheckBoxFunc has_checkbox_func,
					void *func_data)
{
	EStorageSetViewPrivate *priv;

	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	priv = storage_set_view->priv;

	show = !! show;

	if (show == priv->show_checkboxes)
		return;

	priv->show_checkboxes = show;

	if (show) {
		/* FIXME: Add the checkbox column */
	} else if (priv->checkbox_column) {
			gtk_tree_view_remove_column (GTK_TREE_VIEW (storage_set_view), 
						     priv->checkbox_column);
			priv->checkbox_column = NULL;
	}
}

gboolean
e_storage_set_view_get_show_checkboxes (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), FALSE);

	return storage_set_view->priv->show_checkboxes;
}

void
e_storage_set_view_enable_search (EStorageSetView *storage_set_view,
				  gboolean enable)
{
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	enable = !! enable;

	if (enable == storage_set_view->priv->search_enabled)
		return;
	
	storage_set_view->priv->search_enabled = enable;
#if 0
	e_tree_set_search_column (E_TREE (storage_set_view), enable ? 0 : -1);
#endif
}

void
e_storage_set_view_set_checkboxes_list (EStorageSetView *storage_set_view,
					GSList          *checkboxes)
{
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
#if 0
	e_storage_set_store_set_checkboxes_list (storage_set_view->priv->store, checkboxes);
#endif
}

GSList *
e_storage_set_view_get_checkboxes_list (EStorageSetView *storage_set_view)
{
#if 0
	return e_storage_set_store_get_checkboxes_list (storage_set_view->priv->store);
#else
	return NULL;
#endif
}


void
e_storage_set_view_set_allow_dnd (EStorageSetView *storage_set_view,
				  gboolean allow_dnd)
{
	g_return_if_fail (storage_set_view != NULL);
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	storage_set_view->priv->allow_dnd = !! allow_dnd;
}

gboolean
e_storage_set_view_get_allow_dnd (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (storage_set_view != NULL, FALSE);
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), FALSE);

	return storage_set_view->priv->allow_dnd;
}

const char *
e_storage_set_view_get_right_click_path (EStorageSetView *storage_set_view)
{
	g_return_val_if_fail (storage_set_view != NULL, NULL);
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	return storage_set_view->priv->right_click_row_path;
}

typedef struct {
	xmlNode *root;
	GtkTreeView *tree;
} TreeAndRoot;

static gboolean
save_expanded_state_func (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	TreeAndRoot *tar = (TreeAndRoot *) data;
	xmlNode *xmlnode;

	if (gtk_tree_view_row_expanded (tar->tree, path)) {
		gchar *name = g_strdup (e_storage_set_store_get_folder_path (
						E_STORAGE_SET_STORE (model), path));
		xmlnode = xmlNewChild (tar->root, NULL, "node", NULL);
		e_xml_set_string_prop_by_name(xmlnode, "id", name);
	}
}

void
e_storage_set_view_load_expanded_state (EStorageSetView *view, const gchar *filename)
{
	/* FIXME */
}

void
e_storage_set_view_save_expanded_state (EStorageSetView *view, const gchar *filename)
{
	TreeAndRoot tar;
	xmlDocPtr doc;
	xmlNode *root;
	
	g_return_if_fail(E_IS_STORAGE_SET_VIEW (view));

	doc = xmlNewDoc ("1.0");
	root = xmlNewDocNode (doc, NULL,
			      (xmlChar *) "expanded_state",
			      NULL);
	xmlDocSetRootElement (doc, root);

	e_xml_set_integer_prop_by_name (root, "vers", 3);
	e_xml_set_bool_prop_by_name (root, "default", FALSE);

	tar.root = root;
	tar.tree = GTK_TREE_VIEW (view);
	
	gtk_tree_model_foreach (GTK_TREE_MODEL (view->priv->store), save_expanded_state_func, &tar);
	
	xmlSaveFile (filename, doc);
	xmlFreeDoc (doc);
}

E_MAKE_TYPE (e_storage_set_view, "EStorageSetView", EStorageSetView, class_init, init, PARENT_TYPE)
