
#include <glib-object.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkscrolledwindow.h>

#include <glib/gi18n.h>

#include <camel/camel-folder.h>

#include <e-util/e-msgport.h>
#include <e-util/e-memory.h>
#include <pthread.h>

#include "em-tree-store.h"

#define d(x)

#define EMTS(x) ((struct _EMTreeStore *)x)
#define GTK_IS_EMTS(x) (1)

#define _PRIVATE(x) (g_type_instance_get_private((GTypeInstance *)(x), em_tree_store_get_type()))

#define node_has_children(node) ((node->flags & EM_TREE_NODE_LEAF) == 0 && node->children.head != (EDListNode *)&node->children.tail)

struct _emts_change {
	struct _emts_change *next;
	struct _emts_change *priv;

	CamelFolderChangeInfo *changes;
};

struct _EMTreeStorePrivate {
	CamelFolder *folder;

	int update_id;
	int changed_id;

	int signals:1;		/* emit signals? */

	EMemChunk *node_chunks;
	EMemChunk *leaf_chunks;

	EDList changes;
	pthread_mutex_t lock;

	EDList aux;
	GHashTable *uid_table;
	GHashTable *id_table;
};

struct _emts_column_info emts_column_info[EMTS_COL_NUMBER] = {
	{ G_TYPE_POINTER, "message-info", "<<invalid>>" },
	{ G_TYPE_STRING, "subject", N_("Subject") },
	{ G_TYPE_STRING, "from", N_("From")  },
	{ G_TYPE_STRING, "to", N_("To") },
	{ G_TYPE_ULONG, "date", N_("Date") },
};

static EMTreeNode *
emts_node_alloc(struct _EMTreeStorePrivate *p)
{
	EMTreeNode *node = e_memchunk_alloc0(p->node_chunks);

	e_dlist_init(&node->children);

	return node;
}

static EMTreeNode *
emts_leaf_alloc(struct _EMTreeStorePrivate *p)
{
	return emts_node_alloc(p);
#if 0
	EMTreeNode *node = e_memchunk_alloc0(p->leaf_chunks);

	node->flags = EM_TREE_NODE_LEAF;

	return node;
#endif
}

static void
emts_node_free(struct _EMTreeStorePrivate *p, EMTreeNode *node)
{
	if (node->info)
		camel_folder_free_message_info(p->folder, node->info);

	if (node->flags & EM_TREE_NODE_LEAF)
		e_memchunk_free(p->leaf_chunks, node);
	else
		e_memchunk_free(p->node_chunks, node);
}

/* Implementation */

static GtkTreeModelFlags
emts_get_flags (GtkTreeModel *tree_model)
{
	return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint
emts_get_n_columns(GtkTreeModel *tree_model)
{
	/*EMTreeStore *emts = (EMTreeStore *) tree_model;*/

	return EMTS_COL_NUMBER;
}

static GType
emts_get_column_type(GtkTreeModel *tree_model, gint index)
{
	/*EMTreeStore *emts = (EMTreeStore *)tree_model;*/

	g_return_val_if_fail(index < EMTS_COL_NUMBER && index >= 0, G_TYPE_INVALID);

	return emts_column_info[index].type;
}

static gboolean
emts_get_iter(GtkTreeModel *tree_model, GtkTreeIter  *iter, GtkTreePath  *path)
{
	EMTreeStore *emts = (EMTreeStore *)tree_model;
	GtkTreeIter parent;
	int *indices;
	int depth, i;

	indices = gtk_tree_path_get_indices(path);
	depth = gtk_tree_path_get_depth(path);

	g_return_val_if_fail(depth > 0, FALSE);

	parent.stamp = emts->stamp;
	parent.user_data = emts->root;

	if (! gtk_tree_model_iter_nth_child (tree_model, iter, &parent, indices[0]))
		return FALSE;

	for (i = 1; i < depth; i++) {
		parent = *iter;
		if (! gtk_tree_model_iter_nth_child (tree_model, iter, &parent, indices[i]))
			return FALSE;
	}

	return TRUE;
}

/* do this because prepend_index is really inefficient */
static void
emts_calc_path(EMTreeNode *node, GtkTreePath *path)
{
	if (node->parent) {
		int i = 0;
		EMTreeNode *scan;

		emts_calc_path(node->parent, path);

		scan = (EMTreeNode *)node->parent->children.head;
		while (scan->next && scan != node) {
			i++;
			scan = scan->next;
		}

		gtk_tree_path_append_index(path, i);
	}
}

static GtkTreePath *
emts_get_path(GtkTreeModel *tree_model, GtkTreeIter  *iter)
{
	GtkTreePath *path;

	g_return_val_if_fail (iter != NULL, NULL);
	g_return_val_if_fail (iter->user_data != NULL, NULL);
	g_return_val_if_fail (iter->stamp == EMTS (tree_model)->stamp, NULL);

	path = gtk_tree_path_new();
	emts_calc_path((EMTreeNode *)iter->user_data, path);

	return path;
}

static void
emts_get_value(GtkTreeModel *tree_model, GtkTreeIter  *iter, gint column, GValue *value)
{
	EMTreeNode *node;
	/*EMTreeStore *emts = (EMTreeStore *)tree_model;*/

	g_return_if_fail(iter != NULL);
	g_return_if_fail(column < EMTS_COL_NUMBER);

	g_value_init(value, emts_column_info[column].type);

	node = (EMTreeNode *)iter->user_data;
	if (node->info == NULL) {
		switch((emts_col_t)column) {
		case EMTS_COL_MESSAGEINFO:
			g_value_set_pointer(value, node->info);
			break;
		case EMTS_COL_SUBJECT:
		case EMTS_COL_FROM:
		case EMTS_COL_TO:
		case EMTS_COL_DATE:
			g_value_set_string(value, "<unset>");
			break;
		case EMTS_COL_NUMBER:
			abort();
			break;
		}
	} else {
		switch((emts_col_t)column) {
		case EMTS_COL_MESSAGEINFO:
			/* FIXME: need to ref the info? */
			g_value_set_pointer(value, node->info);
			break;
		case EMTS_COL_SUBJECT:
			g_value_set_string(value, camel_message_info_subject(node->info));
			break;
		case EMTS_COL_FROM:
			g_value_set_string(value, camel_message_info_from(node->info));
			break;
		case EMTS_COL_TO:
			g_value_set_string(value, camel_message_info_from(node->info));
			break;
		case EMTS_COL_DATE:
			g_value_set_ulong(value, (unsigned long)node->info->date_sent);
			break;
		case EMTS_COL_NUMBER:
			abort();
			break;
		}
	}
}

static gboolean
emts_iter_next(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	EMTreeNode *node;

	g_return_val_if_fail (iter != NULL, FALSE);
	g_return_val_if_fail (iter->user_data != NULL, FALSE);

	/* FIXME: check stamp? */

	/* TODO: how can we ignore placeholder nodes but keep them around anyway??? */

	node = iter->user_data;
	node = node->next;
	if (node->next) {
		iter->user_data = node;
		return TRUE;
	}

	return FALSE;
}

static gboolean
emts_iter_child (GtkTreeModel *tree_model, GtkTreeIter  *iter, GtkTreeIter  *parent)
{
	EMTreeNode *node;

	/* FIXME: check stamp? */

	if (parent)
		node = parent->user_data;
	else
		node = ((EMTreeStore *)tree_model)->root;

	if (node_has_children(node)) {
		iter->stamp = EMTS (tree_model)->stamp;
		iter->user_data = node->children.head;
		return TRUE;
	}

	return FALSE;
}

static gboolean
emts_iter_has_child (GtkTreeModel *tree_model, GtkTreeIter  *iter)
{
	EMTreeNode *node;

	/* FIXME: check stamp? */

	node = iter->user_data;

	return node_has_children(node);
}

static gint
emts_iter_n_child(GtkTreeModel *tree_model, GtkTreeIter  *iter)
{
	EMTreeNode *node;
	int i = 0;

	/* FIXME: check stamp? */

	if (iter == NULL)
		node = ((EMTreeStore *)tree_model)->root;
	else
		node = iter->user_data;

	if (node_has_children(node)) {
		node = (EMTreeNode *)node->children.head;
		while (node->next) {
			i++;
			node = node->next;
		}
	}

	return i;
}

static gboolean
emts_iter_nth_child(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *parent, gint n)
{
	EMTreeNode *node;

	printf("emts_iter_nth_child(%d)\n", n);

	if (parent)
		node = parent->user_data;
	else
		node = ((EMTreeStore *)tree_model)->root;

	if (node_has_children(node)) {
		node = (EMTreeNode *)node->children.head;
		while (node->next) {
			if (n == 0) {
				iter->user_data = node;
				return TRUE;
			}
			n--;
			node = node->next;
		}
	}

	return FALSE;
}

static gboolean
emts_iter_parent(GtkTreeModel *tree_model, GtkTreeIter *iter, GtkTreeIter *child)
{
	EMTreeNode *parent;

	parent = ((EMTreeNode *)child->user_data)->parent;

	if (parent != ((EMTreeStore *)tree_model)->root) {
		iter->user_data = parent;
		/*iter->stamp = EMTS (tree_model)->stamp;*/
		return TRUE;
	}

	return FALSE;
}

static void
emts_class_init(EMTreeStoreClass *klass)
{
	g_type_class_add_private(klass, sizeof(struct _EMTreeStorePrivate));
}

static guint
emts_id_hash(void *key)
{
	CamelSummaryMessageID *id = (CamelSummaryMessageID *)key;

	return id->id.part.lo;
}

static gint
emts_id_equal(void *a, void *b)
{
	return ((CamelSummaryMessageID *)a)->id.id == ((CamelSummaryMessageID *)b)->id.id;
}

static void
emts_init(EMTreeStore *emts)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);

	p->uid_table = g_hash_table_new(g_str_hash, g_str_equal);
	p->id_table = g_hash_table_new((GHashFunc)emts_id_hash, (GCompareFunc)emts_id_equal);

	pthread_mutex_init(&p->lock, NULL);
	e_dlist_init(&p->changes);
	e_dlist_init(&p->aux);

	p->node_chunks = e_memchunk_new(64, sizeof(EMTreeNode));
	p->leaf_chunks = e_memchunk_new(64, sizeof(EMTreeLeaf));

	emts->root = emts_node_alloc(p);
}

static void
emts_tree_model_init(GtkTreeModelIface *iface)
{
	iface->get_flags = emts_get_flags;
	iface->get_n_columns = emts_get_n_columns;
	iface->get_column_type = emts_get_column_type;
	iface->get_iter = emts_get_iter;
	iface->get_path = emts_get_path;
	iface->get_value = emts_get_value;
	iface->iter_next = emts_iter_next;
	iface->iter_children = emts_iter_child;
	iface->iter_has_child = emts_iter_has_child;
	iface->iter_n_children = emts_iter_n_child;
	iface->iter_nth_child = emts_iter_nth_child;
	iface->iter_parent = emts_iter_parent;
}

GType
em_tree_store_get_type (void)
{
	static GType tree_store_type = 0;

	if (!tree_store_type) {
		static const GTypeInfo tree_store_info = {
			sizeof (EMTreeStoreClass),
			NULL, NULL,
			(GClassInitFunc) emts_class_init,
			NULL, NULL,
			sizeof (EMTreeStore), 0,
			(GInstanceInitFunc) emts_init,
		};

		static const GInterfaceInfo tree_model_info = {
			(GInterfaceInitFunc) emts_tree_model_init,
			NULL,
			NULL
		};

		tree_store_type = g_type_register_static(G_TYPE_OBJECT, "EMTreeStore", &tree_store_info, 0);

		g_type_add_interface_static(tree_store_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
	}

	return tree_store_type;
}

EMTreeStore *
em_tree_store_new(CamelFolder *folder)
{
	EMTreeStore *emts = (EMTreeStore *)g_object_new(em_tree_store_get_type(), NULL);
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);

	p->folder = folder;
	camel_object_ref(folder);
	p->changed_id = camel_object_hook_event(folder, "folder_changed", emts_folder_changed, emts);

	summary = camel_folder_get_summary(folder);
	for (i=0;i<summary->len;i++) {
		CamelMessageInfo *mi = summary->pdata[i];

		camel_folder_ref_message_info(folder, mi);
		emts_insert_info_base(emts, mi);
	}
	camel_folder_free_summary(folder, summary);

	emts_prune_empty(emts, (EMTreeNode *)emts->root->children.head);

	/* TBD */
	p->signals = TRUE;

	return emts;
}

int em_tree_store_get_iter(EMTreeStore *emts, GtkTreeIter *iter, const char *uid)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);

	iter->user_data = g_hash_table_lookup(p->uid_table, uid);
	iter->stamp = emts->stamp;

	return iter->user_data != NULL;
}

#if d(!)0
static void
dump_info_rec(EMTreeNode *node, GString *pre)
{
	int len = pre->len;

	g_string_append(pre, "  ");
	printf("%p: %s%s\n", node, pre->str, node->info?camel_message_info_subject(node->info):"<unset>");
	if (node_has_children(node)) {
		node = node->children.head;
		while (node->next) {
			dump_info_rec(node, pre);
			node = node->next;
		}
	}
	g_string_truncate(pre, len);
}

static void
dump_info(EMTreeNode *root)
{
	GString *pre = g_string_new("");

	dump_info_rec(root, pre);
	g_string_free(pre, TRUE);
}
#endif

static void
emts_insert_info_old(EMTreeStore *emts, CamelMessageInfo *mi)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *c, *match, *node, *parent;
	int j;
	GtkTreeIter iter;
	GtkTreePath *path;

	if (mi->message_id.id.id) {
		match = g_hash_table_lookup(p->id_table, &mi->message_id);
	} else {
		match = NULL;
	}

	if (mi->references) {
		parent = emts->root;

		/* FIXME: Going in this direction means that if a new parent is discovered
		   for the parent, it isn't moved.  So it needs to create the node first,
		   then go up the parents */

		/* make sure the tree from root to the message exists */
		for (j=mi->references->size-1;j>=0;j--) {
			/* should never be empty, but just incase */
			if (mi->references->references[j].id.id == 0)
				continue;

			node = g_hash_table_lookup(p->id_table, &mi->references->references[j]);
			d(printf("looking up reference = %p\n", node));
			if (node == parent || (node && node->parent == parent)) {
				if (node == parent)
					d(printf(" same as parent?\n"));
				else
					d(printf(" parent is same\n"));
				parent = node;
				continue;
			}

			if (node == NULL) {
				/* need to make a place-holder parent node */

				d(printf(" placeholder\n"));
				node = emts_node_alloc(p);
				g_hash_table_insert(p->id_table, &mi->references->references[j], node);
			} else {
				/* re-parent the node based on the updated information */

				d(printf(" new parent\n"));

				if (p->signals) {
					iter.user_data = node;
					iter.stamp = emts->stamp;
					path = emts_get_path((GtkTreeModel *)emts, &iter);
					e_dlist_remove((EDListNode *)node);
					gtk_tree_model_row_deleted((GtkTreeModel *)emts, path);
					gtk_tree_path_free(path);
				} else {
					e_dlist_remove((EDListNode *)node);
				}

				/* do we need to promote a leaf node to a full node? */
				if (node->flags & EM_TREE_NODE_LEAF) {
					g_hash_table_remove(p->id_table, &mi->references->references[j]);
					c = emts_node_alloc(p);
					c->info = node->info;
					emts_node_free(p, node);
					node = c;
					g_hash_table_insert(p->id_table, &mi->references->references[j], node);
					if (node->info)
						g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(node->info), node);
				}
			}

			d(printf("  insert placeholder %p parent %p\n", node, parent));

			node->parent = parent;
			e_dlist_addtail(&parent->children, (EDListNode *)node);
			parent = node;

			if (p->signals) {
				iter.user_data = node;
				iter.stamp = emts->stamp;
				path = emts_get_path((GtkTreeModel *)emts, &iter);
				gtk_tree_model_row_inserted((GtkTreeModel *)emts, path, &iter);
				gtk_tree_path_free(path);
			}
		}
	} else {
		/* no references, must be a root node - for now */
		parent = emts->root;
	}

	if (match && match->info == NULL) {
		/* we're filling in a dummy node previously made */
		c = match;
	} else {
		match = NULL;

		/* else we're creating a new one */
		c = emts_leaf_alloc(p);
		if (mi->message_id.id.id)
			g_hash_table_insert(p->id_table, &mi->message_id, c);

		/* promote if need be */
		if (parent->flags & EM_TREE_NODE_LEAF) {
			node = emts_node_alloc(p);
			node->next = parent->next;
			node->prev = parent->prev;
			node->info = parent->info;
			node->next->prev = node;
			node->prev->next = node;
			emts_node_free(p, parent);
			parent = node;
			if (parent->info) {
				if (parent->info->message_id.id.id)
					g_hash_table_insert(p->id_table, &parent->info->message_id.id.id, parent);
				g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(parent->info), parent);
			}
		}

		c->parent = parent;
		e_dlist_addtail(&parent->children, (EDListNode *)c);
	}

	d(printf("insert %p parent %p\n", c, parent));
		
	c->info = mi;
	g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(mi), c);

	if (!match && p->signals) {
		iter.user_data = c;
		iter.stamp = emts->stamp;
		path = emts_get_path((GtkTreeModel *)emts, &iter);
		gtk_tree_model_row_inserted((GtkTreeModel *)emts, path, &iter);
		gtk_tree_path_free(path);
	}
}

static void
emts_reparent_empty(EMTreeNode *parent, EMTreeNode *node)
{
	GSList *l;

	/* FIXME: need to know if child is still alive */
	/* Refcount? */

	printf(" reparenting children to '%s'\n", camel_message_info_subject(parent->info));
	/* 'reparent' any awaiting children */
	for (l = node->aux;l;l = g_slist_next(l)) {
		EMTreeNode *child = l->data;
		
		if (child->info) {
			printf("  %p: child '%s'\n", child, camel_message_info_subject(child->info));
			e_dlist_remove((EDListNode *)node);
			e_dlist_addtail(&parent->children, (EDListNode *)node);
			node->parent = parent;
		} else {
			emts_reparent_empty(parent, child);
		}
	}

	if (node == parent) {
		g_slist_free(node->aux);
		node->aux = NULL;
	}
}

/* experimental one which is supposed to work incrementally all the time, but it doesn't */
static void
emts_insert_info_fails(EMTreeStore *emts, CamelMessageInfo *mi)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *match;
	int j;
#if 0
	GtkTreeIter iter;
	GtkTreePath *path;
#endif

	printf("inserting message '%s'\n", camel_message_info_subject(mi));

	if (mi->message_id.id.id
	    && (match = g_hash_table_lookup(p->id_table, &mi->message_id))
	    && match->info == NULL) {
		/* We already have this node, just fill it in */
		printf("%p: filling in node\n", match);
		match->info = mi;
		emts_reparent_empty(match, match);
		e_dlist_remove((EDListNode *)match);
		e_dlist_addtail(&match->parent->children, (EDListNode *)match);
	} else {
		/* Allocating a new node, always 'parent' to root to start with */
		match = emts_node_alloc(p);
		printf("%p: creating new root node\n", match);
		match->info = mi;
		if (mi->message_id.id.id)
			g_hash_table_insert(p->id_table, &mi->message_id, match);
		match->parent = emts->root;
		e_dlist_addtail(&emts->root->children, (EDListNode *)match);
	}

	g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(mi), match);

	/* FIXME: Need to emit appropriate signals
	   This may be very difficult to calculate accurately and efficiently */

	if (mi->references) {
		EMTreeNode *node, *parent;

		node = match;

		/* make sure the tree from root to the message exists */
		for (j=0;j<mi->references->size;j++) {
			/* should never be empty, but just incase */
			if (mi->references->references[j].id.id == 0)
				continue;

			parent = g_hash_table_lookup(p->id_table, &mi->references->references[j]);
			if (parent == NULL || parent->info == NULL) {
				/* create placeholder, it isn't in the tree */
				if (parent == NULL) {
					parent = emts_node_alloc(p);
					printf("%p: creating placeholder\n", parent);
					g_hash_table_insert(p->id_table, &mi->references->references[j], parent);
					parent->parent = emts->root;
					e_dlist_addtail(&p->aux, (EDListNode *)parent);
				}
				if (j == 0)
					parent->aux = g_slist_append(parent->aux, node);
			} else if (parent != node) {
				printf("reparenting under %s\n", camel_message_info_subject(parent->info));
				/* parent changed, then re-parent to the correct one */
				if (node->parent != parent) {
					node->parent = parent;
					e_dlist_remove((EDListNode *)node);
					e_dlist_addtail(&parent->children, (EDListNode *)node);
				}
				node = parent;
			}
		}
	} else if (FALSE /* etms->thread_subject */) {
#if 0
		if (is_re(mi)) {
			sub = strip_re(mi);
			if (lookup(re_root)) {
				reparent(match);
			}
		}
#endif
	}
}

/* This is used along with prune_empty to build the initial model, before the model is set on any tree
   It should match the old thread algorithm exactly */
static void
emts_insert_info_base(EMTreeStore *emts, CamelMessageInfo *mi)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *match;
	int j;

	if (mi->message_id.id.id
	    && (match = g_hash_table_lookup(p->id_table, &mi->message_id))
	    && match->info == NULL) {
		/* We already have this node, just fill it in */
		match->info = mi;
	} else {
		/* Allocating a new node, always 'parent' to root to start with */
		match = emts_node_alloc(p);
		match->info = mi;
		if (mi->message_id.id.id
		    && g_hash_table_lookup(p->id_table, &mi->message_id) == NULL)
			g_hash_table_insert(p->id_table, &mi->message_id, match);
		match->parent = emts->root;
		e_dlist_addtail(&emts->root->children, (EDListNode *)match);
	}

	g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(mi), match);

	if (mi->references) {
		EMTreeNode *node, *parent;

		node = match;

		/* make sure the tree from root to the message exists */
		for (j=0;j<mi->references->size;j++) {
			/* should never be empty, but just incase */
			if (mi->references->references[j].id.id == 0)
				continue;

			/* If no existing parent, create one, always put it under root to start with */
			parent = g_hash_table_lookup(p->id_table, &mi->references->references[j]);
			if (parent == NULL) {
				parent = emts_node_alloc(p);
				g_hash_table_insert(p->id_table, &mi->references->references[j], parent);
				parent->parent = emts->root;
				e_dlist_addtail(&emts->root->children, (EDListNode *)parent);
			}

			/* parent changed, then re-parent to the correct one */
			if (parent != node) {
				if (node->parent != parent) {
					node->parent = parent;
					e_dlist_remove((EDListNode *)node);
					e_dlist_addtail(&parent->children, (EDListNode *)node);
				}
				node = parent;
			}
		}
	}
}

/* This is used to incrementally update the model as changes come in.
   It takes some short-cuts since we no longer have the auxillairy place-holders
   to fill out the full root tree.  It will work if messages arrive in the right order however */
static void
emts_insert_info_incr(EMTreeStore *emts, CamelMessageInfo *mi)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *match;
	int j;
	GtkTreeIter iter;
	GtkTreePath *path;

	/* TODO: emit signals */

	printf("inserting new: '%s'\n", camel_message_info_subject(mi));

	/* Allocating a new node, always 'parent' to root to start with */
	match = emts_node_alloc(p);
	match->info = mi;
	if (mi->message_id.id.id
	    && g_hash_table_lookup(p->id_table, &mi->message_id) == NULL)
		g_hash_table_insert(p->id_table, &mi->message_id, match);
	match->parent = emts->root;
	e_dlist_addtail(&emts->root->children, (EDListNode *)match);

	g_hash_table_insert(p->uid_table, (void *)camel_message_info_uid(mi), match);

	if (mi->references) {
		EMTreeNode *node, *parent;

		node = match;

		/* Search for a parent, if we have one already - otherwise it stays at the root */
		for (j=0;j<mi->references->size;j++) {
			if (mi->references->references[j].id.id != 0
			    && (parent = g_hash_table_lookup(p->id_table, &mi->references->references[j]))
			    && parent != node) {
				printf(" found parent, reparenting\n");
				node->parent = parent;
				e_dlist_remove((EDListNode *)node);
				e_dlist_addtail(&parent->children, (EDListNode *)node);
				break;
			}
		}
	}

	iter.user_data = match;
	iter.stamp = emts->stamp;
	path = emts_get_path((GtkTreeModel *)emts, &iter);
	gtk_tree_model_row_inserted((GtkTreeModel *)emts, path, &iter);
	printf("Inserted at: %s\n", gtk_tree_path_to_string(path));
	gtk_tree_path_free(path);
}

static void
emts_prune_empty(EMTreeStore *emts, EMTreeNode *node)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *child, *next, *save;
	
	next = node->next;
	while (next) {
		if (node->info == NULL) {
			if (!node_has_children(node)) {
				e_dlist_remove((EDListNode *)node);
				emts_node_free(p, node);
				node = next;
				next = node->next;
			} else {
				while ((child = (EMTreeNode *)e_dlist_remhead(&node->children))) {
					child->next = next;
					child->prev = next->prev;
					next->prev->next = child;
					next->prev = child;
					child->parent = node->parent;
				}
				save = node;
				node = node->next;
				next = node->next;
				e_dlist_remove((EDListNode *)save);
				emts_node_free(p, save);
			}
		} else {
			if (node_has_children(node))
				emts_prune_empty(emts, (EMTreeNode *)node->children.head);
			node = next;
			next = node->next;
		}
	}
}

static void
emts_remove_uid(EMTreeStore *emts, const char *uid)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *c, *d, *node;
	GtkTreePath *path = NULL, *tpath;
	GtkTreeIter iter;

	node = g_hash_table_lookup(p->uid_table, uid);
	if (node == NULL)
		return;

	g_hash_table_remove(p->uid_table, uid);

	iter.stamp = emts->stamp;
	iter.user_data = node;
	path = emts_get_path((GtkTreeModel *)emts, &iter);
	e_dlist_remove((EDListNode *)node);
	gtk_tree_model_row_deleted((GtkTreeModel*)emts, path);

	if (node_has_children(node)) {
		c = (EMTreeNode *)e_dlist_remhead(&node->children);
		c->prev = node->prev;
		node->prev->next = c;
		c->next = node->next;
		node->next->prev = c;
		c->parent = node->parent;

		iter.stamp = emts->stamp;
		iter.user_data = node;
		tpath = emts_get_path((GtkTreeModel *)emts, &iter);
		gtk_tree_model_row_inserted((GtkTreeModel*)emts, tpath, &iter);
		gtk_tree_path_free(tpath);

		while ( (d = (EMTreeNode *)e_dlist_remhead(&node->children)) ) {
			d->parent = c;
			e_dlist_addtail(&c->children, (EDListNode *)d);

			iter.stamp = emts->stamp;
			iter.user_data = d;
			tpath = emts_get_path((GtkTreeModel *)emts, &iter);
			gtk_tree_model_row_inserted((GtkTreeModel*)emts, tpath, &iter);
			gtk_tree_path_free(tpath);
		}
	}

	if (!node_has_children(node->parent)) {
		gtk_tree_path_up(path);
		iter.user_data = node->parent;
		gtk_tree_model_row_has_child_toggled((GtkTreeModel*)emts, path, &iter);
	}
	gtk_tree_path_free(path);

	emts_node_free(p, node);
}

static gboolean
emts_folder_changed_idle(void *data)
{
	EMTreeStore *emts = data;
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	struct _emts_change *c;
	int i;

	pthread_mutex_lock(&p->lock);
	while ( ( c = (struct _emts_change *)e_dlist_remhead(&p->changes)) ) {
		pthread_mutex_unlock(&p->lock);

		for (i=0;i<c->changes->uid_added->len;i++) {
			const char *uid = c->changes->uid_added->pdata[i];
			CamelMessageInfo *mi;

			if (g_hash_table_lookup(p->uid_table, uid) == NULL
			    && (mi = camel_folder_get_message_info(p->folder, uid)))
				emts_insert_info_incr(emts, mi);
		}

		for (i=0;i<c->changes->uid_removed->len;i++) {
			const char *uid = c->changes->uid_removed->pdata[i];

			emts_remove_uid(emts, uid);
		}

		for (i=0;i<c->changes->uid_changed->len;i++) {
			const char *uid = c->changes->uid_changed->pdata[i];
			EMTreeNode *node;

			if (p->signals
			    && (node = g_hash_table_lookup(p->uid_table, uid))) {
				GtkTreeIter iter;
				GtkTreePath *path;

				iter.stamp = emts->stamp;
				iter.user_data = node;

				path = emts_get_path((GtkTreeModel*)emts, &iter);
				gtk_tree_model_row_changed((GtkTreeModel*)emts, path, &iter);
				gtk_tree_path_free(path);
			}
		}

		camel_folder_change_info_free(c->changes);
		g_free(c);
		pthread_mutex_lock(&p->lock);
	}

	p->update_id = 0;
	pthread_mutex_unlock(&p->lock);

	return FALSE;
}

static void
emts_folder_changed(CamelObject *o, void *event, void *data)
{
	EMTreeStore *emts = data;
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	struct _emts_change *c;

	c = g_malloc0(sizeof(*c));
	if (event) {
		c->changes = camel_folder_change_info_new();
		camel_folder_change_info_cat(c->changes, (CamelFolderChangeInfo *)event);
	}
	pthread_mutex_lock(&p->lock);
	e_dlist_addtail(&p->changes, (EDListNode *)c);
	if (p->update_id == 0)
		p->update_id = g_idle_add(emts_folder_changed_idle, emts);
	pthread_mutex_unlock(&p->lock);
}

struct _TreeData {
	GtkTreeView *tree;

	guint state_timeout;
	char *state_filename;

	GtkCellRenderer *text_renderer;

	char *expanded_filename;
	FILE *expanded_row_file; /* for i/o */
};

static void emtv_column_notify(GObject *o, GParamSpec *spec, struct _TreeData *td);

static void
emtv_cell_date(GtkTreeViewColumn *col, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	struct _TreeData *td = data;
	CamelMessageInfo *mi = NULL;
	time_t nowdate = time(NULL);
	struct tm then, now;
	char buf[26];
	PangoWeight weight = PANGO_WEIGHT_NORMAL;

	gtk_tree_model_get(model, iter, EMTS_COL_MESSAGEINFO, &mi, -1);

	if (mi) {
		if ((mi->flags & CAMEL_MESSAGE_SEEN) ==0)
			weight = PANGO_WEIGHT_BOLD;

		localtime_r(&mi->date_received, &then);
		localtime_r(&nowdate, &now);

		if (then.tm_year == now.tm_year) {
			e_utf8_strftime_fix_am_pm (buf, 26, _("%b %d %l:%M %p"), &then);
		} else {
			e_utf8_strftime_fix_am_pm (buf, 26, _("%b %d %Y"), &then);
		}
		g_object_set(cell, "text", buf, "weight", weight, NULL);
	} else {
		g_object_set(cell, "text", "???", "weight", weight, NULL);
	}
}

static void
emtv_cell_properties(GtkTreeViewColumn *col, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	struct _TreeData *td = data;
	CamelMessageInfo *mi = NULL;
	PangoWeight weight = PANGO_WEIGHT_NORMAL;
	const char *colour = NULL;
	gboolean strike = FALSE;

	gtk_tree_model_get(model, iter, EMTS_COL_MESSAGEINFO, &mi, -1);

	if (mi) {
		if ((mi->flags & CAMEL_MESSAGE_SEEN) == 0)
			weight = PANGO_WEIGHT_BOLD;
		if ((mi->flags & (CAMEL_MESSAGE_DELETED|CAMEL_MESSAGE_JUNK)) != 0)
			strike = TRUE;

		/* Priority: colour tag; label tag; important flag; due-by tag */
		
		colour = camel_tag_get((CamelTag **)&mi->user_tags, "colour");
		if (colour == NULL) {
			const char *label;

			if ((label = camel_tag_get((CamelTag **)&mi->user_tags, "label")) == NULL
			    || (colour = mail_config_get_label_color_by_name(label)) == NULL) {
				if (mi->flags & CAMEL_MESSAGE_FLAGGED) {
					colour = "#A7453E";
				} else {
					const char *due, *completed;

					due = camel_tag_get((CamelTag **)&mi->user_tags, "due-by");
					completed = camel_tag_get((CamelTag **)&mi->user_tags, "completed-on");
					if (due && due[0] && !(completed && completed[0])) {
						time_t now = time (NULL);
						time_t target;
				
						target = camel_header_decode_date(due, NULL);
						if (now >= target)
							colour = "#A7453E";
					}
				}
			}
		}

		
	}

	g_object_set(cell, "weight", weight, "foreground", colour, "strikethrough", strike, NULL);
}

static GtkTreeViewColumn *
emtv_add_column(struct _TreeData *td, int id, int width)
{
	GtkTreeViewColumn *col;

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, _(emts_column_info[id].title));
	switch (id) {
	case EMTS_COL_SUBJECT:
	case EMTS_COL_FROM:
		gtk_tree_view_column_pack_start(col, td->text_renderer, TRUE);
		gtk_tree_view_column_set_attributes(col, td->text_renderer, "text", id, NULL);
		gtk_tree_view_column_set_cell_data_func(col, td->text_renderer, emtv_cell_properties, td, NULL);
		break;
	case EMTS_COL_DATE:
		gtk_tree_view_column_pack_start(col, td->text_renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func(col, td->text_renderer, emtv_cell_date, td, NULL);
		break;
	default:
		abort();
	}

	g_object_set(col, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, "fixed_width", width, "resizable", TRUE, NULL);
	g_object_set_data(col, "column-id", emts_column_info[id].id);
	gtk_tree_view_append_column(td->tree, col);
	if (id == EMTS_COL_SUBJECT)
		gtk_tree_view_set_expander_column(td->tree, col);
	g_signal_connect(col, "notify", G_CALLBACK(emtv_column_notify), td);

	return col;
}

static int
emtv_load_state(struct _TreeData *td)
{
	xmlDocPtr doc;
	int res = -1;
	xmlNodePtr node;

	doc = xmlParseFile(td->state_filename);
	if (doc == NULL)
		return -1;

	node = xmlDocGetRootElement(doc);
	if (node == NULL
	    || strcmp(node->name, "em-tree-state") != 0)
		goto fail;

	node = node->children;
	while (node) {
		if (!strcmp(node->name, "column")) {
			char *tmp;
			int id, width;

			tmp = xmlGetProp(node, "id");
			if (tmp == NULL)
				goto fail;
			for (id=0;id<EMTS_COL_NUMBER;id++)
				if (!strcmp(emts_column_info[id].id, tmp))
					break;
			xmlFree(tmp);
			if (id == EMTS_COL_NUMBER)
				goto fail;

			tmp = xmlGetProp(node, "width");
			if (tmp) {
				width = strtoul(tmp, NULL, 10);
				xmlFree(tmp);
			} else
				width = 150;

			emtv_add_column(td, id, width);
		}
		node = node->next;
	}

	res = 0;
fail:
	xmlFreeDoc(doc);

	return res;
}

static void
emtv_load_state_default(struct _TreeData *td)
{
	GtkTreeViewColumn *col;

	/* TODO: Remove columns? */

	/* TODO: Base column sizes on window size */

	emtv_add_column(td, EMTS_COL_SUBJECT, 300);
	emtv_add_column(td, EMTS_COL_FROM, 300);
	emtv_add_column(td, EMTS_COL_DATE, 100);
}

static void
emtv_save_expanded_row(GtkTreeView *tree, GtkTreePath *path, void *data)
{
	struct _TreeData *td = data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	const char *uid;
	CamelMessageInfo *mi;

	model = gtk_tree_view_get_model(tree);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, EMTS_COL_MESSAGEINFO, &mi, -1);
	if (mi && (uid = camel_message_info_uid(mi)))
		fprintf(td->expanded_row_file, "%s\n", uid);
}

static gboolean
emtv_save_state_timeout(struct _TreeData *td)
{
	xmlDocPtr doc;
	xmlNodePtr node, root;
	GList *columns, *l;
	xmlChar *xmlbuf;
	int size;

	td->state_timeout = 0;

	doc = xmlNewDoc("1.0");
	root = xmlNewDocNode(doc, NULL, "em-tree-state", NULL);
	xmlDocSetRootElement(doc, root);

	columns = gtk_tree_view_get_columns(td->tree);
	for (l=columns;l;l=g_list_next(l)) {
		GtkTreeViewColumn *col = l->data;
		char num[16];

		node = xmlNewChild(root, NULL, "column", NULL);
		xmlSetProp(node, "id", g_object_get_data(col, "column-id"));
		sprintf(num, "%d", gtk_tree_view_column_get_width(col));
		xmlSetProp(node, "width", num);
	}
	g_list_free(columns);

	xmlDocDumpFormatMemory(doc, &xmlbuf, &size, TRUE);
	if (size > 0) {
		printf("Saving tree state:\n");
		fwrite(xmlbuf, 1, size, stdout);
		xmlFree(xmlbuf);
	}

	e_xml_save_file(td->state_filename, doc);

	xmlFreeDoc(doc);

	/* TODO: not here */
	td->expanded_row_file = fopen(td->expanded_filename, "w");
	gtk_tree_view_map_expanded_rows(td->tree, emtv_save_expanded_row, td);
	fclose(td->expanded_row_file);

	return FALSE;
}

static void
emtv_column_notify(GObject *o, GParamSpec *spec, struct _TreeData *td)
{
	if (td->state_timeout)
		g_source_remove(td->state_timeout);

	td->state_timeout = g_timeout_add(500, emtv_save_state_timeout, td);
}

GtkWidget *
em_tree_store_get_widget(CamelFolder *folder)
{
	GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	EMTreeStore *model;
	GtkTreeView *tree;
	GtkWidget *widget;
	GtkTreeViewColumn *col;
	struct _EMTreeStorePrivate *p;
	GPtrArray *summary;
	int i;
	struct _TreeData *td;
	FILE *fp;

	model = em_tree_store_new();

	d(dump_info(model->root));

	tree = (GtkTreeView *) gtk_tree_view_new_with_model ((GtkTreeModel *) model);
	gtk_widget_show((GtkWidget *)tree);

	widget = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (widget), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (widget), GTK_SHADOW_IN);
	gtk_container_add((GtkContainer *)widget, (GtkWidget *)tree);
	gtk_widget_show(widget);

	td = g_malloc0(sizeof(*td));
	td->tree = tree;
	td->state_filename = "/home/notzed/.evolution/.emt-state";
	td->expanded_filename = "/home/notzed/.evolution/.emt-expanded";
	td->text_renderer = gtk_cell_renderer_text_new();

	if (emtv_load_state(td) == -1)
		emtv_load_state_default(td);

	/* TODO: move to func */
	fp = fopen(td->expanded_filename, "r");
	if (fp) {
		char line[1024], *p;

		while (fgets(line, sizeof(line), fp)) {
			GtkTreeIter iter;

			p = strchr(line, '\n');
			if (p)
				*p = 0;

			printf("setting expanded: %s\n", line);

			if (em_tree_store_get_iter(model, &iter, line)) {
				GtkTreePath *path;

				path = gtk_tree_model_get_path(model, &iter);
				if (path) {
					gtk_tree_view_expand_row(tree, path, FALSE);
					gtk_tree_path_free(path);
				}
			}
		}

		fclose(fp);
	}

	return widget;
}

void
em_tree_store_test(CamelFolder *folder)
{
	GtkWidget *window, *emts;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	emts = em_tree_store_get_widget(folder);
	gtk_container_add((GtkContainer *)window, emts);
	gtk_widget_show_all(window);
}
