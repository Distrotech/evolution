/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-minicard.h
 * Copyright (C) 2000  Ximian, Inc.
 * Author: Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __E_MINICARD_H__
#define __E_MINICARD_H__

#include <libgnomeui/gnome-canvas.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "addressbook/gui/contact-editor/e-contact-editor.h"
#include "addressbook/backend/ebook/e-card.h"
#include "addressbook/backend/ebook/e-card-simple.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

/* EMinicard - A small card displaying information about a contact.
 *
 * The following arguments are available:
 *
 * name		type		read/write	description
 * --------------------------------------------------------------------------------
 * width        double          RW              width of the card
 * height       double          R               height of the card
 * card		ECard*		RW		Pointer to the ECard
 */

#define E_MINICARD_TYPE			(e_minicard_get_type ())
#define E_MINICARD(obj)			(GTK_CHECK_CAST ((obj), E_MINICARD_TYPE, EMinicard))
#define E_MINICARD_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), E_MINICARD_TYPE, EMinicardClass))
#define E_IS_MINICARD(obj)		(GTK_CHECK_TYPE ((obj), E_MINICARD_TYPE))
#define E_IS_MINICARD_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((obj), E_MINICARD_TYPE))


typedef struct _EMinicard       EMinicard;
typedef struct _EMinicardClass  EMinicardClass;
typedef enum _EMinicardFocusType EMinicardFocusType;

enum _EMinicardFocusType {
	E_MINICARD_FOCUS_TYPE_START,
	E_MINICARD_FOCUS_TYPE_END
};

struct _EMinicard
{
	GnomeCanvasGroup parent;
	
	/* item specific fields */
	ECard *card;
	ECardSimple *simple;
	
	GnomeCanvasItem *rect;
	GnomeCanvasItem *header_rect;
	GnomeCanvasItem *header_text;
	GnomeCanvasItem *list_icon;

	GdkPixbuf *list_icon_pixbuf;
	double list_icon_size;

	GtkObject *editor;

	GList *fields; /* Of type EMinicardField */
	guint needs_remodeling : 1;

	guint changed : 1;

	guint selected : 1;
	guint has_cursor : 1;

	guint has_focus : 1;

	guint editable : 1;

	guint drag_button_down : 1;
	gint drag_button;

	gint button_x;
	gint button_y;

	double width;
	double height;
};

struct _EMinicardClass
{
	GnomeCanvasGroupClass parent_class;

	gint (* selected) (EMinicard *minicard, GdkEvent *event);
	gint (* drag_begin) (EMinicard *minicard, GdkEvent *event);
};


GtkType     e_minicard_get_type     (void);
const char *e_minicard_get_card_id  (EMinicard *minicard);
int         e_minicard_compare      (EMinicard *minicard1,
				     EMinicard *minicard2);

int         e_minicard_selected     (EMinicard *minicard,
				     GdkEvent  *event);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __E_MINICARD_H__ */
