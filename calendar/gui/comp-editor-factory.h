/* Evolution calendar - Component editor factory object
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef COMP_EDITOR_FACTORY_H
#define COMP_EDITOR_FACTORY_H

#include <bonobo/bonobo-xobject.h>
#include "evolution-calendar.h"



#define TYPE_COMP_EDITOR_FACTORY            (comp_editor_factory_get_type ())
#define COMP_EDITOR_FACTORY(obj)            (GTK_CHECK_CAST ((obj), TYPE_COMP_EDITOR_FACTORY,	\
					     CompEditorFactory))
#define COMP_EDITOR_FACTORY_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass),			\
					     TYPE_COMP_EDITOR_FACTORY, CompEditorFactoryClass))
#define IS_COMP_EDITOR_FACTORY(obj)         (GTK_CHECK_TYPE ((obj), TYPE_COMP_EDITOR_FACTORY))
#define IS_COMP_EDITOR_FACTORY_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), TYPE_COMP_EDITOR_FACTORY))

typedef struct CompEditorFactoryPrivate CompEditorFactoryPrivate;

typedef struct {
	BonoboXObject xobject;

	/* Private data */
	CompEditorFactoryPrivate *priv;
} CompEditorFactory;

typedef struct {
	BonoboXObjectClass parent_class;

	POA_GNOME_Evolution_Calendar_CompEditorFactory__epv epv;
} CompEditorFactoryClass;

GtkType comp_editor_factory_get_type (void);

CompEditorFactory *comp_editor_factory_new (void);



#endif
