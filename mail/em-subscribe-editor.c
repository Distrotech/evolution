
#include <gtk/gtktreeview.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtkcellrenderertext.h>

#include "em-subscribe-editor.h"
#include "mail-config.h"
#include "mail-mt.h"
#include "mail-ops.h"

struct _EMSubscribeEditorPrivate {
	int dummy;
};

typedef struct _EMSubscribe EMSubscribe;

struct _EMSubscribe {
	struct _EMSubscribe *next;
	struct _EMSubscribe *prev;

	int ref_count;
	int cancel;

	struct _EMSubscribeEditor *editor; /* parent object*/

	char *store_uri;
	int store_id;		/* looking up a store */

	CamelStore *store;
	GHashTable *folders;

	GtkWidget *widget;	/* widget to show for this store */
	GtkTreeView *tree;	/* tree, if we have it */

	/* list of all returns from get_folder_info, accessed by other structures */
	GSList *info_list;

	/* pending LISTs, EMSubscribeNode's */
	int pending_id;
	EDList pending;
	
	/* queue of pending UN/SUBSCRIBEs, EMsg's */
	int subscribe_id;
	EDList subscribe;

	/* working variables at runtime */
	gboolean subscribed_state:1; /* for setting the selection*/
};

typedef struct _EMSubscribeNode EMSubscribeNode;
struct _EMSubscribeNode {
	struct _EMSubscribeNode *next;
	struct _EMSubscribeNode *prev;

	CamelFolderInfo *info;
	GtkTreePath *path;
};


static EMSubscribe * subscribe_new(EMSubscribeEditor *se, const char *uri);
static void sub_editor_menu_changed(GtkWidget *w, EMSubscribeEditor *se);
static void sub_editor_subscribe(GtkWidget *w, EMSubscribeEditor *se);
static void sub_editor_unsubscribe(GtkWidget *w, EMSubscribeEditor *se);

static GtkDialogClass *emse_parent;

static void
emse_init(GObject *o)
{
	EMSubscribeEditor *se = (EMSubscribeEditor *)o;
#if 0
	struct _EMSubscribeEditorPrivate *p;
#endif
	GtkWidget *hbox, *vbox, *w, *menu;
	EAccountList *accounts;
	EIterator *iter;

	printf("em folder view init\n");
#if 0
	p = se->priv = g_malloc0(sizeof(struct _EMSubscribeEditorPrivate));
#endif
	e_dlist_init(&se->stores);

	se->vbox = gtk_vbox_new(FALSE, 3);

	/* setup menu */
	menu = gtk_menu_new();
	w = gtk_menu_item_new_with_label("No server selected");
	gtk_menu_shell_append ((GtkMenuShell *)menu, w);

	accounts = mail_config_get_accounts ();
	for (iter = e_list_get_iterator ((EList *) accounts);
	     e_iterator_is_valid (iter);
	     e_iterator_next (iter)) {
		EAccount *account = (EAccount *) e_iterator_get (iter);
		
		/* setup url table, and store table? */
		if (account->enabled && account->source->url) {
			w = gtk_menu_item_new_with_label(account->name);
			gtk_menu_shell_append ((GtkMenuShell *)menu, w);
			e_dlist_addtail(&se->stores, (EDListNode *)subscribe_new(se, account->source->url));
		}
	}
	g_object_unref(iter);

	se->optionmenu = gtk_option_menu_new();
	gtk_option_menu_set_menu((GtkOptionMenu *)se->optionmenu, menu);
	gtk_box_pack_start((GtkBox *)((GtkDialog *)se)->vbox, se->optionmenu, FALSE, FALSE, 0);

	g_signal_connect(se->optionmenu, "changed", G_CALLBACK(sub_editor_menu_changed), se);

	hbox = gtk_hbox_new(FALSE, 3);

	gtk_box_pack_start((GtkBox *)hbox, se->vbox, TRUE, TRUE, 0);

	vbox = gtk_vbox_new(TRUE, 3);
	se->subscribe_button = gtk_button_new_with_label(_("Subscribe"));
	gtk_box_pack_start((GtkBox *)vbox, se->subscribe_button, FALSE, FALSE, 0);
	se->unsubscribe_button = gtk_button_new_with_label(_("Unsubscribe"));
	gtk_box_pack_start((GtkBox *)vbox, se->unsubscribe_button, FALSE, FALSE, 0);
	gtk_box_pack_start((GtkBox *)hbox, vbox, FALSE, FALSE, 0);

	g_signal_connect(se->subscribe_button, "clicked", G_CALLBACK(sub_editor_subscribe), se);
	g_signal_connect(se->unsubscribe_button, "clicked", G_CALLBACK(sub_editor_unsubscribe), se);

	gtk_box_pack_start((GtkBox *)((GtkDialog *)se)->vbox, hbox, TRUE, TRUE, 0);
	gtk_widget_show_all((GtkWidget *)((GtkDialog *)se)->vbox);

	/* setup defaults */
	se->none_selected = gtk_label_new("Please select a server");
	gtk_box_pack_start((GtkBox *)se->vbox, se->none_selected, TRUE, TRUE, 0);
	gtk_widget_show(se->none_selected);
}

static void
emse_finalise(GObject *o)
{
	/*EMSubscribeEditor *emfv = (EMSubscribeEditor *)o;*/

	/* FIXME: need to clean out pending store opens */
	/* FIXME: should be in destroy method? */
	printf("editor destroyed\n");
#if 0
	g_free(emfv->priv);
#endif
	((GObjectClass *)emse_parent)->finalize(o);
}

static void
emse_class_init(GObjectClass *klass)
{
	klass->finalize = emse_finalise;
}

GType
em_subscribe_editor_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMSubscribeEditorClass),
			NULL, NULL,
			(GClassInitFunc)emse_class_init,
			NULL, NULL,
			sizeof(EMSubscribeEditor), 0,
			(GInstanceInitFunc)emse_init
		};
		emse_parent = g_type_class_ref(gtk_dialog_get_type());
		type = g_type_register_static(gtk_dialog_get_type(), "EMSubscribeEditor", &info, 0);
	}

	return type;
}

GtkWidget *em_subscribe_editor_new(void)
{
	EMSubscribeEditor *emfv = g_object_new(em_subscribe_editor_get_type(), 0);

	return (GtkWidget *)emfv;
}

/* ********************************************************************** */

static void
sub_node_free(char *key, EMSubscribeNode *node, EMSubscribe *sub)
{
	if (node->path)
		gtk_tree_path_free(node->path);
	g_free(node);
}

static void
sub_ref(EMSubscribe *sub)
{
	sub->ref_count++;
}

static void
sub_unref(EMSubscribe *sub)
{
	GSList *l;

	sub->ref_count--;
	if (sub->ref_count == 0) {
		printf("subscribe object finalised\n");
		/* we dont have to delete the "subscribe" task list, as it must be empty,
		   otherwise we wouldn't be unreffed (intentional circular reference) */
		l = sub->info_list;
		while (l) {
			GSList *n = l->next;

			camel_store_free_folder_info(sub->store, (CamelFolderInfo *)l->data);
			g_slist_free_1(l);
			l = n;
		}
		if (sub->folders) {
			g_hash_table_destroy(sub->folders);
			g_hash_table_foreach(sub->folders, (GHFunc)sub_node_free, sub);
		}
		if (sub->store)
			camel_object_unref(sub->store);
		g_free(sub->store_uri);
		g_free(sub);
	}
}

/* ** Subscribe folder operation **************************************** */

struct _em_subscribe_msg {
	struct _mail_msg msg;

	EMSubscribe *sub;
	EMSubscribeNode *node;
	int subscribe;
	char *path;
};

static void 
sub_folder_subscribe (struct _mail_msg *mm)
{
	struct _em_subscribe_msg *m = (struct _em_subscribe_msg *) mm;

	if (m->subscribe)
		camel_store_subscribe_folder (m->sub->store, m->node->info->full_name, &mm->ex);
	else
		camel_store_unsubscribe_folder (m->sub->store, m->node->info->full_name, &mm->ex);
}

static void 
sub_folder_subscribed (struct _mail_msg *mm)
{
	struct _em_subscribe_msg *m = (struct _em_subscribe_msg *) mm;
	GtkTreeIter iter;
	GtkTreeModel *model;
	EMSubscribeNode *node;
	gboolean subscribed, issub;

	m->sub->subscribe_id = -1;
	if (m->sub->cancel)
		return;

	if (!camel_exception_is_set(&mm->ex)) {
		if (m->subscribe)
			m->node->info->flags |= CAMEL_FOLDER_SUBSCRIBED;
		else
			m->node->info->flags &= ~CAMEL_FOLDER_SUBSCRIBED;
	}

	/* make sure the tree view matches the correct state */
	model = gtk_tree_view_get_model(m->sub->tree);
	if (gtk_tree_model_get_iter_from_string(model, &iter, m->path)) {
		issub = (m->node->info->flags & CAMEL_FOLDER_SUBSCRIBED) != 0;
		gtk_tree_model_get(model, &iter, 0, &subscribed, 2, &node, -1);
		if (node == m->node)
			gtk_tree_store_set((GtkTreeStore *)model, &iter, 0, issub, -1);
		else
			printf("node mismatch, or subscribe state changed failed\n");
	}

	/* queue any further ones */
	m = (struct _em_subscribe_msg *)e_dlist_remhead(&m->sub->subscribe);
	if (m) {
		m->sub->subscribe_id = m->msg.seq;
		e_thread_put (mail_thread_new, (EMsg *)m);
	}
}

static void 
sub_folder_free (struct _mail_msg *mm)
{
	struct _em_subscribe_msg *m = (struct _em_subscribe_msg *) mm;

	g_free(m->path);
	sub_unref(m->sub);
}

static struct _mail_msg_op sub_subscribe_folder_op = {
	NULL, /*subscribe_folder_desc,*/
	sub_folder_subscribe,
	sub_folder_subscribed,
	sub_folder_free,
};

/* spath is tree path in string form */
static int
sub_subscribe_folder (EMSubscribe *sub, EMSubscribeNode *node, int state, const char *spath)
{
	struct _em_subscribe_msg *m;
	int id;

	m = mail_msg_new (&sub_subscribe_folder_op, NULL, sizeof(*m));
	m->sub = sub;
	sub_ref(sub);
	m->node = node;
	m->subscribe = state;
	m->path = g_strdup(spath);

	id = m->msg.seq;
	if (sub->subscribe_id == -1) {
		sub->subscribe_id = id;
		printf("running subscribe folder '%s'\n", spath);
		e_thread_put (mail_thread_new, (EMsg *)m);
	} else {
		printf("queueing subscribe folder '%s'\n", spath);
		e_dlist_addtail(&sub->subscribe, (EDListNode *)m);
	}

	return id;
}

/* ********************************************************************** */
static void
sub_fill_level(EMSubscribe *sub, CamelFolderInfo *info,  GtkTreeIter *parent, int pending)
{
	CamelFolderInfo *fi;
	GtkTreeStore *treestore;
	GtkTreeIter iter;
	EMSubscribeNode *node;

	treestore = (GtkTreeStore *)gtk_tree_view_get_model(sub->tree);

	/* first, fill a level up */
	fi = info;
	while (fi) {
		if (g_hash_table_lookup(sub->folders, fi->full_name) == NULL) {
			gtk_tree_store_append(treestore, &iter, parent);
			node = g_malloc0(sizeof(*node));
			node->info = fi;
			gtk_tree_store_set(treestore, &iter, 0, camel_store_folder_subscribed(sub->store, fi->full_name), 1, fi->name, 2, node, -1);
			if ((fi->flags & CAMEL_FOLDER_NOINFERIORS) == 0) {
				node->path = gtk_tree_model_get_path((GtkTreeModel *)treestore, &iter);
				if (node->path) {
					/* save time, if we have any children alread, dont re-scan */
					if (fi->child) {
						printf("scanning child '%s'\n", fi->child->full_name);
						sub_fill_level(sub, fi->child, &iter, FALSE);
					} else {
						if (pending)
							e_dlist_addtail(&sub->pending, (EDListNode *)node);
					}
				}
				g_hash_table_insert(sub->folders, fi->full_name, node);
			}
		}
		fi = fi->sibling;
	}
}

/* async query of folderinfo */
static int sub_queue_fill_level(EMSubscribe *sub, EMSubscribeNode *node);

struct _zget_folderinfo_msg {
	struct _mail_msg msg;

	EMSubscribe *sub;
	EMSubscribeNode *node;
	CamelFolderInfo *info;
};

static void
sub_folderinfo_get (struct _mail_msg *mm)
{
	struct _zget_folderinfo_msg *m = (struct _zget_folderinfo_msg *) mm;

	m->info = camel_store_get_folder_info (m->sub->store, m->node?m->node->info->full_name:"", CAMEL_STORE_FOLDER_INFO_FAST, &mm->ex);
}

static void
sub_folderinfo_got (struct _mail_msg *mm)
{
	struct _zget_folderinfo_msg *m = (struct _zget_folderinfo_msg *) mm;
	EMSubscribeNode *node;

	m->sub->pending_id = -1;
	if (m->sub->cancel)
		return;

	if (camel_exception_is_set (&mm->ex)) {
		g_warning ("Error getting folder info from store: %s",
			   camel_exception_get_description (&mm->ex));
	}

	if (m->info) {
		if (m->node) {
			GtkTreeIter iter;

			gtk_tree_model_get_iter(gtk_tree_view_get_model(m->sub->tree), &iter, m->node->path);
			sub_fill_level(m->sub, m->info, &iter, FALSE);
		} else {
			sub_fill_level(m->sub, m->info, NULL, TRUE);
		}
	}

	/* check for more to do */
	node = (EMSubscribeNode *)e_dlist_remhead(&m->sub->pending);
	if (node)
		sub_queue_fill_level(m->sub, node);
}

static void
sub_folderinfo_free (struct _mail_msg *mm)
{
	struct _zget_folderinfo_msg *m = (struct _zget_folderinfo_msg *) mm;

	if (m->info)
		m->sub->info_list = g_slist_prepend(m->sub->info_list, m->info);

	sub_unref(m->sub);
}

static struct _mail_msg_op sub_folderinfo_op = {
	NULL, /*sub_folderinfo_desc,  we do our own progress reporting/cancellation */
	sub_folderinfo_get,
	sub_folderinfo_got,
	sub_folderinfo_free,
};

static int
sub_queue_fill_level(EMSubscribe *sub, EMSubscribeNode *node)
{
	struct _zget_folderinfo_msg *m;
	int id;

	printf("Starting get folderinfo of '%s'\n", node?node->info->full_name:"<root>");

	m = mail_msg_new (&sub_folderinfo_op, NULL, sizeof(*m));
	sub_ref(sub);
	m->sub = sub;
	m->node = node;

	sub->pending_id = m->msg.seq;

	e_thread_put (mail_thread_new, (EMsg *)m);
	return id;
}

/* ********************************************************************** */

/* (un) subscribes the current selection */
static void sub_do_subscribe(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, void *data)
{
	EMSubscribe *sub = data;
	EMSubscribeNode *node;
	gboolean subscribed;

	gtk_tree_model_get(model, iter, 0, &subscribed, 2, &node, -1);
	if (sub->subscribed_state ^ subscribed) {
		char *spath;

		spath = gtk_tree_path_to_string(path);
		gtk_tree_store_set((GtkTreeStore *)model, iter, 0, subscribed, -1);
		sub_subscribe_folder(sub, node, sub->subscribed_state, spath);
		g_free(spath);
	}
}

static void
sub_subscribe(EMSubscribe *sub, gboolean subscribed)
{
	GtkTreeSelection *selection;

	if (sub->tree == NULL)
		return;

	sub->subscribed_state = subscribed;
	selection = gtk_tree_view_get_selection (sub->tree);
	gtk_tree_selection_selected_foreach(selection, sub_do_subscribe, sub);
}

static void
sub_subscribe_toggled(GtkCellRendererToggle *render, const char *spath, EMSubscribe *sub)
{
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model(sub->tree);
	EMSubscribeNode *node;
	gboolean subscribed;

	printf("subscribe toggled?\n");

	if (gtk_tree_model_get_iter_from_string(model, &iter, spath)) {
		gtk_tree_model_get(model, &iter, 0, &subscribed, 2, &node, -1);
		subscribed = !subscribed;
		printf("new state is %s\n", subscribed?"subscribed":"not subscribed");
		gtk_tree_store_set((GtkTreeStore *)model, &iter, 0, subscribed, -1);
		sub_subscribe_folder(sub, node, subscribed, spath);
	}
}

static void
sub_row_expanded(GtkTreeView *tree, GtkTreeIter *iter, GtkTreePath *path, EMSubscribe *sub)
{
	EMSubscribeNode *node;
	GtkTreeIter child;
	GtkTreeModel *model = (GtkTreeModel *)gtk_tree_view_get_model(tree);
	EDList list;

	gtk_tree_model_get(model, iter, 2, &node, -1);
	if (node->path == NULL) {
		printf("path '%s' already processed\n", node->info->full_name);
		return;
	}
	gtk_tree_path_free(node->path);
	node->path = NULL;

	e_dlist_init(&list);

	/* add all children nodes to pending, and fire off a pending */
	/* we add them to the head of the pending list, to make it more interactive */
	gtk_tree_model_iter_children(model, &child, iter);
	do {
		gtk_tree_model_get(model, &child, 2, &node, -1);
		if (node->path)
			e_dlist_addtail(&list, (EDListNode *)node);
	} while (gtk_tree_model_iter_next(model, &child));

	while ( (node = (EMSubscribeNode *)e_dlist_remtail(&list)) )
		e_dlist_addhead(&sub->pending, (EDListNode *)node);

	if (sub->pending_id == -1
	    && (node = (EMSubscribeNode *)e_dlist_remtail(&sub->pending)))
		sub_queue_fill_level(sub, node);
}

static void
sub_destroy(GtkWidget *w, EMSubscribe *sub)
{
	struct _em_subscribe_msg *m;

	printf("subscribe closed\n");
	sub->cancel = TRUE;

	if (sub->pending_id != -1)
		mail_msg_cancel(sub->pending_id);

	if (sub->subscribe_id != -1)
		mail_msg_cancel(sub->subscribe_id);

	while ( (m = (struct _em_subscribe_msg *)e_dlist_remhead(&sub->subscribe)) )
		mail_msg_free(m);

	sub_unref(sub);
}

static EMSubscribe *
subscribe_new(EMSubscribeEditor *se, const char *uri)
{
	EMSubscribe *sub;

	sub = g_malloc0(sizeof(*sub));
	sub->store_uri = g_strdup(uri);
	sub->editor = se;
	sub->ref_count = 1;
	sub->pending_id = -1;
	e_dlist_init(&sub->pending);
	sub->subscribe_id = -1;
	e_dlist_init(&sub->subscribe);
	sub->store_id = -1;

	return sub;
}

static void
subscribe_set_store(EMSubscribe *sub, CamelStore *store)
{
	if (store == NULL || !camel_store_supports_subscriptions(store)) {
		sub->widget = gtk_label_new("This store does not support subscriptions");
		gtk_widget_show(sub->widget);
	} else {
		GtkTreeSelection *selection;
		GtkCellRenderer *renderer;
		GtkTreeStore *model;

		sub->store = store;
		camel_object_ref(store);
		sub->folders = g_hash_table_new(g_str_hash, g_str_equal);
		
		model = gtk_tree_store_new (3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_POINTER);
		sub->tree = (GtkTreeView *) gtk_tree_view_new_with_model ((GtkTreeModel *) model);
		gtk_widget_show ((GtkWidget *)sub->tree);
		
		sub->widget = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sub->widget), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sub->widget), GTK_SHADOW_IN);
		gtk_container_add((GtkContainer *)sub->widget, (GtkWidget *)sub->tree);
		gtk_widget_show(sub->widget);

		renderer = gtk_cell_renderer_toggle_new ();
		g_object_set(renderer, "activatable", TRUE, NULL);
		gtk_tree_view_insert_column_with_attributes (sub->tree, -1, _("Subscribed"), renderer, "active", 0, NULL);
		g_signal_connect(renderer, "toggled", G_CALLBACK(sub_subscribe_toggled), sub);
	
		renderer = gtk_cell_renderer_text_new ();
		gtk_tree_view_insert_column_with_attributes (sub->tree, -1, _("Folder"), renderer, "text", 1, NULL);
		gtk_tree_view_set_expander_column(sub->tree, gtk_tree_view_get_column(sub->tree, 1));
		
		selection = gtk_tree_view_get_selection (sub->tree);
		gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
		gtk_tree_view_set_headers_visible (sub->tree, FALSE);

		g_signal_connect(sub->tree, "row-expanded", G_CALLBACK(sub_row_expanded), sub);
		g_signal_connect(sub->tree, "destroy", G_CALLBACK(sub_destroy), sub);

		sub_queue_fill_level(sub, NULL);
	}

	gtk_box_pack_start((GtkBox *)sub->editor->vbox, sub->widget, TRUE, TRUE, 0);
}

static void
sub_editor_subscribe(GtkWidget *w, EMSubscribeEditor *se)
{
	printf("subscribe clicked, current = %p\n", se->current);

	if (se->current)
		sub_subscribe(se->current, TRUE);
}

static void
sub_editor_unsubscribe(GtkWidget *w, EMSubscribeEditor *se)
{
	printf("unsubscribe clicked\n");

	if (se->current)
		sub_subscribe(se->current, FALSE);
}

static void
sub_editor_got_store(char *uri, CamelStore *store, void *data)
{
	struct _EMSubscribe *sub = data;

	if (!sub->cancel)
		subscribe_set_store(sub, store);
	sub_unref(sub);
}

static void
sub_editor_menu_changed(GtkWidget *w, EMSubscribeEditor *se)
{
	int i, n;
	struct _EMSubscribe *sub;

	printf("menu changed\n");

	i = 1;
	n = gtk_option_menu_get_history((GtkOptionMenu *)se->optionmenu);
	if (n == 0)
		gtk_widget_show(se->none_selected);
	else
		gtk_widget_hide(se->none_selected);

	se->current = NULL;
	sub = (struct _EMSubscribe *)se->stores.head;
	while (sub->next) {
		if (i == n) {
			se->current = sub;
			if (sub->widget) {
				gtk_widget_show(sub->widget);
			} else if (sub->store_id == -1) {
				sub_ref(sub);
				sub->store_id = mail_get_store(sub->store_uri, NULL, sub_editor_got_store, sub);
			}
		} else {
			if (sub->widget)
				gtk_widget_hide(sub->widget);
		}
		i++;
		sub = sub->next;
	}
}
