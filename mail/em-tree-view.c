
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <glib/gi18n.h>

#include <gtk/gtktreeselection.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkcellrenderertext.h>

#include <camel/camel-folder.h>

#include <libxml/tree.h>

#include "mail-config.h"

#include "em-tree-view.h"
#include "em-tree-store.h"

struct _EMTreeViewPrivate {
	/* for various state loading/saving */
	guint state_timeout;
	char *expanded_filename;
	FILE *expanded_file;
	char *state_filename;

	GtkCellRenderer *text_renderer;
};

#define _PRIVATE(x) (g_type_instance_get_private((GTypeInstance *)(x), em_tree_view_get_type()))

static GtkTreeViewClass *emtv_parent;

enum {
	LAST_SIGNAL
};

static guint emtv_signals[LAST_SIGNAL];

static void
emtv_init(GObject *o)
{
	EMTreeView *emtv = (EMTreeView *)o;
	struct _EMTreeViewPrivate *p = _PRIVATE(o);

	p->text_renderer = gtk_cell_renderer_text_new();
	emtv = emtv;
}

static void
emtv_finalise(GObject *o)
{
	EMTreeView *emtv = (EMTreeView *)o;
	struct _EMTreeViewPrivate *p = _PRIVATE(o);

	g_object_unref(p->text_renderer);

	((GObjectClass *)emtv_parent)->finalize(o);

	emtv = emtv;
}

static void
emtv_destroy (GtkObject *o)
{
	EMTreeView *emtv = (EMTreeView *)o;
	struct _EMTreeViewPrivate *p = _PRIVATE(o);

	emtv = emtv;
	p = p;

	((GtkObjectClass *)emtv_parent)->destroy(o);
}

static void
emtv_class_init(GObjectClass *klass)
{
	klass->finalize = emtv_finalise;
	
	((GtkObjectClass *)klass)->destroy = emtv_destroy;
	/*((GtkWidgetClass *)klass)->popup_menu = emtv_popup_menu;*/

	g_type_class_add_private(klass, sizeof(struct _EMTreeViewPrivate));
}

GType
em_tree_view_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMTreeViewClass),
			NULL, NULL,
			(GClassInitFunc)emtv_class_init,
			NULL, NULL,
			sizeof(EMTreeView), 0,
			(GInstanceInitFunc)emtv_init
		};
		emtv_parent = g_type_class_ref(gtk_tree_view_get_type());
		type = g_type_register_static(gtk_tree_view_get_type(), "EMTreeView", &info, 0);
	}

	return type;
}

EMTreeView *em_tree_view_new(void)
{
	EMTreeView *emtv = g_object_new(em_tree_view_get_type(), 0);

	return emtv;
}

static void emtv_column_notify(GObject *o, GParamSpec *spec, EMTreeView *emtv);

static void
emtv_cell_date(GtkTreeViewColumn *col, GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	/*EMTreeView *emtv = data;*/
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
	/*EMTreeView *emtv = data;*/
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
emtv_add_column(EMTreeView *emtv, int id, int width)
{
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);
	GtkTreeViewColumn *col;

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, _(emts_column_info[id].title));
	switch (id) {
	case EMTS_COL_SUBJECT:
	case EMTS_COL_FROM:
		gtk_tree_view_column_pack_start(col, p->text_renderer, TRUE);
		gtk_tree_view_column_set_attributes(col, p->text_renderer, "text", id, NULL);
		gtk_tree_view_column_set_cell_data_func(col, p->text_renderer, emtv_cell_properties, emtv, NULL);
		break;
	case EMTS_COL_DATE:
		gtk_tree_view_column_pack_start(col, p->text_renderer, TRUE);
		gtk_tree_view_column_set_cell_data_func(col, p->text_renderer, emtv_cell_date, emtv, NULL);
		break;
	default:
		abort();
	}

	g_object_set(col, "sizing", GTK_TREE_VIEW_COLUMN_FIXED, "fixed_width", width, "resizable", TRUE, NULL);
	g_object_set_data((GObject *)col, "column-id", (void *)emts_column_info[id].id);
	gtk_tree_view_append_column((GtkTreeView *)emtv, col);
	if (id == EMTS_COL_SUBJECT)
		gtk_tree_view_set_expander_column((GtkTreeView *)emtv, col);
	g_signal_connect(col, "notify", G_CALLBACK(emtv_column_notify), emtv);

	return col;
}

static int
emtv_load_state(EMTreeView *emtv)
{
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);
	xmlDocPtr doc;
	int res = -1;
	xmlNodePtr node;

	doc = xmlParseFile(p->state_filename);
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

			emtv_add_column(emtv, id, width);
		}
		node = node->next;
	}

	res = 0;
fail:
	xmlFreeDoc(doc);

	return res;
}

static void
emtv_load_state_default(EMTreeView *emtv)
{
	/* TODO: Remove columns? */

	/* TODO: Base column sizes on window size */

	emtv_add_column(emtv, EMTS_COL_SUBJECT, 300);
	emtv_add_column(emtv, EMTS_COL_FROM, 300);
	emtv_add_column(emtv, EMTS_COL_DATE, 100);
}

static void
emtv_save_expanded_row(GtkTreeView *tree, GtkTreePath *path, void *data)
{
	struct _EMTreeViewPrivate *p = data;
	GtkTreeModel *model;
	GtkTreeIter iter;
	const char *uid;
	CamelMessageInfo *mi;

	model = gtk_tree_view_get_model(tree);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, EMTS_COL_MESSAGEINFO, &mi, -1);
	if (mi && (uid = camel_message_info_uid(mi)))
		fprintf(p->expanded_file, "%s\n", uid);
}

static gboolean
emtv_save_state_timeout(void *data)
{
	EMTreeView *emtv = data;
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);
	xmlDocPtr doc;
	xmlNodePtr node, root;
	GList *columns, *l;
	xmlChar *xmlbuf;
	int size;

	p->state_timeout = 0;

	doc = xmlNewDoc("1.0");
	root = xmlNewDocNode(doc, NULL, "em-tree-state", NULL);
	xmlDocSetRootElement(doc, root);

	columns = gtk_tree_view_get_columns((GtkTreeView *)emtv);
	for (l=columns;l;l=g_list_next(l)) {
		GtkTreeViewColumn *col = l->data;
		char num[16];

		node = xmlNewChild(root, NULL, "column", NULL);
		xmlSetProp(node, "id", g_object_get_data((GObject *)col, "column-id"));
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

	e_xml_save_file(p->state_filename, doc);

	xmlFreeDoc(doc);

	/* TODO: not here */
	p->expanded_file = fopen(p->expanded_filename, "w");
	gtk_tree_view_map_expanded_rows((GtkTreeView *)emtv, emtv_save_expanded_row, p);
	fclose(p->expanded_file);

	return FALSE;
}

static void
emtv_column_notify(GObject *o, GParamSpec *spec, EMTreeView *emtv)
{
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);

	if (p->state_timeout)
		g_source_remove(p->state_timeout);

	p->state_timeout = g_timeout_add(500, emtv_save_state_timeout, emtv);
}

static void
emtv_load_expanded(EMTreeView *emtv)
{
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);
	FILE *fp;
	EMTreeStore *model;

	model = (EMTreeStore *)gtk_tree_view_get_model((GtkTreeView *)emtv);

	fp = fopen(p->expanded_filename, "r");
	if (fp) {
		char line[1024], *p;

		while (fgets(line, sizeof(line), fp)) {
			GtkTreeIter iter;

			p = strchr(line, '\n');
			if (p)
				*p = 0;

			printf("setting expanded: %s\n", line);

			/* TODO: right model call depending on view mode */
			if (em_tree_store_get_iter(model, &iter, line)) {
				GtkTreePath *path;

				path = gtk_tree_model_get_path((GtkTreeModel *)model, &iter);
				if (path) {
					gtk_tree_view_expand_row((GtkTreeView *)emtv, path, FALSE);
					gtk_tree_path_free(path);
				}
			}
		}

		fclose(fp);
	}
}

static void
emtv_rebuild_model(EMTreeView *emtv)
{
	struct _EMTreeViewPrivate *p = _PRIVATE(emtv);
	EMTreeStore *emts;
	GList *columns, *l;

	/* clear old data */

	gtk_tree_view_set_model((GtkTreeView *)emtv, NULL);

	g_free(p->expanded_filename);
	p->expanded_filename = NULL;
	g_free(p->state_filename);
	p->state_filename = NULL;

	columns = gtk_tree_view_get_columns((GtkTreeView *)emtv);
	for (l=columns;l;l=g_list_next(l))
		gtk_tree_view_remove_column((GtkTreeView *)emtv, (GtkTreeViewColumn *)l->data);
	g_list_free(columns);

	if (emtv->folder == NULL)
		return;

	/* TODO: build flat model for unthreaded mode */
	emts = em_tree_store_new(emtv->folder);
	gtk_tree_view_set_model((GtkTreeView *)emtv, (GtkTreeModel *)emts);

	p->expanded_filename = mail_config_folder_to_cachename(emtv->folder, "emt-expanded-");
	p->expanded_filename = mail_config_folder_to_cachename(emtv->folder, "emt-state-");

	if (emtv_load_state(emtv) == -1)
		emtv_load_state_default(emtv);

	emtv_load_expanded(emtv);

	/* TODO: Scroll to the right position in the list, should be stored in state? */
}

void em_tree_view_set_folder(EMTreeView *emtv, struct _CamelFolder *folder, const char *uri, em_tree_view_t type)
{
	if (emtv->folder == folder)
		return;

	if (emtv->folder) {
		/*emtv_save_state(emtv);*/
		emtv_save_state_timeout(emtv);
		camel_object_unref(emtv->folder);
		emtv->folder = NULL;
		g_free(emtv->folder_uri);
		emtv->folder_uri = NULL;
	}

	emtv->folder = folder;
	emtv->folder_uri = g_strdup(uri);
	if (folder)
		camel_object_ref(folder);

	emtv_rebuild_model(emtv);
}

GPtrArray *em_tree_view_get_selected(EMTreeView *emtv)
{
	GPtrArray *uids = g_ptr_array_new();
	GtkTreeSelection *selection = gtk_tree_view_get_selection((GtkTreeView *)emtv);
	GList *rows, *l;
	GtkTreeModel *model;

	/* This looks ... slow */
	rows = gtk_tree_selection_get_selected_rows(selection, &model);
	for (l=rows; l; l=g_list_next(l)) {
		GtkTreeIter iter;
		GtkTreePath *path = l->data;

		if (gtk_tree_model_get_iter(model, &iter, path)) {
			CamelMessageInfo *mi;
			
			gtk_tree_model_get(model, &iter, EMTS_COL_MESSAGEINFO, &mi, -1);
			if (mi)
				g_ptr_array_add(uids, g_strdup(camel_message_info_uid(mi)));
		}
		gtk_tree_path_free(path);
	}
	g_list_free(rows);

	return uids;
}

void em_tree_view_set_selected(EMTreeView *emtv, GPtrArray *uids)
{
	/* */
}

void em_tree_view_select_uid(EMTreeView *emtv, const char *uid)
{
	/* must also handle pending updates */
}

void em_tree_view_select_all(EMTreeView *emtv)
{
}

void em_tree_view_select_invert(EMTreeView *emtv)
{
}

void em_tree_view_set_hidedeleted(EMTreeView *emtv, int state)
{
	if (emtv->hidedeleted == state)
		return;

	emtv->hidedeleted = state;

	/* refresh, async? */
}

void em_tree_view_set_threaded(EMTreeView *emtv, int state)
{
	if (emtv->threaded == state)
		return;

	emtv->threaded = state;

	/* refresh, async? */
}

void em_tree_view_set_search(EMTreeView *emtv, const char *search)
{
	if (emtv->search == search
	    || (emtv->search && search && strcmp(emtv->search, search) == 0))
		return;

	g_free(emtv->search);
	emtv->search = g_strdup(search);

	/* refresh, async? */
}
