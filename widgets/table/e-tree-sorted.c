/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-tree-sorted.c: a Tree Model implementation that the programmer builds in sorted.
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *   Chris Lahey <clahey@ximian.com>
 *
 * Adapted from the gtree code and ETableModel.
 *
 * (C) 2000, 2001 Ximian, Inc.
 */
#include <config.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <gnome-xml/parser.h>
#include <gnome-xml/xmlmemory.h>

#include <gtk/gtksignal.h>
#include <stdlib.h>
#include "gal/util/e-util.h"
#include "gal/util/e-xml-utils.h"
#include "e-tree-sorted.h"
#include "e-table-sorting-utils.h"

#define PARENT_TYPE E_TREE_MODEL_TYPE

#define TREEPATH_CHUNK_AREA_SIZE (30 * sizeof (ETreeSortedPath))

static ETreeModel *parent_class;
static GMemChunk  *node_chunk;

typedef struct ETreeSortedPath ETreeSortedPath;

struct ETreeSortedPath {
	ETreePath         corresponding;

	/* parent/child/sibling pointers */
	ETreeSortedPath  *parent;
	gint              num_children;
	ETreeSortedPath **children;
	int               position;
};

struct ETreeSortedPriv {
	ETreeModel      *source;
	ETreeSortedPath *root;

	ETableSortInfo   *sort_info;
	ETableHeader     *full_header;

	int          tree_model_node_changed_id;
	int          tree_model_node_data_changed_id;
	int          tree_model_node_col_changed_id;
	int          tree_model_node_inserted_id;
	int          tree_model_node_removed_id;
};



static ETreeSortedPath *
find_path(ETreeSorted *ets, ETreePath corresponding)
{
	int depth;
	ETreePath *sequence;
	int i;
	ETreeSortedPath *path;

	if (corresponding == NULL)
		return NULL;

#if 0
	if (etta->priv->last_access != -1 && etta->priv->map_table[etta->priv->last_access] == path)
		return etta->priv->last_access;
#endif

	depth = e_tree_model_node_depth(ets->priv->source, corresponding);

	sequence = g_new(ETreePath, depth + 1);

	sequence[0] = corresponding;

	for (i = 0; i < depth; i++)
		sequence[i + 1] = e_tree_model_node_get_parent(ets->priv->source, sequence[i]);

	path = ets->priv->root;

	for (i = depth - 1; i >= 0 && path != NULL; i --) {
		int j;

		if (path->num_children == -1) {
			path = NULL;
			break;
		}

		for (j = 0; j < path->num_children; j++) {
			if (path->children[j]->corresponding == sequence[i]) {
				break;
			}
		}

		if (j < path->num_children) {
			path = path->children[j];
		} else {
			path = NULL;
		}
	}
	g_free (sequence);

#if 0
	ets->priv->last_access = row;
#endif

	return path;
}

static void free_path (ETreeSortedPath *path);

static void
free_children (ETreeSortedPath *path)
{
	int i;

	if (path == NULL)
		return;

	for (i = 0; i < path->num_children; i++) {
		free_path(path->children[i]);
	}

	g_free(path->children);
	path->children = NULL;
	path->num_children = -1;
}

static void
free_path (ETreeSortedPath *path)
{
	free_children(path);
	g_free(path);
}

static ETreeSortedPath *
new_path (ETreeSortedPath *parent, ETreePath corresponding)
{
	ETreeSortedPath *path;

	path = g_new(ETreeSortedPath, 1);

	path->corresponding = corresponding;
	path->parent = parent;
	path->num_children = -1;
	path->children = NULL;
	path->position = -1;

	return path;
}

static void
reposition_path (ETreeSorted *ets, ETreeSortedPath *path)
{
	
}

static void
generate_children(ETreeSorted *ets, ETreeSortedPath *path)
{
	ETreePath child;
	int i;
	int count;

	g_free(path->children);
	path->children = NULL;

	count = 0;
	for (child = e_tree_model_node_get_first_child(ets->priv->source, path->corresponding);
	     child;
	     child = e_tree_model_node_get_next(ets->priv->source, child)) {
		count ++;
	}

	path->num_children = count;
	path->children = g_new(ETreeSortedPath *, count);
	for (child = e_tree_model_node_get_first_child(ets->priv->source, path->corresponding), i = 0;
	     i < count;
	     child = e_tree_model_node_get_next(ets->priv->source, child), i++) {
		path->children[i] = new_path(path, child);
		path->children[i]->position = i;
	}
}



/* virtual methods */

static void
ets_destroy (GtkObject *object)
{
	ETreeSorted *ets = E_TREE_SORTED (object);
	ETreeSortedPriv *priv = ets->priv;

	/* FIXME lots of stuff to free here */

	free_path(priv->root);

	if (priv->source) {
		gtk_signal_disconnect (GTK_OBJECT (priv->source),
				       priv->tree_model_node_changed_id);
		gtk_signal_disconnect (GTK_OBJECT (priv->source),
				       priv->tree_model_node_data_changed_id);
		gtk_signal_disconnect (GTK_OBJECT (priv->source),
				       priv->tree_model_node_col_changed_id);
		gtk_signal_disconnect (GTK_OBJECT (priv->source),
				       priv->tree_model_node_inserted_id);
		gtk_signal_disconnect (GTK_OBJECT (priv->source),
				       priv->tree_model_node_removed_id);

		gtk_object_unref (GTK_OBJECT (priv->source));
		priv->source = NULL;

		priv->tree_model_node_changed_id = 0;
		priv->tree_model_node_data_changed_id = 0;
		priv->tree_model_node_col_changed_id = 0;
		priv->tree_model_node_inserted_id = 0;
		priv->tree_model_node_removed_id = 0;
	}

	if (priv->sort_info)
		gtk_object_unref(GTK_OBJECT(priv->sort_info));

	if (priv->full_header)
		gtk_object_unref(GTK_OBJECT(priv->full_header));

	g_free (priv);

	GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

static ETreePath
ets_get_root (ETreeModel *etm)
{
	ETreeSortedPriv *priv = E_TREE_SORTED(etm)->priv;
	if (priv->root == NULL) {
		ETreeSorted *ets = E_TREE_SORTED(etm);
		ETreePath corresponding = e_tree_model_get_root(ets->priv->source);

		if (corresponding) {
			priv->root = new_path(NULL, corresponding);
		}
	}

	return priv->root;
}

static ETreePath
ets_get_parent (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	return path->parent;
}

static ETreePath
ets_get_first_child (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	if (path->num_children == -1)
		generate_children(ets, path);

	if (path->num_children > 0)
		return path->children[0];
	else
		return NULL;
}

static ETreePath
ets_get_last_child (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	if (path->num_children == -1)
		generate_children(ets, path);

	if (path->num_children > 0)
		return path->children[path->num_children - 1];
	else
		return NULL;
}

static ETreePath
ets_get_next (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSortedPath *parent = path->parent;
	if (parent) {
		if (parent->num_children > path->position + 1)
			return parent->children[path->position + 1];
		else
			return NULL;
	} else
		  return NULL;
}

static ETreePath
ets_get_prev (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSortedPath *parent = path->parent;
	if (parent) {
		if (path->position - 1 >= 0)
			return parent->children[path->position - 1];
		else
			return NULL;
	} else
		  return NULL;
}

static gboolean
ets_is_root (ETreeModel *etm, ETreePath node)
{
 	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_node_is_root (ets->priv->source, path->corresponding);
}

static gboolean
ets_is_expandable (ETreeModel *etm, ETreePath node)
{
 	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_node_is_expandable (ets->priv->source, path->corresponding);
}

static guint
ets_get_children (ETreeModel *etm, ETreePath node, ETreePath **nodes)
{
	ETreeSortedPath *path = node;
	guint n_children;

	n_children = path->num_children;

	if (nodes) {
		int i;

		(*nodes) = g_malloc (sizeof (ETreePath) * n_children);
		for (i = 0; i < n_children; i ++) {
			(*nodes)[i] = path->children[i];
		}
	}

	return n_children;
}

static guint
ets_depth (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_node_depth(ets->priv->source, path->corresponding);
}

static GdkPixbuf *
ets_icon_at (ETreeModel *etm, ETreePath node)
{
	ETreeSortedPath *path = node;
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_icon_at(ets->priv->source, path->corresponding);
}

static gboolean
ets_get_expanded_default (ETreeModel *etm)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_get_expanded_default(ets->priv->source);
}

static gint
ets_column_count (ETreeModel *etm)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_column_count(ets->priv->source);
}


static void *
ets_value_at (ETreeModel *etm, ETreePath node, int col)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	ETreeSortedPath *path = node;

	return e_tree_model_value_at(ets->priv->source, path->corresponding, col);
}

static void
ets_set_value_at (ETreeModel *etm, ETreePath node, int col, const void *val)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	ETreeSortedPath *path = node;

	e_tree_model_set_value_at (ets->priv->source, path->corresponding, col, val);
}

static gboolean
ets_is_editable (ETreeModel *etm, ETreePath node, int col)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	ETreeSortedPath *path = node;

	return e_tree_model_node_is_editable (ets->priv->source, path->corresponding, col);
}


/* The default for ets_duplicate_value is to return the raw value. */
static void *
ets_duplicate_value (ETreeModel *etm, int col, const void *value)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);

	return e_tree_model_duplicate_value (ets->priv->source, col, value);
}

static void
ets_free_value (ETreeModel *etm, int col, void *value)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);

	e_tree_model_free_value (ets->priv->source, col, value);
}

static void *
ets_initialize_value (ETreeModel *etm, int col)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	
	return e_tree_model_initialize_value (ets->priv->source, col);
}

static gboolean
ets_value_is_empty (ETreeModel *etm, int col, const void *value)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	
	return e_tree_model_value_is_empty (ets->priv->source, col, value);
}

static char *
ets_value_to_string (ETreeModel *etm, int col, const void *value)
{
	ETreeSorted *ets = E_TREE_SORTED(etm);
	
	return e_tree_model_value_to_string (ets->priv->source, col, value);
}



/* Proxy functions */

static void
ets_proxy_node_changed (ETreeModel *etm, ETreePath node, ETreeSorted *ets)
{
	if (e_tree_model_node_is_root(ets->priv->source, node) && ets->priv->root == NULL) {
		ets->priv->root = new_path(NULL, node);
		e_tree_model_node_changed(E_TREE_MODEL(ets), ets->priv->root);
	} else {
		ETreeSortedPath *path = find_path(ets, node);

		if (path) {
			free_children(path);
			path->children = NULL;
			path->num_children = -1;
			reposition_path(ets, path);
			e_tree_model_node_changed(E_TREE_MODEL(ets), path);
		}
	}
}

static void
ets_proxy_node_data_changed (ETreeModel *etm, ETreePath node, ETreeSorted *ets)
{
	ETreeSortedPath *path = find_path(ets, node);

	if (path) {
		reposition_path(ets, path);
		e_tree_model_node_data_changed(E_TREE_MODEL(ets), path);
	}
}

static void
ets_proxy_node_col_changed (ETreeModel *etm, ETreePath node, int col, ETreeSorted *ets)
{
	ETreeSortedPath *path = find_path(ets, node);

	if (path) {
		if (e_table_sorting_utils_affects_sort(ets->priv->sort_info, ets->priv->full_header, col))
			reposition_path(ets, path);
		e_tree_model_node_col_changed(E_TREE_MODEL(ets), path, col);
	}
}

static void
ets_proxy_node_inserted (ETreeModel *etm, ETreePath parent, ETreePath child, ETreeSorted *ets)
{
	ETreeSortedPath *parent_path = find_path(ets, parent);

	if (parent_path && parent_path->num_children != -1) {
		parent_path->num_children ++;
		parent_path->children = g_renew(ETreeSortedPath *, parent_path->children, parent_path->num_children);
		parent_path->children[parent_path->num_children - 1] = new_path(parent_path, child);
		parent_path->children[parent_path->num_children - 1]->position = parent_path->num_children - 1;
		e_tree_model_node_inserted(E_TREE_MODEL(ets), parent_path, parent_path->children[parent_path->num_children - 1]);
		reposition_path(ets, parent_path->children[parent_path->num_children - 1]);
	} else if (ets->priv->root == NULL && parent == NULL) {
		if (child) {
			ets->priv->root = new_path(NULL, child);
			e_tree_model_node_inserted(E_TREE_MODEL(ets), NULL, ets->priv->root);
		}
	}
}

static void
ets_proxy_node_removed (ETableModel *etm, ETreePath parent, ETreePath child, ETreeSorted *ets)
{
	ETreeSortedPath *parent_path = find_path(ets, parent);
	ETreeSortedPath *path = find_path(ets, child);

	if (parent_path && parent_path->num_children != -1) {
		parent_path->num_children --;
		memmove(parent_path->children + path->position, parent_path->children + path->position + 1, sizeof(ETreeSortedPath *) * (parent_path->num_children - path->position - 1));
		e_tree_model_node_removed(E_TREE_MODEL(ets), parent_path, path);
		free_path(path);
	} else if (path != NULL && path == ets->priv->root) {
		ets->priv->root = NULL;
		e_tree_model_node_removed(E_TREE_MODEL(ets), NULL, path);
		free_path(path);
	}
}



/* Initialization and creation */

static void
e_tree_sorted_class_init (GtkObjectClass *klass)
{
	ETreeModelClass *tree_class = (ETreeModelClass *) klass;

	parent_class                     = gtk_type_class (PARENT_TYPE);
	
	node_chunk                       = g_mem_chunk_create (ETreeSortedPath, TREEPATH_CHUNK_AREA_SIZE, G_ALLOC_AND_FREE);

	klass->destroy                   = ets_destroy;

	tree_class->get_root             = ets_get_root;
	tree_class->get_parent           = ets_get_parent;
	tree_class->get_first_child      = ets_get_first_child;
	tree_class->get_last_child       = ets_get_last_child;
	tree_class->get_prev             = ets_get_prev;
	tree_class->get_next             = ets_get_next;

	tree_class->is_root              = ets_is_root;
	tree_class->is_expandable        = ets_is_expandable;
	tree_class->get_children         = ets_get_children;
	tree_class->depth                = ets_depth;

	tree_class->icon_at              = ets_icon_at;

	tree_class->get_expanded_default = ets_get_expanded_default;
	tree_class->column_count         = ets_column_count;




	tree_class->value_at             = ets_value_at;
	tree_class->set_value_at         = ets_set_value_at;
	tree_class->is_editable          = ets_is_editable;

	tree_class->duplicate_value      = ets_duplicate_value;
	tree_class->free_value           = ets_free_value;
	tree_class->initialize_value     = ets_initialize_value;
	tree_class->value_is_empty       = ets_value_is_empty;
	tree_class->value_to_string      = ets_value_to_string;
}

static void
e_tree_sorted_init (GtkObject *object)
{
	ETreeSorted *ets = (ETreeSorted *)object;

	ETreeSortedPriv *priv;

	priv = g_new0 (ETreeSortedPriv, 1);
	ets->priv = priv;

	priv->root = NULL;
}

E_MAKE_TYPE(e_tree_sorted, "ETreeSorted", ETreeSorted, e_tree_sorted_class_init, e_tree_sorted_init, PARENT_TYPE)

/**
 * e_tree_sorted_construct:
 * @etree: 
 * 
 * 
 **/
void
e_tree_sorted_construct (ETreeSorted *ets, ETreeModel *source, ETableHeader *full_header, ETableSortInfo *sort_info)
{
	ets->priv->source = source;
	if (source)
		gtk_object_ref(GTK_OBJECT(source));

	ets->priv->full_header = full_header;
	if (full_header)
		gtk_object_ref(GTK_OBJECT(full_header));\

	ets->priv->sort_info = sort_info;
	if (sort_info)
		gtk_object_ref(GTK_OBJECT(sort_info));

	ets->priv->tree_model_node_changed_id = gtk_signal_connect (GTK_OBJECT (source), "node_changed",
								    GTK_SIGNAL_FUNC (ets_proxy_node_changed), ets);
	ets->priv->tree_model_node_data_changed_id = gtk_signal_connect (GTK_OBJECT (source), "node_data_changed",
									 GTK_SIGNAL_FUNC (ets_proxy_node_data_changed), ets);
	ets->priv->tree_model_node_col_changed_id = gtk_signal_connect (GTK_OBJECT (source), "node_col_changed",
									GTK_SIGNAL_FUNC (ets_proxy_node_col_changed), ets);
	ets->priv->tree_model_node_inserted_id = gtk_signal_connect (GTK_OBJECT (source), "node_inserted",
								     GTK_SIGNAL_FUNC (ets_proxy_node_inserted), ets);
	ets->priv->tree_model_node_removed_id = gtk_signal_connect (GTK_OBJECT (source), "node_removed",
								    GTK_SIGNAL_FUNC (ets_proxy_node_removed), ets);

}

/**
 * e_tree_sorted_new
 *
 * FIXME docs here.
 *
 * return values: a newly constructed ETreeSorted.
 */
ETreeSorted *
e_tree_sorted_new (ETreeModel *source, ETableHeader *full_header, ETableSortInfo *sort_info)
{
	ETreeSorted *ets;

	ets = gtk_type_new (e_tree_sorted_get_type ());

	e_tree_sorted_construct(ets, source, full_header, sort_info);

	return ets;
}

ETreePath
e_tree_sorted_view_to_model_path  (ETreeSorted    *ets,
				   ETreePath       view_path)
{
	ETreeSortedPath *path = view_path;
	if (path)
		return path->corresponding;
	else
		return NULL;
}

ETreePath
e_tree_sorted_model_to_view_path  (ETreeSorted    *ets,
				   ETreePath       model_path)
{
	ETreeSortedPath *path = find_path(ets, model_path);

	return path;
}

