/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-storage-set-view.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>

#include <gal/util/e-util.h>
#include "e-util/e-gtk-utils.h"

#include "e-shell-constants.h"

#include "e-storage-set-view.h"

#include <gal/e-table/e-tree-memory-callbacks.h>
#include <gal/e-table/e-cell-tree.h>
#include <gal/e-table/e-cell-text.h>

#ifdef JUST_FOR_TRANSLATORS
static char *list [] = {
	N_("Folder"),
};
#endif

#define DRAG_RESISTANCE 3	/* FIXME hardcoded in ETable to this value as
				 * well, and there is no way for us to use the
				 * same value as it's not exported.  */

#define ETREE_SPEC "<ETableSpecification no-headers=\"true\" selection-mode=\"single\" cursor-mode=\"line\" draw-grid=\"false\" horizontal-scrolling=\"true\"> \
  <ETableColumn model_col=\"0\" _title=\"Folder\" expansion=\"1.0\" minimum_width=\"20\" resizable=\"true\" cell=\"render_tree\" compare=\"string\"/> \
	<ETableState>                   			       \
		<column source=\"0\"/>     			       \
	        <grouping></grouping>                                  \
	</ETableState>                  			       \
</ETableSpecification>"


#define PARENT_TYPE E_TREE_TYPE
static ETreeClass *parent_class = NULL;

struct _EStorageSetViewPrivate {
	EStorageSet *storage_set;

	ETreeModel *etree_model;
	ETreePath root_node;

	GHashTable *path_to_etree_node;

	GHashTable *type_name_to_pixbuf;

	/* Path of the row selected by the latest "cursor_activated" signal.  */
	const char *selected_row_path;

	gboolean show_folders;

	int drag_x, drag_y;
	int drag_column, drag_row;
	ETreePath drag_path;
};


enum {
	FOLDER_SELECTED,
	STORAGE_SELECTED,
	DND_ACTION,
	LAST_SIGNAL
};

static unsigned int signals[LAST_SIGNAL] = { 0 };


/* DND stuff.  */

enum _DndTargetType {
	DND_TARGET_TYPE_URI_LIST,
	DND_TARGET_TYPE_E_SHORTCUT
};
typedef enum _DndTargetType DndTargetType;

#define URI_LIST_TYPE   "text/uri-list"
#define E_SHORTCUT_TYPE "E-SHORTCUT"

static GtkTargetEntry source_drag_types [] = {
	{ URI_LIST_TYPE, 0, DND_TARGET_TYPE_URI_LIST },
	{ E_SHORTCUT_TYPE, 0, DND_TARGET_TYPE_E_SHORTCUT }
};
static const int num_source_drag_types = sizeof (source_drag_types) / sizeof (source_drag_types[0]);

static GtkTargetEntry destination_drag_types [] = {
	{ URI_LIST_TYPE, 0, DND_TARGET_TYPE_URI_LIST },
	{ E_SHORTCUT_TYPE, 0, DND_TARGET_TYPE_E_SHORTCUT }
};
static const int num_destination_drag_types = sizeof (destination_drag_types) / sizeof (destination_drag_types[0]);

static GtkTargetList *target_list;


/* Helper functions.  */

static gboolean
add_node_to_hash (EStorageSetView *storage_set_view,
		  const char *path,
		  ETreePath node)
{
	EStorageSetViewPrivate *priv;
	char *hash_path;

	g_return_val_if_fail (g_path_is_absolute (path), FALSE);

	priv = storage_set_view->priv;

	if (g_hash_table_lookup (priv->path_to_etree_node, path) != NULL) {
		g_warning ("EStorageSetView: Node already existing while adding -- %s", path);
		return FALSE;
	}

	hash_path = g_strdup (path);

	g_hash_table_insert (priv->path_to_etree_node, hash_path, node);

	return TRUE;
}

static ETreePath
lookup_node_in_hash (EStorageSetView *storage_set_view,
		     const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL)
		g_warning ("EStorageSetView: Node not found while updating -- %s", path);

	return node;
}

static ETreePath
remove_node_from_hash (EStorageSetView *storage_set_view,
		       const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL) {
		g_warning ("EStorageSetView: Node not found while removing -- %s", path);
		return NULL;
	}

	g_hash_table_remove (priv->path_to_etree_node, path);

	return node;
}

static GdkPixbuf*
get_pixbuf_for_folder (EStorageSetView *storage_set_view,
		       EFolder *folder)
{
	GdkPixbuf *scaled_pixbuf;
	const char *type_name;
	EStorageSetViewPrivate *priv;

	priv = storage_set_view->priv;
	
	type_name = e_folder_get_type_string (folder);

	scaled_pixbuf = g_hash_table_lookup (priv->type_name_to_pixbuf, type_name);

	if (scaled_pixbuf == NULL) {
		EFolderTypeRegistry *folder_type_registry;
		EStorageSet *storage_set;
		GdkPixbuf *icon_pixbuf;
		int icon_pixbuf_width, icon_pixbuf_height;

		storage_set = priv->storage_set;
		folder_type_registry = e_storage_set_get_folder_type_registry (storage_set);

		icon_pixbuf = e_folder_type_registry_get_icon_for_type (folder_type_registry,
									type_name, TRUE);

		if (icon_pixbuf == NULL)
			return NULL;

		icon_pixbuf_width = gdk_pixbuf_get_width (icon_pixbuf);
		icon_pixbuf_height = gdk_pixbuf_get_height (icon_pixbuf);

		if (icon_pixbuf_width == E_SHELL_MINI_ICON_SIZE && icon_pixbuf_height == E_SHELL_MINI_ICON_SIZE) {
			scaled_pixbuf = gdk_pixbuf_ref (icon_pixbuf);
		} else {
			scaled_pixbuf = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (icon_pixbuf),
							gdk_pixbuf_get_has_alpha (icon_pixbuf),
							gdk_pixbuf_get_bits_per_sample (icon_pixbuf),
							E_SHELL_MINI_ICON_SIZE, E_SHELL_MINI_ICON_SIZE);

			gdk_pixbuf_scale (icon_pixbuf, scaled_pixbuf,
					  0, 0, E_SHELL_MINI_ICON_SIZE, E_SHELL_MINI_ICON_SIZE,
					  0.0, 0.0,
					  (double) E_SHELL_MINI_ICON_SIZE / gdk_pixbuf_get_width (icon_pixbuf),
					  (double) E_SHELL_MINI_ICON_SIZE / gdk_pixbuf_get_height (icon_pixbuf),
					  GDK_INTERP_HYPER);
		}

		g_hash_table_insert (priv->type_name_to_pixbuf, g_strdup(type_name), scaled_pixbuf);
	}

	return scaled_pixbuf;
}

static EFolder *
get_folder_at_node (EStorageSetView *storage_set_view,
		    ETreePath path)
{
	EStorageSetViewPrivate *priv;
	const char *folder_path;
	EFolder *folder;

	priv = storage_set_view->priv;

	if (path == NULL)
		return NULL;

	folder_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);
	g_assert (folder_path != NULL);

	folder = e_storage_set_get_folder (priv->storage_set, folder_path);
	g_assert (folder != NULL);

	return folder;
}


/* Custom marshalling function.  */

typedef void (* GtkSignal_NONE__GDKDRAGCONTEXT_STRING_STRING_STRING) (GtkObject *object,
								      GdkDragContext *action,
								      const char *,
								      const char *,
								      const char *);

static void
marshal_NONE__GDKDRAGCONTEXT_STRING_STRING_STRING (GtkObject *object,
						   GtkSignalFunc func,
						   void *func_data,
						   GtkArg *args)
{
	GtkSignal_NONE__GDKDRAGCONTEXT_STRING_STRING_STRING rfunc;

	rfunc = (GtkSignal_NONE__GDKDRAGCONTEXT_STRING_STRING_STRING) func;
	(* rfunc) (object,
		   GTK_VALUE_POINTER (args[0]),
		   GTK_VALUE_STRING (args[1]),
		   GTK_VALUE_STRING (args[2]),
		   GTK_VALUE_STRING (args[3]));
}


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

	/* We always add an entry for an Evolution URI type.  This will let us
	   do drag & drop within Evolution at least.  */
	num_entries ++;

	entries = g_new (GtkTargetEntry, num_entries);

	i = 0;

	/* The Evolution URI will always come first.  */
	entries[i].target = E_SHORTCUT_TYPE;
	entries[i].flags = 0;
	entries[i].info = 0;
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
create_target_list_for_row (EStorageSetView *storage_set_view,
			    int row)
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

	folder = get_folder_at_node (storage_set_view, e_tree_node_at_row(E_TREE(storage_set_view), row));
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

#if 0
static void
set_uri_list_selection (EStorageSetView *storage_set_view,
			GtkSelectionData *selection_data)
{
	EStorageSetViewPrivate *priv;
	char *uri_list;

	priv = storage_set_view->priv;

	/* FIXME: Get `evolution:' from somewhere instead of hardcoding it here.  */
	uri_list = g_strconcat ("evolution:", priv->selected_row_path, "\n", NULL);
	gtk_selection_data_set (selection_data, selection_data->target,
				8, (guchar *) uri_list, strlen (uri_list));
	g_free (uri_list);
}
#endif

static void
set_e_shortcut_selection (EStorageSetView *storage_set_view,
			  GtkSelectionData *selection_data)
{
	EStorageSetViewPrivate *priv;
	int shortcut_len;
	char *shortcut;
	const char *trailing_slash;
	const char *name;

	g_assert (storage_set_view != NULL);

	priv = storage_set_view->priv;

	trailing_slash = strrchr (priv->selected_row_path, '/');
	if (trailing_slash == NULL)
		name = NULL;
	else
		name = trailing_slash + 1;

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


/* Folder context menu.  */
/* FIXME: This should be moved somewhere else, so that also the shortcut code
   can share it.  */

#if 0
static void
folder_context_menu_activate_cb (BonoboUIComponent *uih,
				 void *data,
				 const char *path)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;

	gtk_signal_emit (GTK_OBJECT (storage_set_view), signals[FOLDER_SELECTED],
			 priv->selected_row_path);
}

static void
populate_folder_context_menu_with_common_items (EStorageSetView *storage_set_view,
						BonoboUIComponent *uih)
{
	bonobo_ui_handler_menu_new_item (uih, "/Activate",
					 _("_View"), _("View the selected folder"),
					 0, BONOBO_UI_HANDLER_PIXMAP_NONE,
					 NULL, 0, 0,
					 folder_context_menu_activate_cb,
					 storage_set_view);
}
#endif

static void
popup_folder_menu (EStorageSetView *storage_set_view,
		   GdkEventButton *event)
{
#if 0
	EvolutionShellComponentClient *handler;
	EStorageSetViewPrivate *priv;
	EFolderTypeRegistry *folder_type_registry;
	BonoboUIComponent *uih;
	EFolder *folder;

	priv = storage_set_view->priv;

	uih = bonobo_ui_handler_new ();
	bonobo_ui_handler_create_popup_menu (uih);

	folder = e_storage_set_get_folder (priv->storage_set, priv->selected_row_path);
	if (folder == NULL) {
		/* Uh!?  */
		return;
	}

	folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);
	g_assert (folder_type_registry != NULL);

	handler = e_folder_type_registry_get_handler_for_type (folder_type_registry,
							       e_folder_get_type_string (folder));
	g_assert (handler != NULL);

	evolution_shell_component_client_populate_folder_context_menu (handler,
								       uih,
								       e_folder_get_physical_uri (folder),
								       e_folder_get_type_string (folder));

	populate_folder_context_menu_with_common_items (storage_set_view, uih);

	bonobo_ui_handler_do_popup_menu (uih);

	bonobo_object_unref (BONOBO_OBJECT (uih));
#endif
}


/* GtkObject methods.  */

static void
path_free_func (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

static void
pixbuf_free_func (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
	gdk_pixbuf_unref ((GdkPixbuf*)value);
}

static void
destroy (GtkObject *object)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (object);
	priv = storage_set_view->priv;

	/* need to destroy our tree */
	e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), priv->root_node);
	gtk_object_unref (GTK_OBJECT (priv->etree_model));

	/* now free up all the paths stored in the hash table and
           destroy the hash table itself */
	g_hash_table_foreach (priv->path_to_etree_node, path_free_func, NULL);
	g_hash_table_destroy (priv->path_to_etree_node);

	/* now free up all the type_names and pixbufs stored in the
           hash table and destroy the hash table itself */
	g_hash_table_foreach (priv->type_name_to_pixbuf, pixbuf_free_func, NULL);
	g_hash_table_destroy (priv->type_name_to_pixbuf);

	gtk_object_unref (GTK_OBJECT (priv->storage_set));

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


/* GtkWidget methods.  */

static int
button_press_event (GtkWidget *widget,
		    GdkEventButton *event)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETree *tree;
	int row, column;

	storage_set_view = E_STORAGE_SET_VIEW (widget);
	priv = storage_set_view->priv;

	tree = E_TREE (widget);

	/* FIXME correct? */
	if (GTK_WIDGET_CLASS (parent_class)->button_press_event != NULL)
		(* GTK_WIDGET_CLASS (parent_class)->button_press_event) (widget, event);

	if (event->button != 1)
		return FALSE;

	e_tree_get_cell_at (tree, event->x, event->y, &row, &column);

	priv->drag_x = event->x;
	priv->drag_y = event->y;
	priv->drag_column = column;
	priv->drag_path = e_tree_node_at_row(E_TREE(storage_set_view), row);
	priv->drag_row = row;

	/* FIXME correct? */
	return TRUE;
}

static int
motion_notify_event (GtkWidget *widget,
		     GdkEventMotion *event)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETree *tree;
	GtkTargetList *target_list;
	GdkDragAction actions;
	GdkDragContext *context;

	puts (__FUNCTION__);

	storage_set_view = E_STORAGE_SET_VIEW (widget);
	priv = storage_set_view->priv;

	tree = E_TREE (widget);

	/* FIXME correct? */
	if (GTK_WIDGET_CLASS (parent_class)->motion_notify_event != NULL)
		(* GTK_WIDGET_CLASS (parent_class)->motion_notify_event) (widget, event);

	/* FIXME correct? */
	if (! (event->state & GDK_BUTTON1_MASK))
		return FALSE;

	if (ABS (priv->drag_x - event->x) < DRAG_RESISTANCE
	    && ABS (priv->drag_y - event->y) < DRAG_RESISTANCE)
		return FALSE;

	target_list = create_target_list_for_row (storage_set_view, priv->drag_row);
	if (target_list == NULL)
		return FALSE;

	actions = GDK_ACTION_MOVE | GDK_ACTION_COPY;

	context = e_tree_drag_begin (tree,
				      priv->drag_row, priv->drag_column,
				      target_list, actions,
				      1, (GdkEvent *) event);
	gtk_drag_set_icon_default (context);

	return FALSE;
}


/* ETree methods.  */

static void
tree_drag_begin (ETree *etree,
		  int row, ETreePath path, int col,
		  GdkDragContext *context)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	priv->selected_row_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);
}

static void
tree_drag_data_get (ETree *etree,
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
	EFolder *folder;
	EFolderTypeRegistry *folder_type_registry;
	EvolutionShellComponentClient *component_client;
	char *selection;
	int selection_length;
	int format;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	if (info == 0) {
		set_e_shortcut_selection (storage_set_view, selection_data);
		return;
	}

	g_assert (info > 0);

	folder = get_folder_at_node (storage_set_view, drag_path);
	g_assert (folder != NULL);

	folder_type_registry = e_storage_set_get_folder_type_registry (priv->storage_set);
	g_assert (folder_type_registry != NULL);

	component_client = e_folder_type_registry_get_handler_for_type (folder_type_registry,
									e_folder_get_type_string (folder));
	g_assert (component_client != NULL);

	evolution_shell_component_client_get_dnd_selection (component_client,
							    e_folder_get_physical_uri (folder),
							    info,
							    &format, &selection, &selection_length);
	if (selection == NULL)
		return;

	gtk_selection_data_set (selection_data, selection_data->target,
				format, selection, selection_length);

	g_free (selection);
}

static gboolean
tree_drag_motion (ETree *tree,
		  int row,
		  ETreePath path,
		  int col,
		  GdkDragContext *context,
		  int x,
		  int y,
		  unsigned int time)
{
	gdk_drag_status (context, GDK_ACTION_MOVE, time);

	return TRUE;
}

static gboolean
tree_drag_drop (ETree *etree,
		int row,
		ETreePath path,
		int col,
		GdkDragContext *context,
		int x,
		int y,
		unsigned int time)
{
	if (context->targets != NULL) {
		gtk_drag_get_data (GTK_WIDGET (etree), context,
				   GPOINTER_TO_INT (context->targets->data),
				   time);
		return TRUE;
	}

	return FALSE;
}

static void
tree_drag_data_received (ETree *etree,
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
	const char *target_path;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	target_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);
}

static gboolean
right_click (ETree *etree,
	     int row,
	     ETreePath path,
	     int col,
	     GdkEvent *event)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (etree);
	priv = storage_set_view->priv;

	popup_folder_menu (storage_set_view, (GdkEventButton *) event);

	return TRUE;
}

static void
cursor_activated (ETree *tree,
		  int row,
		  ETreePath path)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;

	storage_set_view = E_STORAGE_SET_VIEW (tree);

	priv = storage_set_view->priv;

	priv->selected_row_path = e_tree_memory_node_get_data (E_TREE_MEMORY(priv->etree_model), path);

	if (e_tree_model_node_depth (priv->etree_model, path) >= 2) {
		/* it was a folder */
		gtk_signal_emit (GTK_OBJECT (storage_set_view), signals[FOLDER_SELECTED],
				 priv->selected_row_path);
	} else {
		/* it was a storage */
		gtk_signal_emit (GTK_OBJECT (storage_set_view), signals[STORAGE_SELECTED],
				 priv->selected_row_path + 1);
	}
}


/* ETreeModel Methods */

static GdkPixbuf*
etree_icon_at (ETreeModel *etree,
	       ETreePath tree_path,
	       void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	EFolder *folder;
	char *path;

	/* folders are from depth 2 on.  depth 1 are storages and 0 is
           our (invisible) root node. */
	if (e_tree_model_node_depth (etree, tree_path) < 2)
		return NULL;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	storage_set = storage_set_view->priv->storage_set;

	path = (char*)e_tree_memory_node_get_data (E_TREE_MEMORY(etree), tree_path);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder == NULL)
		return NULL;

	return get_pixbuf_for_folder (storage_set_view, folder);
}

/* This function returns the number of columns in our ETreeModel. */
static int
etree_column_count (ETreeModel *etc, void *data)
{
	return 2;
}

static gboolean
etree_has_save_id (ETreeModel *etm, void *data)
{
	return TRUE;
}

static gchar *
etree_get_save_id (ETreeModel *etm, ETreePath node, void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	EStorage *storage;
	EFolder *folder;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	storage_set = storage_set_view->priv->storage_set;

	path = (char *) e_tree_memory_node_get_data (E_TREE_MEMORY(etm), node);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder != NULL) {
		return g_strdup (e_folder_get_name (folder));
	}

	storage = e_storage_set_get_storage (storage_set, path + 1);
	if (storage != NULL)
		return g_strdup (e_storage_get_name (storage));

	return g_strdup("root");
}

static void *
etree_value_at (ETreeModel *etree, ETreePath tree_path, int col, void *model_data)
{
	EStorageSetView *storage_set_view;
	EStorageSet *storage_set;
	EStorage *storage;
	EFolder *folder;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (model_data);
	storage_set = storage_set_view->priv->storage_set;

	path = (char *) e_tree_memory_node_get_data (E_TREE_MEMORY(etree), tree_path);

	folder = e_storage_set_get_folder (storage_set, path);
	if (folder != NULL) {
		if (col == 0)
			return (void *) e_folder_get_name (folder);
		else
			return (void *) e_folder_get_highlighted (folder);
	}

	storage = e_storage_set_get_storage (storage_set, path + 1);
	if (storage != NULL && col == 0)
		return (void *) e_storage_get_name (storage);

	return NULL;
}

static void
etree_set_value_at (ETreeModel *etree, ETreePath path, int col, const void *val, void *model_data)
{
	/* nada */
}

static gboolean
etree_is_editable (ETreeModel *etree, ETreePath path, int col, void *model_data)
{
	return FALSE;
}


/* This function duplicates the value passed to it. */
static void *
etree_duplicate_value (ETreeModel *etc, int col, const void *value, void *data)
{
	if (col == 0)
		return g_strdup (value);
	else
		return (void *)value;
}

/* This function frees the value passed to it. */
static void
etree_free_value (ETreeModel *etc, int col, void *value, void *data)
{
	if (col == 0)
		g_free (value);
}

/* This function creates an empty value. */
static void *
etree_initialize_value (ETreeModel *etc, int col, void *data)
{
	if (col == 0)
		return g_strdup ("");
	else
		return NULL;
}

/* This function reports if a value is empty. */
static gboolean
etree_value_is_empty (ETreeModel *etc, int col, const void *value, void *data)
{
	if (col == 0)
		return !(value && *(char *)value);
	else
		return !value;
}

/* This function reports if a value is empty. */
static char *
etree_value_to_string (ETreeModel *etc, int col, const void *value, void *data)
{
	if (col == 0)
		return g_strdup(value);
	else
		return g_strdup(value ? "Yes" : "No");
}



static void
new_storage_cb (EStorageSet *storage_set,
		EStorage *storage,
		void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreePath node;
	char *path;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;

	path = g_strconcat (G_DIR_SEPARATOR_S, e_storage_get_name (storage), NULL);

	node = e_tree_memory_node_insert_id (E_TREE_MEMORY(priv->etree_model),
					     priv->root_node,
					     -1, path, path);

	e_tree_node_set_expanded (E_TREE(storage_set), node, TRUE);

	if (! add_node_to_hash (storage_set_view, path, node)) {
		g_free (path);
		e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), node);
		return;
	}
}

static void
removed_storage_cb (EStorageSet *storage_set,
		    EStorage *storage,
		    void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;
	char *path;
	char *node_data;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	path = g_strconcat (G_DIR_SEPARATOR_S, e_storage_get_name (storage), NULL);
	node = remove_node_from_hash (storage_set_view, path);
	g_free (path);

	node_data = e_tree_memory_node_remove (E_TREE_MEMORY(etree), node);
	g_free (node_data);
}

static void
new_folder_cb (EStorageSet *storage_set,
	       const char *path,
	       void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath parent_node;
	ETreePath new_node;
	const char *last_separator;
	char *parent_path;
	char *copy_of_path;

	g_return_if_fail (g_path_is_absolute (path));

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	last_separator = strrchr (path, G_DIR_SEPARATOR);

	parent_path = g_strndup (path, last_separator - path);
	parent_node = g_hash_table_lookup (priv->path_to_etree_node, parent_path);
	if (parent_node == NULL) {
		g_warning ("EStorageSetView: EStorageSet reported new subfolder for non-existing folder -- %s\n",
			   parent_path);
		g_free (parent_path);
		return;
	}

	g_free (parent_path);

	copy_of_path = g_strdup (path);
	new_node = e_tree_memory_node_insert_id (E_TREE_MEMORY(etree), parent_node, -1, copy_of_path, copy_of_path);

	if (! add_node_to_hash (storage_set_view, path, new_node)) {
		e_tree_memory_node_remove (E_TREE_MEMORY(etree), new_node);
		return;
	}
}

static void
updated_folder_cb (EStorageSet *storage_set,
		   const char *path,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	node = lookup_node_in_hash (storage_set_view, path);
	e_tree_model_node_data_changed (etree, node);
}

static void
removed_folder_cb (EStorageSet *storage_set,
		   const char *path,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;
	char *node_data;

	storage_set_view = E_STORAGE_SET_VIEW (data);
	priv = storage_set_view->priv;
	etree = priv->etree_model;

	node = remove_node_from_hash (storage_set_view, path);
	node_data = e_tree_memory_node_remove (E_TREE_MEMORY(etree), node);
	g_free (node_data);
}


static void
class_init (EStorageSetViewClass *klass)
{
	GtkObjectClass *object_class;
	ETreeClass *etree_class;
	GtkWidgetClass *widget_class;

	parent_class = gtk_type_class (e_tree_get_type ());

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->button_press_event  = button_press_event;
	widget_class->motion_notify_event = motion_notify_event;

	etree_class = E_TREE_CLASS (klass);
	etree_class->right_click              = right_click;
	etree_class->cursor_activated         = cursor_activated;
	etree_class->tree_drag_begin         = tree_drag_begin;
	etree_class->tree_drag_data_get      = tree_drag_data_get;
	etree_class->tree_drag_motion        = tree_drag_motion;
	etree_class->tree_drag_drop          = tree_drag_drop;
	etree_class->tree_drag_data_received = tree_drag_data_received;

	signals[FOLDER_SELECTED]
		= gtk_signal_new ("folder_selected",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EStorageSetViewClass, folder_selected),
				  gtk_marshal_NONE__STRING,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[STORAGE_SELECTED]
		= gtk_signal_new ("storage_selected",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EStorageSetViewClass, storage_selected),
				  gtk_marshal_NONE__STRING,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[DND_ACTION]
		= gtk_signal_new ("dnd_action",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EStorageSetViewClass, dnd_action),
				  marshal_NONE__GDKDRAGCONTEXT_STRING_STRING_STRING,
				  GTK_TYPE_NONE, 4,
				  GTK_TYPE_GDK_DRAG_CONTEXT,
				  GTK_TYPE_STRING,
				  GTK_TYPE_STRING,
				  GTK_TYPE_STRING);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	/* Set up DND.  */

	target_list = gtk_target_list_new (source_drag_types, num_source_drag_types);
	g_assert (target_list != NULL);
}

static void
init (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;

	priv = g_new (EStorageSetViewPrivate, 1);

	priv->storage_set         = NULL;
	priv->path_to_etree_node  = g_hash_table_new (g_str_hash, g_str_equal);
	priv->type_name_to_pixbuf = g_hash_table_new (g_str_hash, g_str_equal);
	priv->selected_row_path   = NULL;
	priv->show_folders        = TRUE;
	priv->drag_x              = 0;
	priv->drag_y              = 0;
	priv->drag_column         = 0;
	priv->drag_row            = 0;

	storage_set_view->priv = priv;
}


/* Handling of the "changed" signal in EFolders displayed in the EStorageSetView.  */

struct _FolderChangedCallbackData {
	EStorageSetView *storage_set_view;
	char *path;
};
typedef struct _FolderChangedCallbackData FolderChangedCallbackData;

static void
folder_changed_callback_data_destroy_notify (void *data)
{
	FolderChangedCallbackData *callback_data;

	callback_data = (FolderChangedCallbackData *) data;

	g_free (callback_data->path);
	g_free (callback_data);
}

static void
folder_changed_cb (EFolder *folder,
		   void *data)
{
	EStorageSetView *storage_set_view;
	EStorageSetViewPrivate *priv;
	FolderChangedCallbackData *callback_data;
	ETreePath node;

	callback_data = (FolderChangedCallbackData *) data;

	storage_set_view = callback_data->storage_set_view;
	priv = callback_data->storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, callback_data->path);
	if (node == NULL) {
		g_warning ("EStorageSetView -- EFolder::changed emitted for a folder whose path I don't know.");
		return;
	}

	e_tree_model_node_data_changed (priv->etree_model, node);
}


static void
insert_folders (EStorageSetView *storage_set_view,
		ETreePath parent,
		EStorage *storage,
		const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreeModel *etree;
	ETreePath node;
	GList *folder_path_list;
	GList *p;
	const char *storage_name;

	priv = storage_set_view->priv;
	etree = priv->etree_model;

	storage_name = e_storage_get_name (storage);

	folder_path_list = e_storage_get_subfolder_paths (storage, path);
	if (folder_path_list == NULL)
		return;

	for (p = folder_path_list; p != NULL; p = p->next) {
		FolderChangedCallbackData *folder_changed_callback_data;
		EFolder *folder;
		const char *folder_name;
		const char *folder_path;
		char *full_path;

		folder_path = (const char *) p->data;
		folder = e_storage_get_folder (storage, folder_path);
		folder_name = e_folder_get_name (folder);

		full_path = g_strconcat ("/", storage_name, folder_path, NULL);
		node = e_tree_memory_node_insert_id (E_TREE_MEMORY(etree), parent, -1, (void *) full_path, full_path);
		add_node_to_hash (storage_set_view, full_path, node);

		insert_folders (storage_set_view, node, storage, folder_path);

		folder_changed_callback_data = g_new (FolderChangedCallbackData, 1);
		folder_changed_callback_data->storage_set_view = storage_set_view;
		folder_changed_callback_data->path = g_strdup (full_path);

		e_gtk_signal_connect_full_while_alive (GTK_OBJECT (folder), "changed",
						       GTK_SIGNAL_FUNC (folder_changed_cb),
						       NULL,
						       folder_changed_callback_data,
						       folder_changed_callback_data_destroy_notify,
						       FALSE, FALSE,
						       GTK_OBJECT (storage_set_view));
	}

	e_free_string_list (folder_path_list);
}

static void
insert_storages (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;
	EStorageSet *storage_set;
	GList *storage_list;
	GList *p;

	priv = storage_set_view->priv;

	storage_set = priv->storage_set;

	storage_list = e_storage_set_get_storage_list (storage_set);

	for (p = storage_list; p != NULL; p = p->next) {
		EStorage *storage = E_STORAGE (p->data);
		const char *name;
		char *path;
		ETreePath parent;

		name = e_storage_get_name (storage);
		path = g_strconcat ("/", name, NULL);

		parent = e_tree_memory_node_insert_id (E_TREE_MEMORY(priv->etree_model), priv->root_node,
						   -1, path, path);
		e_tree_node_set_expanded (E_TREE(storage_set_view), parent, TRUE);

		g_hash_table_insert (priv->path_to_etree_node, path, parent);

		if (priv->show_folders)
			insert_folders (storage_set_view, parent, storage, "/");
	}

	e_free_object_list (storage_list);
}

void
e_storage_set_view_construct (EStorageSetView *storage_set_view,
			      EStorageSet *storage_set)
{
	EStorageSetViewPrivate *priv;
	ETableExtras *extras;
	ECell *cell;

	g_return_if_fail (storage_set_view != NULL);
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (storage_set != NULL);
	g_return_if_fail (E_IS_STORAGE_SET (storage_set));

	priv = storage_set_view->priv;

	priv->etree_model = e_tree_memory_callbacks_new (etree_icon_at,

							 etree_column_count,

							 etree_has_save_id,
							 etree_get_save_id,

							 etree_value_at,
							 etree_set_value_at,
							 etree_is_editable,

							 etree_duplicate_value,
							 etree_free_value,
							 etree_initialize_value,
							 etree_value_is_empty,
							 etree_value_to_string,

							 storage_set_view);

	priv->root_node = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), NULL, -1, "/Root Node");

	extras = e_table_extras_new ();
	cell = e_cell_text_new (NULL, GTK_JUSTIFY_LEFT);
	gtk_object_set (GTK_OBJECT (cell), "bold_column", 1, NULL);
	e_table_extras_add_cell (extras, "render_tree",
				 e_cell_tree_new (NULL, NULL, TRUE, cell));

	e_tree_construct (E_TREE (storage_set_view), priv->etree_model, extras,
			   ETREE_SPEC, NULL);

	e_tree_root_node_set_visible (E_TREE(storage_set_view), FALSE);

	gtk_object_unref (GTK_OBJECT (extras));

#if 0
	e_tree_drag_source_set (E_TREE (storage_set_view), GDK_BUTTON1_MASK,
	e_table_drag_source_set (E_TABLE (storage_set_view), GDK_BUTTON1_MASK,
				 source_drag_types, num_source_drag_types,
				 GDK_ACTION_MOVE | GDK_ACTION_COPY);

	e_tree_drag_dest_set (E_TREE (storage_set_view), GTK_DEST_DEFAULT_ALL,
			       source_drag_types, num_source_drag_types,
			       GDK_ACTION_MOVE | GDK_ACTION_COPY);
#endif

	gtk_object_ref (GTK_OBJECT (storage_set));
	priv->storage_set = storage_set;

	gtk_signal_connect_while_alive (GTK_OBJECT (storage_set), "new_storage",
					GTK_SIGNAL_FUNC (new_storage_cb), storage_set_view,
					GTK_OBJECT (storage_set_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (storage_set), "removed_storage",
					GTK_SIGNAL_FUNC (removed_storage_cb), storage_set_view,
					GTK_OBJECT (storage_set_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (storage_set), "new_folder",
					GTK_SIGNAL_FUNC (new_folder_cb), storage_set_view,
					GTK_OBJECT (storage_set_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (storage_set), "updated_folder",
					GTK_SIGNAL_FUNC (updated_folder_cb), storage_set_view,
					GTK_OBJECT (storage_set_view));
	gtk_signal_connect_while_alive (GTK_OBJECT (storage_set), "removed_folder",
					GTK_SIGNAL_FUNC (removed_folder_cb), storage_set_view,
					GTK_OBJECT (storage_set_view));

	insert_storages (storage_set_view);
}

GtkWidget *
e_storage_set_view_new (EStorageSet *storage_set)
{
	GtkWidget *new;

	g_return_val_if_fail (storage_set != NULL, NULL);
	g_return_val_if_fail (E_IS_STORAGE_SET (storage_set), NULL);

	new = gtk_type_new (e_storage_set_view_get_type ());
	e_storage_set_view_construct (E_STORAGE_SET_VIEW (new), storage_set);

	return new;
}


void
e_storage_set_view_set_current_folder (EStorageSetView *storage_set_view,
				       const char *path)
{
	EStorageSetViewPrivate *priv;
	ETreePath node;

	g_return_if_fail (storage_set_view != NULL);
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));
	g_return_if_fail (path != NULL && g_path_is_absolute (path));

	priv = storage_set_view->priv;

	node = g_hash_table_lookup (priv->path_to_etree_node, path);
	if (node == NULL) {
		return;
	}

	e_tree_show_node (E_TREE(storage_set_view), node);
	e_tree_set_cursor (E_TREE (storage_set_view), node);

	gtk_signal_emit (GTK_OBJECT (storage_set_view), signals[FOLDER_SELECTED], path);
}

const char *
e_storage_set_view_get_current_folder (EStorageSetView *storage_set_view)
{
	EStorageSetViewPrivate *priv;
	ETreePath etree_node;
	const char *path;

	g_return_val_if_fail (storage_set_view != NULL, NULL);
	g_return_val_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view), NULL);

	priv = storage_set_view->priv;

	if (!priv->show_folders)
		return NULL; /* Mmh! */

	etree_node = e_tree_get_cursor (E_TREE (storage_set_view));

	if (etree_node == NULL)
		return NULL;	/* Mmh? */

	path = (char*)e_tree_memory_node_get_data(E_TREE_MEMORY(priv->etree_model), etree_node);

	return path;
}

void
e_storage_set_view_set_show_folders (EStorageSetView *storage_set_view,
				     gboolean show)
{
	EStorageSetViewPrivate *priv;

	g_return_if_fail (storage_set_view != NULL);
	g_return_if_fail (E_IS_STORAGE_SET_VIEW (storage_set_view));

	priv = storage_set_view->priv;

	if (show == priv->show_folders)
		return;

	/* tear down existing tree and hash tree mappings */
	e_tree_memory_node_remove (E_TREE_MEMORY(priv->etree_model), priv->root_node);
	g_hash_table_foreach (priv->path_to_etree_node, path_free_func, NULL);

	/* now re-add the root node */
	priv->root_node = e_tree_memory_node_insert (E_TREE_MEMORY(priv->etree_model), NULL, -1, "/Root Node");

	/* then reinsert the storages after setting the "show_folders"
	   flag.  insert_storages will call insert_folders if
	   show_folders is TRUE */

	priv->show_folders = show;
	insert_storages (storage_set_view);
}

gboolean
e_storage_set_view_get_show_folders (EStorageSetView *storage_set_view)
{
	return storage_set_view->priv->show_folders;
}


E_MAKE_TYPE (e_storage_set_view, "EStorageSetView", EStorageSetView, class_init, init, PARENT_TYPE)
