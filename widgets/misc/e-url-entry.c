/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-url-entry.c
 *
 * Copyright (C) 2002  JP Rosevear
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: JP Rosevear
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <libgnome/gnome-url.h>
#include "e-url-entry.h"

struct _EUrlEntryPrivate {
	GtkWidget *entry;
	GtkWidget *button;
};

static void class_init (EUrlEntryClass *klass);
static void init (EUrlEntry *url_entry);
static void destroy (GtkObject *obj);

static void button_clicked_cb (GtkWidget *widget, gpointer data);

static GtkHBoxClass *parent_class = NULL;


GtkType
e_url_entry_get_type (void)
{
  static GtkType type = 0;

  if (type == 0)
    {
      static const GtkTypeInfo info =
      {
        "EUrlEntry",
        sizeof (EUrlEntry),
        sizeof (EUrlEntryClass),
        (GtkClassInitFunc) class_init,
        (GtkObjectInitFunc) init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      type = gtk_type_unique (gtk_hbox_get_type (), &info);
    }

  return type;
}

static void
class_init (EUrlEntryClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);

	parent_class = g_type_class_ref(gtk_hbox_get_type ());
	
	object_class->destroy = destroy;
}


static void
init (EUrlEntry *url_entry)
{
	EUrlEntryPrivate *priv;
	GtkWidget *pixmap;

	priv = g_new0 (EUrlEntryPrivate, 1);
	url_entry->priv = priv;

	priv->entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (url_entry), priv->entry, TRUE, TRUE, 0);
	priv->button = gtk_button_new ();
	gtk_box_pack_start (GTK_BOX (url_entry), priv->button, FALSE, FALSE, 0);
	pixmap = gtk_image_new_from_file (MAP_DIR "/connect_to_url-16.xpm");
	gtk_container_add (GTK_CONTAINER (priv->button), pixmap);
	gtk_widget_show (pixmap);

	gtk_widget_show (priv->button);
	gtk_widget_show (priv->entry);
	
	g_signal_connect((priv->button), "clicked",
			    G_CALLBACK (button_clicked_cb), url_entry);
}

static void
destroy (GtkObject *obj)
{
	EUrlEntry *url_entry;
	
	url_entry = E_URL_ENTRY (obj);
	if (url_entry->priv) {
		g_free (url_entry->priv);
		url_entry->priv = NULL;
	}
}



GtkWidget *
e_url_entry_new (void)
{
	return gtk_type_new (E_TYPE_URL_ENTRY);
}


GtkWidget *
e_url_entry_get_entry (EUrlEntry *url_entry)
{
	EUrlEntryPrivate *priv;
	
	g_return_val_if_fail (url_entry != NULL, NULL);
	g_return_val_if_fail (E_IS_URL_ENTRY (url_entry), NULL);

	priv = url_entry->priv;
	
	return priv->entry;
}

static void
button_clicked_cb (GtkWidget *widget, gpointer data)
{
	EUrlEntry *url_entry;
	EUrlEntryPrivate *priv;
	char *url;
	
	url_entry = E_URL_ENTRY (data);
	priv = url_entry->priv;
	
	url = gtk_editable_get_chars (GTK_EDITABLE (priv->entry), 0, -1);
	gnome_url_show (url, NULL);
	g_free (url);
}
