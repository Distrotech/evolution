
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
void e_plugin_add_load_path(const char *);
int e_plugin_load_plugins(void);

void e_plugin_register_type(GType type);

void *e_plugin_invoke(EPlugin *ep, const char *name, void *data);

/* static helpers */
/* maps prop or content to 'g memory' */
char *e_plugin_xml_prop(xmlNodePtr node, const char *id);
int e_plugin_xml_int(xmlNodePtr node, const char *id, int def);
char *e_plugin_xml_content(xmlNodePtr node);

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

/* utilities for subclasses to use */
typedef struct _EPluginHookTargetMap EPluginHookTargetMap;
typedef struct _EPluginHookTargetKey EPluginHookTargetKey;

struct _EPluginHookTargetMap {
	const char *type;
	int id;
	const struct _EPluginHookTargetKey *mask_bits;	/* null terminated array */
};

/* maps a field name in xml to bit(s) or id */
struct _EPluginHookTargetKey {
	const char *key;
	guint32 value;
};

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

/* static methods */
guint32 e_plugin_hook_mask(xmlNodePtr root, const struct _EPluginHookTargetKey *map, const char *prop);
guint32 e_plugin_hook_id(xmlNodePtr root, const struct _EPluginHookTargetKey *map, const char *prop);

#endif /* ! _E_PLUGIN_H */
