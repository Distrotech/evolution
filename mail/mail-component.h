/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* mail-component.h
 *
 * Copyright (C) 2003  Ettore Perazzoli
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
 *   Ettore Perazzoli <ettore@ximian.com>
 */

#ifndef _MAIL_COMPONENT_H_
#define _MAIL_COMPONENT_H_

#include <bonobo/bonobo-object.h>

#include <camel/camel-store.h>

#include "e-storage.h"
#include "Evolution.h"


#define MAIL_TYPE_COMPONENT			(mail_component_get_type ())
#define MAIL_COMPONENT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MAIL_TYPE_COMPONENT, MailComponent))
#define MAIL_COMPONENT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), MAIL_TYPE_COMPONENT, MailComponentClass))
#define MAIL_IS_COMPONENT(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAIL_TYPE_COMPONENT))
#define MAIL_IS_COMPONENT_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), MAIL_TYPE_COMPONENT))


typedef struct _MailComponent        MailComponent;
typedef struct _MailComponentPrivate MailComponentPrivate;
typedef struct _MailComponentClass   MailComponentClass;

struct _MailComponent {
	BonoboObject parent;

	MailComponentPrivate *priv;
};

struct _MailComponentClass {
	BonoboObjectClass parent_class;

	POA_GNOME_Evolution_Component__epv epv;
};


GType  mail_component_get_type  (void);

MailComponent *mail_component_peek  (void);

const char *mail_component_peek_base_directory  (MailComponent *component);

void mail_component_add_store (MailComponent *component,
			       CamelStore *store,
			       const char *name,
			       const char *uri);

void  mail_component_load_storage_by_uri    (MailComponent *component,
					     const char    *uri,
					     const char    *name);
void  mail_component_remove_storage         (MailComponent *component,
					     CamelStore    *store);
void  mail_component_remove_storage_by_uri  (MailComponent *component,
					     const char    *uri);

EStorage *mail_component_lookup_storage  (MailComponent *component,
					  CamelStore    *store);

int   mail_component_get_storage_count  (MailComponent *component);

void  mail_component_storages_foreach   (MailComponent *component,
					 GHFunc         func,
					 void          *data);

#endif /* _MAIL_COMPONENT_H_ */
