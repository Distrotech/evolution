/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TREE_MODEL_H_
#define _E_TREE_MODEL_H_

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gal/e-table/e-table-model.h>

#define E_TREE_MODEL_TYPE        (e_tree_model_get_type ())
#define E_TREE_MODEL(o)          (GTK_CHECK_CAST ((o), E_TREE_MODEL_TYPE, ETreeModel))
#define E_TREE_MODEL_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TREE_MODEL_TYPE, ETreeModelClass))
#define E_IS_TREE_MODEL(o)       (GTK_CHECK_TYPE ((o), E_TREE_MODEL_TYPE))
#define E_IS_TREE_MODEL_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TREE_MODEL_TYPE))

typedef struct ETreePath ETreePath;
typedef struct ETreeModel ETreeModel;
typedef struct ETreeModelPriv ETreeModelPriv;
typedef struct ETreeModelClass ETreeModelClass;
typedef gint (*ETreePathCompareFunc)(ETreeModel *model, ETreePath *path1, ETreePath *path2);
typedef gboolean (*ETreePathFunc)(ETreeModel *model, ETreePath *path, gpointer data);

struct ETreeModel {
	ETableModel base;
	ETreeModelPriv *priv;
};

struct ETreeModelClass {
	ETableModelClass parent_class;

	/*
	 * Virtual methods
	 */
	ETreePath *(*get_root)      (ETreeModel *etm);

	ETreePath *(*get_parent)      (ETreeModel *etm, ETreePath* node);
	ETreePath *(*get_first_child) (ETreeModel *etm, ETreePath* node);
	ETreePath *(*get_last_child)  (ETreeModel *etm, ETreePath* node);
	ETreePath *(*get_next)        (ETreeModel *etm, ETreePath* node);
	ETreePath *(*get_prev)        (ETreeModel *etm, ETreePath* node);
	guint      (*get_children)    (ETreeModel *etm, ETreePath* node, ETreePath ***paths);

	gboolean   (*is_expanded)          (ETreeModel *etm, ETreePath* node);
	gboolean   (*is_visible)           (ETreeModel *etm, ETreePath* node);
	void       (*set_expanded)         (ETreeModel *etm, ETreePath* node, gboolean expanded);
	void       (*set_expanded_recurse) (ETreeModel *etm, ETreePath *node, gboolean expanded);
	void       (*set_expanded_level)   (ETreeModel *etm, ETreePath *node, gboolean expanded, int level);

	GdkPixbuf *(*icon_at)              (ETreeModel *etm, ETreePath* node);
	ETreePath* (*node_at_row)          (ETreeModel *etm, int row);

	/*
	 * ETable analogs
	 */
	void      *(*value_at)      (ETreeModel *etm, ETreePath* node, int col);
	void       (*set_value_at)  (ETreeModel *etm, ETreePath* node, int col, const void *val);
	gboolean   (*is_editable)   (ETreeModel *etm, ETreePath* node, int col);


	/*
	 * Signals
	 */
	void       (*node_changed)   (ETreeModel *etm, ETreePath *node);
	void       (*node_inserted)  (ETreeModel *etm, ETreePath *parent, ETreePath *inserted_node);
	void       (*node_removed)   (ETreeModel *etm, ETreePath *parent, ETreePath *removed_node);
	void       (*node_collapsed) (ETreeModel *etm, ETreePath *node);
	void       (*node_expanded)  (ETreeModel *etm, ETreePath *node, gboolean *allow_expand);

};

GtkType     e_tree_model_get_type  (void);
void        e_tree_model_construct (ETreeModel *etree);
ETreeModel *e_tree_model_new       (void);

/* tree traversal operations */
ETreePath *e_tree_model_get_root             (ETreeModel *etree);
ETreePath *e_tree_model_node_get_parent      (ETreeModel *etree, ETreePath *path);
ETreePath *e_tree_model_node_get_first_child (ETreeModel *etree, ETreePath *path);
ETreePath *e_tree_model_node_get_last_child  (ETreeModel *etree, ETreePath *path);
ETreePath *e_tree_model_node_get_next        (ETreeModel *etree, ETreePath *path);
ETreePath *e_tree_model_node_get_prev        (ETreeModel *etree, ETreePath *path);

/* node operations */
ETreePath *e_tree_model_node_insert        (ETreeModel *etree, ETreePath *parent, int position, gpointer node_data);
ETreePath *e_tree_model_node_insert_before (ETreeModel *etree, ETreePath *parent, ETreePath *sibling, gpointer node_data);
gpointer   e_tree_model_node_remove        (ETreeModel *etree, ETreePath *path);

/* node accessors */
gboolean e_tree_model_node_is_root                 (ETreeModel *etree, ETreePath *path);
gboolean e_tree_model_node_is_expandable           (ETreeModel *etree, ETreePath *path);
gboolean e_tree_model_node_is_expanded             (ETreeModel *etree, ETreePath *path);
gboolean e_tree_model_node_is_visible              (ETreeModel *etree, ETreePath *path);
void     e_tree_model_set_expanded_default         (ETreeModel *etree, gboolean expanded);
void     e_tree_model_node_set_expanded            (ETreeModel *etree, ETreePath *path, gboolean expanded);
void     e_tree_model_node_set_expanded_recurse    (ETreeModel *etree, ETreePath *path, gboolean expanded);
guint    e_tree_model_node_get_children            (ETreeModel *etree, ETreePath *path, ETreePath ***paths);
guint    e_tree_model_node_depth                   (ETreeModel *etree, ETreePath *path);
guint    e_tree_model_node_num_visible_descendents (ETreeModel *etm, ETreePath *node);
gpointer e_tree_model_node_get_data                (ETreeModel *etm, ETreePath *node);
void     e_tree_model_node_set_data                (ETreeModel *etm, ETreePath *node, gpointer node_data);

/* display oriented routines */
ETreePath *e_tree_model_node_at_row           (ETreeModel *etree, int row);
GdkPixbuf *e_tree_model_icon_of_node          (ETreeModel *etree, ETreePath *path);
int        e_tree_model_row_of_node           (ETreeModel *etree, ETreePath *path);
void       e_tree_model_root_node_set_visible (ETreeModel *etree, gboolean visible);
gboolean   e_tree_model_root_node_is_visible  (ETreeModel *etree);

/* sort routine, analogous to gtk_ctree_node_sort */
void e_tree_model_node_set_compare_function (ETreeModel *tree_model, ETreePath *node, ETreePathCompareFunc compare);
void e_tree_model_node_sort (ETreeModel *tree_model, ETreePath *node);

/*
** Routines for emitting signals on the ETreeModel
*/
void  e_tree_model_node_changed  (ETreeModel *tree_model, ETreePath *node);
void  e_tree_model_node_inserted (ETreeModel *tree_model, ETreePath *parent_node, ETreePath *inserted_node);
void  e_tree_model_node_removed  (ETreeModel *tree_model, ETreePath *parent_node, ETreePath *removed_node);
void  e_tree_model_node_collapsed (ETreeModel *tree_model, ETreePath *node);
void  e_tree_model_node_expanded  (ETreeModel *tree_model, ETreePath *node, gboolean *allow_expand);

/* expanded state saving stuff */
gboolean   e_tree_model_save_expanded_state (ETreeModel *etm, const char *filename);
gboolean   e_tree_model_load_expanded_state (ETreeModel *etm, const char *filename);
void       e_tree_model_node_set_save_id    (ETreeModel *etm, ETreePath *node, const char *id);
ETreePath* e_tree_model_node_insert_id      (ETreeModel *tree_model, ETreePath *parent_path,
					     int position, gpointer node_data, const char *save_id);

/* depth first traversal of path's descendents, calling func on each one */
void       e_tree_model_node_traverse (ETreeModel *model, ETreePath *path, ETreePathFunc func, gpointer data);

#endif /* _E_TREE_MODEL_H */
