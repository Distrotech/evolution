/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors: Michel Zucchi <notzed@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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

#ifndef __EM_POPUP_H__
#define __EM_POPUP_H__

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* NB: This is TEMPORARY, to be replaced by EggMenu, if it does what we need? */
enum _em_popup_t {
	EM_POPUP_ITEM = 0,
	EM_POPUP_TOGGLE,
	EM_POPUP_RADIO,
	EM_POPUP_IMAGE,
	EM_POPUP_SUBMENU,
	EM_POPUP_BAR,
	EM_POPUP_TYPE_MASK = 0xffff,
	EM_POPUP_ACTIVE = 0x10000,
};

typedef struct _EMPopupItem EMPopupItem;
typedef struct _EMPopup EMPopup;
typedef struct _EMPopupClass EMPopupClass;

struct _EMPopupItem {
	enum _em_popup_t type;
	char *path;		/* absolute path! must sort ascii-lexographically into the right spot */
	char *label;
	GCallback activate;
	void *activate_data;
	void *image;		/* char* for item type, GtkWidget * for image type */
	guint32 mask;
};

struct _EMPopup {
	GObject object;

	struct _EMPopupPrivate *priv;
};

struct _EMPopupClass {
	GObjectClass object_class;
};

GType em_popup_get_type(void);

EMPopup *em_popup_new(void);
void em_popup_add_items(EMPopup *, GSList *items, GDestroyNotify freefunc);
struct _GtkMenu *em_popup_create_menu(EMPopup *, guint32 hide_mask, guint32 disable_mask);
struct _GtkMenu *em_popup_create_menu_once(EMPopup *emp, guint32 hide_mask, guint32 disable_mask);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EM_POPUP_H__ */
