/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Iain Holmes <iain@ximian.com>
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

#include <stdio.h>
#include <string.h>

#include <gtk/gtkhbox.h>
#include <gtk/gtkenums.h>
#include <gtk/gtksignal.h>

#include <gal/util/e-util.h>
#include <gal/widgets/e-gui-utils.h>

#include <gal/e-table/e-table-header.h>
#include <gal/e-table/e-table-header-item.h>
#include <gal/e-table/e-table-item.h>
#include <gal/e-table/e-cell-text.h>
#include <gal/e-table/e-cell-tree.h>
#include <gal/e-table/e-table.h>
#include <gal/e-table/e-tree.h>

#include <gal/e-table/e-tree-scrolled.h>
#include <gal/e-table/e-tree-memory.h>
#include <gal/e-table/e-tree-memory-callbacks.h>
#include <gal/e-table/e-tree-table-adapter.h>

#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-init.h>
#include <libgnomeui/gnome-stock.h>

#include "e-summary-shown.h"

#define COLS 1

/* Needs to be filled in before use */
#define SPEC "<ETableSpecification cursor-mode=\"line\" draw-focus=\"true\"> \
<ETableColumn model_col=\"0\" _title=\"%s\" resizable=\"true\" cell=\"tree-string\" compare=\"string\"/> \
<ETableState> \
<column source=\"0\"/> \
<grouping></grouping> \
</ETableState> \
</ETableSpecification>"

#define PARENT_TYPE (gtk_hbox_get_type ())
static GtkObjectClass *e_summary_shown_parent_class;

enum {
	ITEM_CHANGED,
	SELECTION_CHANGED,
	LAST_SIGNAL
};
static guint32 shown_signals[LAST_SIGNAL] = { 0 };

typedef struct _TableData {
	ETreeModel *etm;
	ETreePath *root;
	
	GtkWidget *etable;
	GSList *contents;
} TableData;

struct _ESummaryShownPrivate {
	TableData *all, *shown;
	GtkWidget *add, *remove;
};

static GdkPixbuf *
icon_at (ETreeModel *etm,
	 ETreePath path,
	 void *model_data)
{
	return NULL;
}

static int
column_count (ETreeModel *etm,
	      void *data)
{
	return COLS;
}

static void *
duplicate_value (ETreeModel *etm,
		 int col,
		 const void *value,
		 void *data)
{
	return g_strdup (value);
}

static void
free_value (ETreeModel *etm,
	    int col,
	    void *value,
	    void *data)
{
	g_free (value);
}

static void *
initialise_value (ETreeModel *etm,
		  int col,
		  void *data)
{
	return g_strdup ("");
}

static gboolean
value_is_empty (ETreeModel *etm,
		int col,
		const void *value,
		void *data)
{
	return !(value && *(char *)value);
}

static char *
value_to_string (ETreeModel *etm,
		 int col,
		 const void *value,
		 void *data)
{
	return g_strdup (value);
}

static void *
value_at (ETreeModel *etm,
	  ETreePath path,
	  int col,
	  void *model_data)
{
	GHashTable *model = model_data;
	ESummaryShownModelEntry *entry;

	if (e_tree_model_node_is_root (etm, path)) {
		return "<Root>";
	}

	entry = g_hash_table_lookup (model, path);
	if (entry == NULL) {
		return "<None>";
	} else {
		return entry->name;
	}
}

static gboolean
is_editable (ETreeModel *etm,
	     ETreePath path,
	     int col,
	     void *model_data)
{
	return FALSE;
}

static void
destroy (GtkObject *object)
{
	ESummaryShown *shown;
	ESummaryShownPrivate *priv;

	shown = E_SUMMARY_SHOWN (object);
	priv = shown->priv;

	if (priv == NULL) {
		return;
	}

	g_free (priv);
	shown->priv = NULL;

	e_summary_shown_parent_class->destroy (object);
}

static void
e_summary_shown_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = destroy;

	e_summary_shown_parent_class = gtk_type_class (PARENT_TYPE);

	shown_signals[ITEM_CHANGED] = gtk_signal_new ("item-changed",
						      GTK_RUN_LAST,
						      object_class->type,
						      GTK_SIGNAL_OFFSET (ESummaryShownClass, item_changed),
						      gtk_marshal_NONE__NONE,
						      GTK_TYPE_NONE, 0);
	shown_signals[SELECTION_CHANGED] = gtk_signal_new ("selection-changed",
							   GTK_RUN_LAST,
							   object_class->type,
							   GTK_SIGNAL_OFFSET (ESummaryShownClass, selection_changed),
							   gtk_marshal_NONE__POINTER,
							   GTK_TYPE_NONE, 1,
							   GTK_TYPE_POINTER);
	gtk_object_class_add_signals (object_class, shown_signals, LAST_SIGNAL);
}

static gboolean
is_location_in_shown (ESummaryShown *shown,
		      const char *location)
{
	GSList *p;

	for (p = shown->priv->shown->contents; p; p = p->next) {
		ESummaryShownModelEntry *entry = p->data;
		if (entry->location == NULL) {
			continue;
		}
		
		if (strcmp (entry->location, location) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

struct _CountData {
	ESummaryShown *shown;
	GList *selected_list;
	int count;
};

static void
real_selected_count (ETreePath path,
		     gpointer data)
{
	ESummaryShownModelEntry *entry;
	struct _CountData *cd = data;

	entry = g_hash_table_lookup (cd->shown->all_model, path);
	g_return_if_fail (entry != NULL);

	cd->selected_list = g_list_prepend (cd->selected_list, path);
	if (entry->showable == FALSE) {
		return;
	}

	if (is_location_in_shown (cd->shown, entry->location)) {
		return;
	}

	cd->count++;
}
	
static void
all_selection_changed (ETree *et,
		       ESummaryShown *shown)
{
	ESelectionModel *esm;
	int count;
	
	esm = e_tree_get_selection_model (et);
	
	count = e_selection_model_selected_count (esm);
	if (count == 0) {
		gtk_widget_set_sensitive (shown->priv->add, FALSE);

		gtk_signal_emit (GTK_OBJECT (shown), shown_signals[SELECTION_CHANGED], 0, NULL);
	} else {
		struct _CountData *cd;

		cd = g_new (struct _CountData, 1);
		cd->shown = shown;
		cd->selected_list = NULL;
		cd->count = 0;

		e_tree_selection_model_foreach (E_TREE_SELECTION_MODEL (esm),
						real_selected_count, cd);
		if (cd->count != 0) {
			gtk_widget_set_sensitive (shown->priv->add, TRUE);
		} else {
			gtk_widget_set_sensitive (shown->priv->add, FALSE);
		}

		gtk_signal_emit (GTK_OBJECT (shown), shown_signals[SELECTION_CHANGED], cd->selected_list);

		g_list_free (cd->selected_list);
		g_free (cd);
	}
}

static void
shown_selection_changed (ETree *et,
			 ESummaryShown *shown)
{
	ESelectionModel *esm;
	int count;

	esm = e_tree_get_selection_model (et);
	
	count = e_selection_model_selected_count (esm);
	if (count == 0) {
		gtk_widget_set_sensitive (shown->priv->remove, FALSE);
	} else {
		gtk_widget_set_sensitive (shown->priv->remove, TRUE);
	}
}

static void
maybe_move_to_shown (ETreePath path,
		     gpointer closure)
{
	gpointer *pair = closure;
	ESummaryShown *shown = pair[0];
	GList **list = pair[1];
	ESummaryShownModelEntry *entry, *new_entry;

	entry = g_hash_table_lookup (shown->all_model, path);
	g_return_if_fail (entry != NULL);

	/* Check is the entry can be shown */
	if (entry->showable == FALSE) {
		return;
	}
	
	/* check if the entry is already in the shown list */
	if (is_location_in_shown (shown, entry->location) == TRUE) {
		return;
	}
	
	new_entry = g_new (ESummaryShownModelEntry, 1);
	new_entry->name = g_strdup (entry->name);
	new_entry->location = g_strdup (entry->location);
	new_entry->showable = entry->showable;
	new_entry->ref_count = 0;

	*list = g_list_prepend (*list, new_entry);
}

static void
add_clicked (GtkWidget *button,
	     ESummaryShown *shown)
{
	ESelectionModel *esm;
	ETree *et;
	gpointer pair[2];
	GList *list = NULL;
	GList *iterator;
		
	et = e_tree_scrolled_get_tree (E_TREE_SCROLLED (shown->priv->all->etable));
	esm = e_tree_get_selection_model (et);

	pair[0] = shown;
	pair[1] = &list;
	e_tree_selection_model_foreach (E_TREE_SELECTION_MODEL (esm),
					maybe_move_to_shown, pair);

	for (iterator = list; iterator; iterator = iterator->next) {
		ESummaryShownModelEntry *new_entry = iterator->data;
		e_summary_shown_add_node (shown, FALSE, new_entry, NULL, TRUE, NULL);
	}

	g_list_free (list);
	gtk_signal_emit (GTK_OBJECT (shown), shown_signals[ITEM_CHANGED]);
        
        gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE); 
}

static void
remove_from_shown (ETreePath path,
		   gpointer closure)
{
	gpointer *pair = closure;
	ESummaryShown *shown = pair[0];
	GList **list = pair[1];
	ESummaryShownModelEntry *entry;

	entry = g_hash_table_lookup (shown->shown_model, path);
	g_return_if_fail (entry != NULL);

	*list = g_list_prepend (*list, entry);
}

static void
remove_clicked (GtkWidget *button,
		ESummaryShown *shown)
{
	ESelectionModel *esm;
	ETree *et;
	gpointer pair[2];
	GList *list = NULL;
	GList *iterator;

	et = e_tree_scrolled_get_tree (E_TREE_SCROLLED (shown->priv->shown->etable));
	esm = e_tree_get_selection_model (et);

	pair[0] = shown;
	pair[1] = &list;
	e_tree_selection_model_foreach (E_TREE_SELECTION_MODEL (esm),
					remove_from_shown, pair);

	list = g_list_reverse (list);

	for (iterator = list; iterator; iterator = iterator->next) {
		ESummaryShownModelEntry *entry = iterator->data;
		e_summary_shown_remove_node (shown, FALSE, entry);
	}
	g_list_free (list);

	gtk_signal_emit (GTK_OBJECT (shown), shown_signals[ITEM_CHANGED]);

	gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
}

static TableData *
make_table (GHashTable *data_model,
	    const char *title,
	    GtkSignalFunc callback,
	    gpointer closure)
{
	TableData *td;
	ETreeMemory *etmm;
	ETree *tree;
	char *real_spec;

	td = g_new (TableData, 1);
	td->etm = e_tree_memory_callbacks_new (icon_at,
					       column_count,
					       
					       NULL,
					       NULL,
					       
					       NULL,
					       NULL,
					       
					       value_at,
					       NULL,
					       is_editable,
					       
					       duplicate_value,
					       free_value,
					       initialise_value,
					       value_is_empty,
					       value_to_string,
					       
					       data_model);
	gtk_object_ref (GTK_OBJECT (td->etm));
	gtk_object_sink (GTK_OBJECT (td->etm));
	
	etmm = E_TREE_MEMORY (td->etm);
	e_tree_memory_set_expanded_default (etmm, TRUE);

	td->root = e_tree_memory_node_insert (etmm, NULL, 0, NULL);

	real_spec = g_strdup_printf (SPEC, title);
	td->etable = e_tree_scrolled_new (td->etm, NULL, real_spec, NULL);
	g_free (real_spec);

	tree = e_tree_scrolled_get_tree (E_TREE_SCROLLED (td->etable));
	e_tree_root_node_set_visible (tree, FALSE);
	gtk_signal_connect (GTK_OBJECT (tree), "selection-change",
			    callback, closure);

	td->contents = NULL;
	return td;
}

static GtkWidget *
construct_pixmap_button (const char *text,
			 const char *image)
{
	GtkWidget *box, *button, *pixmap, *label;

	box = gtk_hbox_new (FALSE, 1);

	pixmap = gnome_stock_pixmap_widget (NULL, image);
	gtk_box_pack_start (GTK_BOX (box), pixmap, FALSE, FALSE, 0);
	
	label = gtk_label_new (text);
	gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

	button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (button), box);

	gtk_widget_show_all (box);

	return button;
}
	
static void
e_summary_shown_init (ESummaryShown *shown)
{
	ESummaryShownPrivate *priv;
	GtkWidget *vbox;
	GtkWidget *align;

	gtk_box_set_spacing (GTK_BOX (shown), 3);

	shown->shown_model = g_hash_table_new (NULL, NULL);
	shown->all_model = g_hash_table_new (NULL, NULL);

	priv = g_new (ESummaryShownPrivate, 1);
	shown->priv = priv;

	priv->all = make_table (shown->all_model, _("All"), GTK_SIGNAL_FUNC (all_selection_changed), shown);
	
	gtk_box_pack_start (GTK_BOX (shown), priv->all->etable, TRUE, TRUE, 2);
	gtk_widget_show (priv->all->etable);
	
	vbox = gtk_vbox_new (TRUE, 9);	
	align = gtk_alignment_new (.5, .5, .5, 0.0);
        gtk_container_add (GTK_CONTAINER (align), vbox);

	gtk_box_pack_start (GTK_BOX (shown), align, FALSE, FALSE, 3);

	/* Fixme: nice GFX version */
	priv->add = construct_pixmap_button (_("Add"), GNOME_STOCK_BUTTON_NEXT);
	gtk_widget_set_sensitive (priv->add, FALSE);
	gtk_box_pack_start (GTK_BOX (vbox), priv->add, TRUE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (priv->add), "clicked",
			    GTK_SIGNAL_FUNC (add_clicked), shown);

	/* Fixme: Ditto */
	priv->remove = construct_pixmap_button (_("Remove"), GNOME_STOCK_BUTTON_PREV);
	gtk_widget_set_sensitive (priv->remove, FALSE);
	gtk_box_pack_start (GTK_BOX (vbox), priv->remove, TRUE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (priv->remove), "clicked",
			    GTK_SIGNAL_FUNC (remove_clicked), shown);

	gtk_widget_show_all (align);

	priv->shown = make_table (shown->shown_model, _("Shown"), GTK_SIGNAL_FUNC (shown_selection_changed), shown);

	gtk_box_pack_start (GTK_BOX (shown), priv->shown->etable, TRUE, TRUE, 2);
	gtk_widget_show (priv->shown->etable);
}

E_MAKE_TYPE (e_summary_shown, "ESummaryShown", ESummaryShown,
	     e_summary_shown_class_init, e_summary_shown_init, PARENT_TYPE);

GtkWidget *
e_summary_shown_new (void)
{
	ESummaryShown *shown;

	shown = gtk_type_new (e_summary_shown_get_type ());
	return GTK_WIDGET (shown);
}

static ETreePath
e_tree_model_node_append (ETreeModel *etm,
			  ETreePath parent,
			  gpointer data)
{
	ETreeMemory *etmm;
	ETreePath path;
	
	etmm = E_TREE_MEMORY (etm);
	e_tree_memory_freeze (etmm);
	path = e_tree_memory_node_insert (etmm, parent, -1, data);
	e_tree_memory_thaw (etmm);

	return path;
}

ETreePath
e_summary_shown_add_node (ESummaryShown *shown,
			  gboolean all,
			  ESummaryShownModelEntry *entry,
			  ETreePath parent,
			  gboolean expanded,
			  gpointer data)
{
	TableData *td;
	ETreePath path;
	ETreeMemory *etmm;
	ETree *tree;
	GHashTable *model;
	
	g_return_val_if_fail (IS_E_SUMMARY_SHOWN (shown), NULL);

	if (all == TRUE) {
		td = shown->priv->all;
		model = shown->all_model;
	} else {
		td = shown->priv->shown;
		model = shown->shown_model;
	}

	if (parent == NULL) {
		parent = td->root;
	}

	etmm = E_TREE_MEMORY (td->etm);
	path = e_tree_model_node_append (td->etm, parent, data);

	tree = e_tree_scrolled_get_tree (E_TREE_SCROLLED (td->etable));
	e_tree_node_set_expanded (tree, path, expanded);

	entry->path = path;
	
	g_hash_table_insert (model, path, entry);

	if (all == FALSE) {
		/* Add the location to the list */
		td->contents = g_slist_prepend (td->contents, entry);
	}

	return path;
}

void
e_summary_shown_remove_node (ESummaryShown *shown,
			     gboolean all,
			     ESummaryShownModelEntry *entry)
{
	TableData *td;
	GHashTable *model;
	ETreeMemory *etmm;
	
	g_return_if_fail (IS_E_SUMMARY_SHOWN (shown));

	if (all == TRUE) {
		td = shown->priv->all;
		model = shown->all_model;
	} else {
		td = shown->priv->shown;
		model = shown->shown_model;
	}

	etmm = E_TREE_MEMORY (td->etm);
	e_tree_memory_node_remove (etmm, entry->path);

	g_hash_table_remove (model, entry->path);

	if (all == FALSE) {
		td->contents = g_slist_remove (td->contents, entry);
	}

}

static void
make_list (ETreePath path,
	   gpointer data)
{
	GList **list = data;

	*list = g_list_prepend (*list, path);
}

GList *
e_summary_shown_get_selection (ESummaryShown *shown,
			       gboolean all)
{
	ETree *et;
	ESelectionModel *esm;
	GList *list = NULL;
	
	if (all) {
		et = e_tree_scrolled_get_tree (E_TREE_SCROLLED (shown->priv->all->etable));
	} else {
		et = e_tree_scrolled_get_tree (E_TREE_SCROLLED (shown->priv->shown->etable));
	}
	
	esm = e_tree_get_selection_model (et);
	
	e_tree_selection_model_foreach (E_TREE_SELECTION_MODEL (esm),
					make_list, &list);

	return list;
}
	
			       
