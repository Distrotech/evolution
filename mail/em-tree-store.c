
#include <glib-object.h>
#include <gtk/gtktreemodel.h>

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

	EMemChunk *node_chunks;
	EMemChunk *leaf_chunks;

	EDList changes;
	pthread_mutex_t lock;

	EDList aux;
	GHashTable *uid_table;
	GHashTable *id_table;
};

static GObjectClass *emts_parent;

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

/* This only frees external references, the actual nodes are in the memchunks */
static void
emts_node_free_rec(struct _EMTreeStorePrivate *p, EMTreeNode *node)
{
	EMTreeNode *child, *nchild;

	if (node_has_children(node)) {
		child = (EMTreeNode *)node->children.head;
		nchild = child->next;
		while (nchild) {
			emts_node_free_rec(p, child);
			child = nchild;
			nchild = nchild->next;
		}
	}

	if (node->info)
		camel_folder_free_message_info(p->folder, child->info);
}

static void
emts_finalise(GObject *o)
{
	EMTreeStore *emts = (EMTreeStore *)o;
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	struct _emts_change *c;

	if (p->update_id)
		g_source_remove(p->update_id);

	camel_object_remove_event(p->folder, p->changed_id);

	pthread_mutex_destroy(&p->lock);

	while ( ( c = (struct _emts_change *)e_dlist_remhead(&p->changes)) ) {
		camel_folder_change_info_free(c->changes);
		g_free(c);
	}

	emts_node_free_rec(p, emts->root);

	e_memchunk_destroy(p->node_chunks);
	e_memchunk_destroy(p->leaf_chunks);

	g_hash_table_destroy(p->uid_table);
	g_hash_table_destroy(p->id_table);

	((GObjectClass *)emts_parent)->finalize(o);
}

static void
emts_class_init(EMTreeStoreClass *klass)
{
	((GObjectClass *)klass)->finalize = emts_finalise;

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

		emts_parent = g_type_class_ref(G_TYPE_OBJECT);
		tree_store_type = g_type_register_static(G_TYPE_OBJECT, "EMTreeStore", &tree_store_info, 0);

		g_type_add_interface_static(tree_store_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
	}

	return tree_store_type;
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

/* This removes all empty nodes */
static void
emts_prune_empty(EMTreeStore *emts, EMTreeNode *node)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	EMTreeNode *child, *next, *save;

	/* FIXME: MUST MUST MUST - Must remove the ID hash table entries for these nodes too */
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

			if ((node = g_hash_table_lookup(p->uid_table, uid))) {
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

EMTreeStore *
em_tree_store_new(CamelFolder *folder)
{
	EMTreeStore *emts = (EMTreeStore *)g_object_new(em_tree_store_get_type(), NULL);
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);
	GPtrArray *summary;
	int i;

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

	return emts;
}

int em_tree_store_get_iter(EMTreeStore *emts, GtkTreeIter *iter, const char *uid)
{
	struct _EMTreeStorePrivate *p = _PRIVATE(emts);

	iter->user_data = g_hash_table_lookup(p->uid_table, uid);
	iter->stamp = emts->stamp;

	return iter->user_data != NULL;
}
