
#ifndef _E_PLUGIN_H
#define _E_PLUGIN_H

#include <glib.h>
#include <glib-object.h>
#include <libxml/tree.h>
#include <gmodule.h>

/* ********************************************************************** */

typedef struct _EPlugin EPlugin;
typedef struct _EPluginClass EPluginClass;

#define E_PLUGIN_CLASSID "com.ximian.evolution.plugin"

struct _EPlugin {
	GObject object;

	char *description;
	char *name;
	GSList *hooks;
};

struct _EPluginClass {
	GObjectClass class;

	const char *id;

	int (*construct)(EPlugin *, xmlNodePtr root);
	void *(*find_callback)(EPlugin *, const char *name);
};

GType e_plugin_get_type(void);

int e_plugin_construct(EPlugin *ep, xmlNodePtr root);
void *e_plugin_find_callback(EPlugin *ep, const char *name);

int e_plugin_load_plugins(const char *path);
void e_plugin_register_type(GType type);

/* ********************************************************************** */

typedef struct _EPluginLib EPluginLib;
typedef struct _EPluginLibClass EPluginLibClass;

struct _EPluginLib {
	EPlugin plugin;

	char *location;
	GModule *module;
};

struct _EPluginLibClass {
	EPluginClass plugin_class;
};

GType e_plugin_lib_get_type(void);

EPlugin *e_plugin_lib_new(const char *path);

#if 0
struct _EPluginStatic {
	GHashTable *symbols;
};

EPlugin *e_plugin_static_new(void);
#endif

/* ********************************************************************** */

typedef struct _EPluginHook EPluginHook;
typedef struct _EPluginHookClass EPluginHookClass;

struct _EPluginHook {
	GObject object;

	struct _EPlugin *plugin;
};

struct _EPluginHookClass {
	GObjectClass class;

	const char *id;

	int (*construct)(EPluginHook *eph, EPlugin *ep, xmlNodePtr root);
};

GType e_plugin_hook_get_type(void);

void e_plugin_hook_register_type(GType type);

EPluginHook * e_plugin_hook_new(EPlugin *ep, xmlNodePtr root);

/* ********************************************************************** */

#include "em-popup.h"

typedef struct _EMPopupHookItem EMPopupHookItem;
typedef struct _EMPopupHookMenu EMPopupHookMenu;
typedef struct _EMPopupHook EMPopupHook;
typedef struct _EMPopupHookClass EMPopupHookClass;

typedef void (*EMPopupHookFunc)(struct _EPlugin *plugin, EMPopupTarget *target);

struct _EMPopupHookItem {
	EMPopupItem item;

	struct _EMPopupHook *hook; /* parent pointer */

	struct _EMPopupTarget *target; /* to save the target during menu popup */
	char *activate;		/* name of activate callback, from plugin module */
	EMPopupHookFunc activate_func; /* activate function once looked up in module */
};

struct _EMPopupHookMenu {
	struct _EMPopupHook *hook; /* parent pointer */
	char *id;		/* target menu id for these menu items */
	int target_type;	/* target type of this menu */
	GSList *items;		/* items to add to menu */
};

struct _EMPopupHook {
	EPluginHook hook;

	GSList *menus;
};

struct _EMPopupHookClass {
	EPluginHookClass hook_class;
};

GType em_popup_hook_get_type(void);


#endif /* ! _E_PLUGIN_H */
