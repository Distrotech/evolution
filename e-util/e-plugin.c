
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include "e-plugin.h"

/* plugin debug */
#define pd(x) x
/* plugin hook debug */
#define phd(x) x

/*
<camel-plugin
  class="com.ximian.camel.plugin.provider:1.0"
  id="com.ximian.camel.provider.imap:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  factory="camel_imap_provider_new">
 <name>imap</name>
 <description>IMAP4 and IMAP4v1 mail store</description>
 <class-data class="com.ximian.camel.plugin.provider:1.0"
   protocol="imap"
   domain="mail"
   flags="remote,source,storage,ssl"/>
</camel-plugin>

<camel-plugin
  class="com.ximian.camel.plugin.sasl:1.0"
  id="com.ximian.camel.sasl.plain:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelsasl.so"
  factory="camel_sasl_plain_new">
 <name>PLAIN</name>
 <description>SASL PLAIN authentication mechanism</description>
</camel-plugin>
*/

static GObjectClass *ep_parent_class;
static GHashTable *ep_types;

static int
ep_construct(EPlugin *ep, xmlNodePtr root)
{
	xmlNodePtr node;
	int res = -1;

	ep->description = e_plugin_xml_prop(root, "description");
	ep->name = e_plugin_xml_prop(root, "name");

	printf("creating plugin '%s'\n", ep->name);

	node = root->children;
	while (node) {
		if (strcmp(node->name, "hook") == 0) {
			struct _EPluginHook *hook;

			hook = e_plugin_hook_new(ep, node);
			if (hook)
				ep->hooks = g_slist_prepend(ep->hooks, hook);
			else {
				char *tmp = xmlGetProp(node, "class");

				g_warning("Plugin '%s' failed to load hook '%s'", ep->name, tmp?tmp:"unknown");
				if (tmp)
					xmlFree(tmp);
			}
		}
		node = node->next;
	}
	res = 0;

	return res;
}

static void
ep_finalise(GObject *o)
{
	EPlugin *ep = (EPlugin *)o;

	g_free(ep->description);
	g_free(ep->name);

	g_slist_foreach(ep->hooks, (GFunc)g_object_unref, NULL);
	g_slist_free(ep->hooks);

	((GObjectClass *)ep_parent_class)->finalize(o);
}

static void
ep_class_init(EPluginClass *klass)
{
	((GObjectClass *)klass)->finalize = ep_finalise;
	klass->construct = ep_construct;
}

GType
e_plugin_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EPluginClass), NULL, NULL, (GClassInitFunc) ep_class_init, NULL, NULL,
			sizeof(EPlugin), 0, (GInstanceInitFunc) NULL,
		};

		ep_parent_class = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EPlugin", &info, 0);
	}
	
	return type;
}

static int
ep_load(const char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	int res = -1;
	EPlugin *ep;

	doc = xmlParseFile(filename);
	if (doc == NULL) {
		return -1;
	}

	root = xmlDocGetRootElement(doc);
	if (strcmp(root->name, "e-plugin-list") != 0)
		goto fail;

	root = root->children;
	while (root) {
		if (strcmp(root->name, "e-plugin") == 0) {
			char *prop;
			EPluginClass *klass;

			/* we only support type=shlib anyway */
			prop = xmlGetProp(root, "type");
			if (prop == NULL)
				goto fail;

			klass = g_hash_table_lookup(ep_types, prop);
			if (klass == NULL) {
				g_warning("can't find plugin type '%s'\n", prop);
				xmlFree(prop);
				goto fail;
			}

			xmlFree(prop);

			ep = g_object_new(G_TYPE_FROM_CLASS(klass), NULL);
			if (e_plugin_construct(ep, root) == -1) {
				g_object_unref(ep);
			} else {
				/* ... */
			}
		}
		root = root->next;
	}

	res = 0;
fail:
	xmlFreeDoc(doc);
	return res;
}

int
e_plugin_load_plugins(const char *path)
{
	DIR *dir;
	struct dirent *d;

	if (ep_types == NULL) {
		g_warning("no plugin types defined");
		return 0;
	}

	printf("scanning plugin dir '%s'\n", path);

	dir = opendir(path);
	if (dir == NULL) {
		g_warning("Could not find plugin path: %s", path);
		return -1;
	}

	while ( (d = readdir(dir)) ) {
		if (strlen(d->d_name) > 6
		    && !strcmp(d->d_name + strlen(d->d_name) - 6, ".eplug")) {
			char * name = g_build_filename(path, d->d_name, NULL);

			ep_load(name);
			g_free(name);
		}
	}

	closedir(dir);

	return 0;
}

void
e_plugin_register_type(GType type)
{
	EPluginClass *klass;

	if (ep_types == NULL)
		ep_types = g_hash_table_new(g_str_hash, g_str_equal);

	klass = g_type_class_ref(type);

	pd(printf("register plugin type '%s'\n", klass->type));

	g_hash_table_insert(ep_types, (void *)klass->type, klass);
}

int
e_plugin_construct(EPlugin *ep, xmlNodePtr root)
{
	return ((EPluginClass *)G_OBJECT_GET_CLASS(ep))->construct(ep, root);
}

void *
e_plugin_invoke(EPlugin *ep, const char *name, void *data)
{
	return ((EPluginClass *)G_OBJECT_GET_CLASS(ep))->invoke(ep, name, data);
}

/* crappy utils to map between xml and 'system' memory */
char *
e_plugin_xml_prop(xmlNodePtr node, const char *id)
{
	char *p = xmlGetProp(node, id);

	if (g_mem_is_system_malloc()) {
		return p;
	} else {
		char * out = g_strdup(p);

		if (p)
			xmlFree(p);
		return out;
	}
}

char *
e_plugin_xml_content(xmlNodePtr node)
{
	char *p = xmlNodeGetContent(node);

	if (g_mem_is_system_malloc()) {
		return p;
	} else {
		char * out = g_strdup(p);

		if (p)
			xmlFree(p);
		return out;
	}
}

/* ********************************************************************** */
static void *epl_parent_class;

#define epl ((EPluginLib *)ep)

static void *
epl_invoke(EPlugin *ep, const char *name, void *data)
{
	void *(*cb)(EPlugin *ep, void *data);

	if (epl->module == NULL
	    && (epl->module = g_module_open(epl->location, 0)) == NULL) {
		g_warning("can't load plugin '%s'", g_module_error());
		return NULL;
	}

	if (!g_module_symbol(epl->module, name, (void *)&cb))
		return NULL;

	return cb(ep, data);
}

static int
epl_construct(EPlugin *ep, xmlNodePtr root)
{
	if (((EPluginClass *)epl_parent_class)->construct(ep, root) == -1)
		return -1;

	epl->location = e_plugin_xml_prop(root, "location");

	if (epl->location == NULL)
		return -1;

	return 0;
}

static void
epl_finalise(GObject *o)
{
	EPlugin *ep = (EPlugin *)o;

	g_free(epl->location);

	if (epl->module)
		g_module_close(epl->module);

	((GObjectClass *)epl_parent_class)->finalize(o);
}

static void
epl_class_init(EPluginClass *klass)
{
	((GObjectClass *)klass)->finalize = epl_finalise;
	klass->construct = epl_construct;
	klass->invoke = epl_invoke;
	klass->type = "shlib";
}

GType
e_plugin_lib_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EPluginLibClass), NULL, NULL, (GClassInitFunc) epl_class_init, NULL, NULL,
			sizeof(EPluginLib), 0, (GInstanceInitFunc) NULL,
		};

		epl_parent_class = g_type_class_ref(e_plugin_get_type());
		type = g_type_register_static(e_plugin_get_type(), "EPluginLib", &info, 0);
	}
	
	return type;
}

/* ********************************************************************** */
static void *eph_parent_class;
static GHashTable *eph_types;

static int
eph_construct(EPluginHook *eph, EPlugin *ep, xmlNodePtr root)
{
	eph->plugin = ep;

	return 0;
}

static void
eph_finalise(GObject *o)
{
	((GObjectClass *)eph_parent_class)->finalize((GObject *)o);
}

static void
eph_class_init(EPluginHookClass *klass)
{
	((GObjectClass *)klass)->finalize = eph_finalise;
	klass->construct = eph_construct;
}

GType
e_plugin_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EPluginHookClass), NULL, NULL, (GClassInitFunc) eph_class_init, NULL, NULL,
			sizeof(EPluginHook), 0, (GInstanceInitFunc) NULL,
		};

		eph_parent_class = g_type_class_ref(G_TYPE_OBJECT);
		type = g_type_register_static(G_TYPE_OBJECT, "EPluginHook", &info, 0);
	}
	
	return type;
}

EPluginHook *
e_plugin_hook_new(EPlugin *ep, xmlNodePtr root)
{
	EPluginHookClass *type;
	char *class;
	EPluginHook *hook;

	if (eph_types == NULL)
		return NULL;

	class = xmlGetProp(root, "class");
	if (class == NULL)
		return NULL;

	type = g_hash_table_lookup(eph_types, class);
	g_free(class);
	if (type == NULL)
		return NULL;

	hook = g_object_new(G_OBJECT_CLASS_TYPE(type), NULL);
	if (type->construct(hook, ep, root) == -1) {
		g_object_unref(hook);
		hook = NULL;
	}

	return hook;
}

void
e_plugin_hook_register_type(GType type)
{
	EPluginHookClass *klass;

	if (eph_types == NULL)
		eph_types = g_hash_table_new(g_str_hash, g_str_equal);

	klass = g_type_class_ref(type);

	phd(printf("register plugin hook type '%s'\n", klass->id));

	g_hash_table_insert(eph_types, (void *)klass->id, klass);
}

guint32
e_plugin_hook_mask(xmlNodePtr root, const struct _EPluginHookTargetKey *map, const char *prop)
{
	char *val, *p, *start, c;
	guint32 mask = 0;

	val = xmlGetProp(root, prop);
	if (val == NULL)
		return 0;

	p = val;
	do {
		start = p;
		while (*p && *p != ',')
			p++;
		c = *p;
		*p = 0;
		if (start != p) {
			int i;

			for (i=0;map[i].key;i++) {
				if (!strcmp(map[i].key, start)) {
					mask |= map[i].value;
					break;
				}
			}
		}
		*p++ = c;
	} while (c);

	xmlFree(val);

	return mask;
}

guint32
e_plugin_hook_id(xmlNodePtr root, const struct _EPluginHookTargetKey *map, const char *prop)
{
	char *val;
	int i;

	val = xmlGetProp(root, prop);
	if (val == NULL)
		return ~0;

	for (i=0;map[i].key;i++) {
		if (!strcmp(map[i].key, val)) {
			xmlFree(val);
			return map[i].value;
		}
	}

	xmlFree(val);

	return ~0;
}

#if 0
/*
  e-mail-format-handler
    mime_type
    target
*/
struct _EMFormatPlugin {
	EPlugin plugin;

	char *target;
	char *mime_type;
	struct _EMFormatHandler *(*get_handler)(void);
};

struct _EMFormatPluginClass {
	EPluginClass plugin_class;
};

#endif

#if 0
void em_setup_plugins(void);

void
em_setup_plugins(void)
{
	GType *e_plugin_mono_get_type(void);

	e_plugin_register_type(e_plugin_lib_get_type());
	e_plugin_register_type(e_plugin_mono_get_type());

	e_plugin_hook_register_type(em_popup_hook_get_type());

	e_plugin_load_plugins(".");
}
#endif
