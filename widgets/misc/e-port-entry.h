/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 *
 * Authors:
 *	Dan Vratil <dvratil@redhat.com>
 */

#ifndef E_PORT_ENTRY_H
#define E_PORT_ENTRY_H

#include <gtk/gtk.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define E_TYPE_PORT_ENTRY \
	(e_port_entry_get_type ())
#define E_PORT_ENTRY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_PORT_ENTRY, EPortEntry))
#define E_PORT_ENTRY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_PORT_ENTRY, EPortEntryClass))
#define E_IS_PORT_ENTRY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_PORT_ENTRY))
#define E_IS_PORT_ENTRY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_PORT_ENTRY))
#define E_PORT_ENTRY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_PORT_ENTRY, EPortEntryClass))

G_BEGIN_DECLS

typedef struct _EPortEntry EPortEntry;
typedef struct _EPortEntryClass EPortEntryClass;
typedef struct _EPortEntryPrivate EPortEntryPrivate;

struct _EPortEntry {
	GtkComboBox parent;
	EPortEntryPrivate *priv;
};

struct _EPortEntryClass {
	GtkComboBoxClass parent_class;
};

GType		e_port_entry_get_type		(void) G_GNUC_CONST;
GtkWidget *	e_port_entry_new		(void);
void		e_port_entry_set_camel_entries	(EPortEntry *pentry,
						 CamelProviderPortEntry *entries);
void		e_port_entry_security_port_changed
						(EPortEntry *pentry,
						 gchar *ssl);
gint		e_port_entry_get_port		(EPortEntry *pentry);
void		e_port_entry_set_port		(EPortEntry *pentry, gint port);
gboolean	e_port_entry_is_valid		(EPortEntry *pentry);
void		e_port_entry_activate_secured_port
						(EPortEntry *pentry,
						 gint index);
void		e_port_entry_activate_nonsecured_port
						(EPortEntry *pentry,
						 gint index);

G_END_DECLS

#endif /* E_PORT_ENTRY_H */
