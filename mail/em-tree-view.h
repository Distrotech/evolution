
#ifndef _EM_TREE_VIEW_H
#define _EM_TREE_VIEW_H

#include <gtk/gtktreeview.h>

struct _GtkTreeIter;

typedef struct _EMTreeView       EMTreeView;
typedef struct _EMTreeViewClass  EMTreeViewClass;

typedef enum {
	EM_TREE_VIEW_PLAIN,
	EM_TREE_VIEW_OUTGOING,
	EM_TREE_VIEW_JUNK,
	EM_TREE_VIEW_TRASH,
} em_tree_view_t;

struct _EMTreeView
{
	GtkTreeView parent;

	struct _CamelFolder *folder;
	char *folder_uri;

	char *search;

	/* Hiding? */

	int threaded:1;
	int hidedeleted:1;
	int hidejunk:1;
};

struct _EMTreeViewClass
{
	GtkTreeViewClass parent_class;
};

GType em_tree_view_get_type (void);

EMTreeview *em_tree_view_new(void);

void em_tree_view_set_folder(EMTreeView *emtv, struct _CamelFolder *folder, const char *uri, em_tree_view_t type);

GPtrArray *em_tree_view_get_selected(EMTreeView *emtv);
void em_tree_view_set_selected(EMTreeView *emtv, GPtrArray *uids);

void em_tree_view_select_uid(EMTreeView *emtv, const char *uid);
void em_tree_view_select_all(EMTreeView *emtv);
void em_tree_view_select_invert(EMTreeView *emtv);

void em_tree_view_set_hidedeleted(EMTreeView *emtv, int hidedeleted);

void em_tree_view_set_threaded(EMTreeView *emtv, int threaded);
void em_tree_view_set_search(EMTreeView *emtv, const char *search);

#define COMPAT
#ifdef COMPAT
typedef struct _EMTreeView MessageList;

#define message_list_new() em_tree_view_new()
#define message_list_set_folder(ml, folder, uri, outgoing) em_tree_view_set_folder(ml, folder, uri, outgoing?EM_TREE_VIEW_OUTGOING:EM_TREE_VIEW_PLAIN)
#define message_list_foreach(ml, callback, data) abort()

/* these might need implementing */
#define message_list_freeze(x)
#define message_list_thaw(x)

#define message_list_get_selected(ml) em_tree_view_get_selected(ml)
#define message_list_set_selected(ml, uids) em_tree_view_set_selected(ml, uids)
#define message_list_free_uids(ml, uids) em_utils_free_uids(uids)

#define MESSAGE_LIST_SELECT_NEXT (0)
#define MESSAGE_LIST_SELECT_PREVIOUS (0)
#define MESSAGE_LIST_SELECT_DIRECTION (0)
#define MESSAGE_LIST_SELECT_WRAP (0)

#define message_list_select(ml, dir, flags, mask) g_warning("message list select not re-implemented")
#define message_list_can_select(ml, dir, flags, mask) (FALSE, g_warning("message_list_can_select not re-implemented"))
#define message_list_select_uid(ml, uid) em_tree_view_select_uid(ml, uid)

#define message_list_select_next_thread(ml) g_warning("message list select next thread not re-implemented")

#define message_list_select_all(ml) em_tree_view_select_all(ml)
#define message_list_select_thread(ml) g_warning("message list select thread not re-implemented")
#define message_list_invert_selection(ml) em_tree_view_select_invert(ml)

#define message_list_copy(ml, cut) g_warning("message list copy not re-implemented")
#define message_list_paste(ml) g_warning("message list paste not re-implemented")

/* info */
#define message_list_length(ml) abort()
#define message_list_hidden(ml) (0, g_warning("message list hidden not re-implemented")

/* hide specific messages */
#define message_list_hide_add(ml, expr, lower, upper) g_warning("message list hide add not re-implemented")
#define message_list_hide_uids(ml, uids) g_warning("message list hide uids not re-implemented")
#define message_list_hide_clear(ml) g_warning("message list hide clear not re-implemented")

#define message_list_set_threaded(ml, threaded) em_tree_view_set_threaded(ml, threaded)
#define message_list_set_hidedeleted(ml, hide) em_tree_view_set_hidedeleted(ml, hide)
#define message_list_set_search(ml, search) em_tree_view_set_search(ml, search)

#define message_list_save_state(ml) g_warning("message list save state not re-implemented")

#define message_list_get_scrollbar_position(ml) (0.0, g_warning("message list get scrollbar pos not re-implemented")
#define message_list_set_scrollbar_position(ml, pos) g_warning("message list set scrollbar pos not re-implemented")

#endif /* COMPAT */

#endif /* _EM_TREE_VIEW_H */
