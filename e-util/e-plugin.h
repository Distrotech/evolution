
#ifndef _E_PLUGIN_H
#define _E_PLUGIN_H

#include <glib.h>
#include <glib-object.h>
#include <libxml/tree.h>

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

	const char *type;

	int (*construct)(EPlugin *, xmlNodePtr root);
	void *(*invoke)(EPlugin *, const char *name, void *data);
};

GType e_plugin_get_type(void);

int e_plugin_construct(EPlugin *ep, xmlNodePtr root);
int e_plugin_load_plugins(const char *path);
void e_plugin_register_type(GType type);

void *e_plugin_invoke(EPlugin *ep, const char *name, void *data);

/* ********************************************************************** */
#include <gmodule.h>

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

#endif /* ! _E_PLUGIN_H */
