/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 *  Copyright 2004 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include <gtk/gtkmenu.h>
#include <gtk/gtkmenuitem.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkradiomenuitem.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimage.h>

#include <libgnome/gnome-url.h>
#include <libgnomevfs/gnome-vfs-mime.h>

#include "em-config.h"
#include "e-util/e-msgport.h"
#include <e-util/e-icon-factory.h>
#include "em-utils.h"
#include "em-composer-utils.h"

#include <camel/camel-store.h>
#include <camel/camel-folder.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-url.h>

#include <camel/camel-vee-folder.h>
#include <camel/camel-vtrash-folder.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include <gal/util/e-util.h>

static GObjectClass *emp_parent;

static void
emp_init(GObject *o)
{
	/*EMConfig *emp = (EMConfig *)o; */
}

static void
emp_finalise(GObject *o)
{
	((GObjectClass *)emp_parent)->finalize(o);
}

static void
emp_target_free(EConfig *ep, EConfigTarget *t)
{
	switch (t->type) {
	case EM_CONFIG_TARGET_FOLDER: {
		EMConfigTargetFolder *s = (EMConfigTargetFolder *)t;

		g_free(s->uri);
		camel_object_unref(s->folder);
		break; }
	}

	((EConfigClass *)emp_parent)->target_free(ep, t);
}

static void
emp_class_init(GObjectClass *klass)
{
	klass->finalize = emp_finalise;
	((EConfigClass *)klass)->target_free = emp_target_free;

	/*e_config_class_add_factory((EConfigClass *)klass, NULL, (EConfigFactoryFunc)emp_standard_menu_factory, NULL);*/
}

GType
em_config_get_type(void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo info = {
			sizeof(EMConfigClass),
			NULL, NULL,
			(GClassInitFunc)emp_class_init,
			NULL, NULL,
			sizeof(EMConfig), 0,
			(GInstanceInitFunc)emp_init
		};
		emp_parent = g_type_class_ref(e_config_get_type());
		type = g_type_register_static(e_config_get_type(), "EMConfig", &info, 0);
	}

	return type;
}

EMConfig *em_config_new(const char *menuid)
{
	EMConfig *emp = g_object_new(em_config_get_type(), 0);

	e_config_construct(&emp->config, menuid);

	return emp;
}

EMConfigTargetFolder *
em_config_target_new_folder(EMConfig *emp, struct _CamelFolder *folder, const char *uri)
{
	EMConfigTargetFolder *t = e_config_target_new(&emp->config, EM_CONFIG_TARGET_FOLDER, sizeof(*t));

	t->uri = g_strdup(uri);
	t->folder = folder;
	camel_object_ref(folder);

	return t;
}

/* ********************************************************************** */

/* Popup menu plugin handler */

/*
<e-plugin
  class="com.ximian.mail.plugin.popup:1.0"
  id="com.ximian.mail.plugin.popup.item:1.0"
  type="shlib"
  location="/opt/gnome2/lib/camel/1.0/libcamelimap.so"
  name="imap"
  description="IMAP4 and IMAP4v1 mail store">
  <hook class="com.ximian.mail.popupMenu:1.0"
        handler="HandlePopup">
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
#define emph ((EMConfigHook *)eph)

static const EConfigHookTargetMask emph_no_masks[] = {
	{ 0 }
};

static const EConfigHookTargetMap emph_targets[] = {
	{ "folder", EM_CONFIG_TARGET_FOLDER, emph_no_masks },
	{ 0 }
};

static void
emph_finalise(GObject *o)
{
	/*EPluginHook *eph = (EPluginHook *)o;*/

	((GObjectClass *)emph_parent_class)->finalize(o);
}

static void
emph_class_init(EPluginHookClass *klass)
{
	int i;

	((GObjectClass *)klass)->finalize = emph_finalise;
	((EPluginHookClass *)klass)->id = "com.novell.evolution.mail.config:1.0";

	for (i=0;emph_targets[i].type;i++)
		e_config_hook_class_add_target_map((EConfigHookClass *)klass, &emph_targets[i]);

	((EConfigHookClass *)klass)->config_class = g_type_class_ref(em_config_get_type());
}

GType
em_config_hook_get_type(void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof(EMConfigHookClass), NULL, NULL, (GClassInitFunc) emph_class_init, NULL, NULL,
			sizeof(EMConfigHook), 0, (GInstanceInitFunc) NULL,
		};

		emph_parent_class = g_type_class_ref(e_config_hook_get_type());
		type = g_type_register_static(e_config_hook_get_type(), "EMConfigHook", &info, 0);
	}
	
	return type;
}
