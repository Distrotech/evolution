/*
  Test Plugin for Evolution Mail.

  This is expermintal, work-in-progress, and will likely never be how
  plugins are actually implemented.  However, it demonstrates some of
  the basics of how a plugin might be able to hook into Evolution.

  The popup menu system allows you to simply hook in to specific or
  all menus, add any number of menu items anywhere in the tree, it
  will allow you to override items, etc.
*/

#include "em-popup.h"
#include <gtk/gtkwidget.h>

#include <gmodule.h>

#define N_(x) x

/* Select plugins get an EMPopupTarget of type EM_POPUP_TARGET_SELECT, which contains the
   the following information:

struct _EMPopupTarget {
	enum _em_popup_target_t type;
	guint32 mask;
	struct _GtkWidget *widget;
	union {
		char *uri;
--		struct {
--			struct _CamelFolder *folder;
--			char *folder_uri;
--			GPtrArray *uids;
--		} select;
		struct {
			char *mime_type;
			struct _CamelMimePart *part;
		} part;
	} data;
};
*/

static void
tp_popup_gpg(GtkWidget *w, EMPopupTarget *t)
{
	printf("Running message through pgp\n");
}

static void
tp_popup_spam(GtkWidget *w, EMPopupTarget *t)
{
	printf("Running message through en-spam-in-a-tor\n");
}

static EMPopupItem list_popups[] = {
	{ EM_POPUP_BAR, "50.pip0", NULL, NULL, NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_SUBMENU, "50.pipe", N_("Pipe to"), NULL, NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "50.pipe/001", N_("_Gpg"), G_CALLBACK(tp_popup_gpg), NULL, NULL, EM_POPUP_SELECT_ONE },
	{ EM_POPUP_ITEM, "50.pipe/002", N_("_Spam Marker"), G_CALLBACK(tp_popup_spam), NULL, NULL, EM_POPUP_SELECT_ONE },
};

#define LEN(x) (sizeof(x)/sizeof(x[0]))

/* ********************************************************************** */

static EMPopupFactory *tp_factory;

static void
tp_list_menu_factory(EMPopup *emp, EMPopupTarget *target, void *data)
{
	int i, len;
	EMPopupItem *items;
	GSList *menus = NULL;

	switch (target->type) {
	case EM_POPUP_TARGET_SELECT:
		items = list_popups;
		len = LEN(list_popups);
		break;
	default:
		len = 0;
		break;
	}

	for (i=0;i<len;i++) {
		if ((items[i].mask & target->mask) == 0) {
			items[i].activate_data = target;
			menus = g_slist_prepend(menus, &items[i]);
		}
	}

	if (menus)
		em_popup_add_items(emp, menus, (GDestroyNotify)g_slist_free);
}

const gchar *
g_module_check_init(GModule *module)
{
	tp_factory = em_popup_static_add_factory("com.ximian.mail.folderview.popup.select", tp_list_menu_factory, NULL);

	return NULL;
}

void
g_module_unload(GModule *module)
{
	em_popup_static_remove_factory(tp_factory);
}
