
#include <sys/types.h>
#include <dirent.h>
#include <string.h>

#include "e-plugin.h"
#include "em-popup.h"

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

static char *
get_xml_prop(xmlNodePtr node, const char *id)
{
	char *p = xmlGetProp(node, id);
	char *out = NULL;

	if (p) {
		out = g_strdup(p);
		xmlFree(p);
	}

	return out;
}

static char *
get_xml_content(xmlNodePtr node)
{
	char *p = xmlNodeGetContent(node);
	char *out = NULL;

	if (p) {
		out = g_strdup(p);
		xmlFree(p);
	}

	return out;
}

static int
ep_construct(EPlugin *ep, xmlNodePtr root)
{
	xmlNodePtr node;
	int res = -1;

	ep->description = get_xml_prop(root, "description");
	ep->name = get_xml_prop(root, "name");

	node = root->children;
	while (node) {
		if (strcmp(node->name, "hook") == 0) {
			struct _EPluginHook *hook;

			hook = e_plugin_hook_new(ep, node);
			if (hook)
				ep->hooks = g_slist_prepend(ep->hooks, hook);
			else
				g_warning("Plugin '%s' failed to load hook", ep->name);
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

			/* we only support type=shlib anyway */
			prop = xmlGetProp(root, "type");
			if (prop == NULL)
				goto fail;

			if (strcmp(prop, "shlib") != 0) {
				xmlFree(prop);
				goto fail;
			}

			xmlFree(prop);

			ep = g_object_new(e_plugin_lib_get_type(), NULL);
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

#if 0
	if (ep_types == NULL) {
		g_warning("no plugin types defined");
		return 0;
	}
#endif

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

#if 0
void
e_plugin_register_type(GType type)
{
	EPluginClass *klass;

	if (ep_types == NULL)
		ep_types = g_hash_table_new(g_str_hash, g_str_equal);

	klass = g_type_class_ref(type);

	pd(printf("register plugin type '%s'\n", klass->id));

	g_hash_table_insert(ep_types, (void *)klass->id, klass);
}
#endif

int
e_plugin_construct(EPlugin *ep, xmlNodePtr root)
{
	return ((EPluginClass *)G_OBJECT_GET_CLASS(ep))->construct(ep, root);
}

void *
e_plugin_find_callback(EPlugin *ep, const char *name)
{
	return ((EPluginClass *)G_OBJECT_GET_CLASS(ep))->find_callback(ep, name);
}

/* ********************************************************************** */
static void *epl_parent_class;

#define epl ((EPluginLib *)ep)

static void *
epl_find_callback(EPlugin *ep, const char *symbol)
{
	void *cb;

	if (epl->module == NULL
	    && (epl->module = g_module_open(epl->location, 0)) == NULL) {
		g_warning("can't load plugin '%s'", g_module_error());
		return NULL;
	}

	if (!g_module_symbol(epl->module, symbol, (void *)&cb))
		return NULL;

	return cb;
}

static int
epl_construct(EPlugin *ep, xmlNodePtr root)
{
	if (((EPluginClass *)epl_parent_class)->construct(ep, root) == -1)
		return -1;

	epl->location = get_xml_prop(root, "location");

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
	klass->find_callback = epl_find_callback;
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
	EPluginClass *klass;

	if (eph_types == NULL)
		eph_types = g_hash_table_new(g_str_hash, g_str_equal);

	klass = g_type_class_ref(type);

	phd(printf("register plugin hook type '%s'\n", klass->id));

	g_hash_table_insert(eph_types, (void *)klass->id, klass);
}

/* ********************************************************************** */

/*
<e-plugin
  class="com.ximian.mail.plugin.popup:1.0"
  id="com.ximian.mail.plugin.popup.item:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  name="imap"
  description="IMAP4 and IMAP4v1 mail store">
  <hook class="com.ximian.mail.popupMenu:1.0">
  <menu id="any" target="select">
   <item
    type="item|toggle|radio|image|submenu|bar"
    active
    path="foo/bar"
    label="label"
    icon="foo"
    mask="select_one"
    activate="emp_view_emacs"/>
  </menu>
  </extension>

*/

static void *emph_parent_class;
#define emph ((EMPopupHook *)eph)

/* must have 1:1 correspondence with em-popup types */
static const char * emph_item_types[] = { "item", "toggle", "radio", "image", "submenu", "bar", NULL };
static const char * emph_target_types[] = { "select", "uri", "part", "folder", NULL };
static const char * emph_select_mask[] = {
	"dummy",
	"one", "many", "mark_read", "mark_unread",
	"delete", "undelete", "mailing_list",
	"resend", "mark_important", "mark_unimportant",
	"flag_followup", "flag_completed", "flag_clear",
	"add_sender", "mark_junk", "mark_nojunk", "folder", NULL
};
static const char * emph_uri_mask[] = { "http", "mailto", "notmailto", NULL };
static const char * emph_part_mask[] = { "message", "image", NULL };
static const char * emph_folder_mask[] = { "folder", "store", "inferiors", "delete", "select", NULL };
static const char ** emph_masks[] = { emph_select_mask, emph_uri_mask, emph_part_mask, emph_folder_mask };

static guint32
emph_mask(xmlNodePtr root, const char **vals, const char *prop)
{
	char *val, *p, *start, c;
	guint32 mask = 0;

	val = xmlGetProp(root, prop);
	if (val == NULL)
		return 0;

	printf(" mask '%s' = ", val);

	p = val;
	do {
		start = p;
		while (*p && *p != ',')
			p++;
		c = *p;
		*p = 0;
		if (start != p) {
			int i;

			for (i=0;vals[i];i++) {
				if (!strcmp(vals[i], start)) {
					mask |= (1<<i);
					break;
				}
			}
		}
		*p++ = c;
	} while (c);

	xmlFree(val);

	printf("%08x\n", mask);

	return mask;
}

static int
emph_index(xmlNodePtr root, const char **vals, const char *prop)
{
	int i = 0;
	char *val;

	val = xmlGetProp(root, prop);
	if (val == NULL) {
		printf(" can't find prop '%s'\n", prop);
		return -1;
	}

	printf("looking up index of '%s'", val);

	while (vals[i]) {
		if (!strcmp(vals[i], val)) {
			printf(" = %d\n", i);
			xmlFree(val);
			return i;
		}
		i++;
	}

	printf(" not found\n");

	xmlFree(val);
	return -1;
}

static void
emph_popup_activate(void *widget, void *data)
{
	struct _EMPopupHookItem *item = data;

	if (item->activate_func == NULL) {
		printf(" looking up callback '%s'\n", item->activate);
		item->activate_func = e_plugin_find_callback(item->hook->hook.plugin, item->activate);
	}
	if (item->activate_func == NULL) {
		printf(" can't find callback '%s'\n", item->activate);
		return;
	}

	item->activate_func(item->hook->hook.plugin, item->target);
}

static void
emph_popup_factory(EMPopup *emp, EMPopupTarget *target, void *data)
{
	struct _EMPopupHookMenu *menu = data;
	GSList *l, *menus = NULL;

	printf("popup factory called %s mask %08x\n", menu->id?menu->id:"all menus", target->mask);

	if (target->type != menu->target_type)
		return;

	l = menu->items;
	while (l) {
		struct _EMPopupHookItem *item = l->data;

		item->target = target;
		printf("  adding menyu item '%s' %08x\n", item->item.label, item->item.mask);
		menus = g_slist_prepend(menus, item);
		l = l->next;
	}

	if (menus)
		em_popup_add_items(emp, menus, (GDestroyNotify)g_slist_free);
}

static void
emph_free_item(struct _EMPopupHookItem *item)
{
	g_free(item->item.path);
	g_free(item->item.label);
	g_free(item->item.image);
	g_free(item->activate);
	g_free(item);
}

static void
emph_free_menu(struct _EMPopupHookMenu *menu)
{
	g_slist_foreach(menu->items, (GFunc)emph_free_item, NULL);
	g_slist_free(menu->items);

	g_free(menu->id);
	g_free(menu);
}

static struct _EMPopupHookItem *
emph_construct_item(EPluginHook *eph, EMPopupHookMenu *menu, xmlNodePtr root)
{
	struct _EMPopupHookItem *item;

	printf("  loading menu item\n");
	item = g_malloc0(sizeof(*item));
	if ((item->item.type = emph_index(root, emph_item_types, "type")) == -1
	    || item->item.type == EM_POPUP_IMAGE)
		goto error;
	item->item.path = get_xml_prop(root, "path");
	item->item.label = get_xml_prop(root, "label");
	item->item.image = get_xml_prop(root, "icon");
	item->item.mask = emph_mask(root, emph_masks[menu->target_type], "mask");
	item->activate = get_xml_prop(root, "activate");

	item->item.activate = G_CALLBACK(emph_popup_activate);
	item->item.activate_data = item;
	item->hook = emph;

	printf("   path=%s\n", item->item.path);
	printf("   label=%s\n", item->item.label);

	return item;
error:
	printf("error!\n");
	emph_free_item(item);
	return NULL;
}

static struct _EMPopupHookMenu *
emph_construct_menu(EPluginHook *eph, xmlNodePtr root)
{
	struct _EMPopupHookMenu *menu;
	xmlNodePtr node;

	printf(" loading menu\n");
	menu = g_malloc0(sizeof(*menu));
	if ((menu->target_type = emph_index(root, emph_target_types, "target")) == -1)
		goto error;
	menu->id = get_xml_prop(root, "id");
	node = root->children;
	while (node) {
		if (0 == strcmp(node->name, "item")) {
			struct _EMPopupHookItem *item;

			item = emph_construct_item(eph, menu, node);
			if (item)
				menu->items = g_slist_append(menu->items, item);
		}
		node = node->next;
	}

	return menu;
error:
	emph_free_menu(menu);
	return NULL;
}

static int
emph_construct(EPluginHook *eph, EPlugin *ep, xmlNodePtr root)
{
	xmlNodePtr node;

	printf("loading popup hook\n");

	if (((EPluginHookClass *)emph_parent_class)->construct(eph, ep, root) == -1)
		return -1;

	node = root->children;
	while (node) {
		if (strcmp(node->name, "menu") == 0) {
			struct _EMPopupHookMenu *menu;

			menu = emph_construct_menu(eph, node);
			if (menu) {
				em_popup_static_add_factory(menu->id, emph_popup_factory, menu);
				emph->menus = g_slist_append(emph->menus, menu);
			}
		}
		node = node->next;
	}

	eph->plugin = ep;

	return 0;
}

static void
emph_finalise(GObject *o)
{
	EPluginHook *eph = (EPluginHook *)o;

	g_slist_foreach(emph->menus, (GFunc)emph_free_menu, NULL);
	g_slist_free(emph->menus);

	((GObjectClass *)emph_parent_class)->finalize(o);
}

static void
emph_class_init(EPluginHookClass *klass)
{
	((GObjectClass *)klass)->finalize = emph_finalise;
	klass->construct = emph_construct;

	klass->id = "com.ximian.evolution.mail.popup:1.0";
}

GType
em_popup_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EMPopupHookClass), NULL, NULL, (GClassInitFunc) emph_class_init, NULL, NULL,
			sizeof(EMPopupHook), 0, (GInstanceInitFunc) NULL,
		};

		emph_parent_class = g_type_class_ref(e_plugin_hook_get_type());
		type = g_type_register_static(e_plugin_hook_get_type(), "EMPopupHook", &info, 0);
	}
	
	return type;
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

void em_setup_plugins(void);

void
em_setup_plugins(void)
{
	e_plugin_hook_register_type(em_popup_hook_get_type());

	e_plugin_load_plugins(".");
}
