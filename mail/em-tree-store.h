
#ifndef _EM_TREE_STORE_H
#define _EM_TREE_STORE_H

#include <glib-object.h>
#include <e-util/e-msgport.h>

struct _GtkTreeIter;

typedef struct _EMTreeLeaf EMTreeLeaf;
typedef struct _EMTreeNode EMTreeNode;

/* To save memory, nodes with no children are stored in leaf nodes */
struct _EMTreeLeaf {
	struct _EMTreeNode *next;
	struct _EMTreeNode *prev;
	struct _EMTreeNode *parent;

	CamelMessageInfo *info;

	unsigned int flags:4;
};

struct _EMTreeNode {
	struct _EMTreeNode *next;
	struct _EMTreeNode *prev;
	struct _EMTreeNode *parent;

	CamelMessageInfo *info;

	unsigned int flags:4;

	GSList *aux;
	EDList children;
};

#define EM_TREE_NODE_LEAF (1<<0)

typedef struct _EMTreeStore       EMTreeStore;
typedef struct _EMTreeStoreClass  EMTreeStoreClass;

typedef enum {
	EMTS_COL_MESSAGEINFO,
	EMTS_COL_SUBJECT,
	EMTS_COL_FROM,
	EMTS_COL_TO,
	EMTS_COL_DATE,
	EMTS_COL_NUMBER
} emts_col_t;

struct _emts_column_info {
	GType type;
	const char *id;
	const char *title;
};

extern struct _emts_column_info *emts_column_info;

struct _EMTreeStore
{
	GObject parent;

	EMTreeNode *root;
	guint32 stamp;
};

struct _EMTreeStoreClass
{
	GObjectClass parent_class;
};

GType em_tree_store_get_type (void);

int em_tree_store_get_iter(EMTreeStore *store, struct _GtkTreeIter *iter, const char *uid);

#endif /* _EM_TREE_STORE_H */
