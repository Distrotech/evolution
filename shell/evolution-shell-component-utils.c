/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-shell-component-utils.c
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "evolution-shell-component-utils.h"

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-moniker-util.h>
#include <bonobo/bonobo-exception.h>
#include <liboaf/oaf.h>
#include <gal/widgets/e-gui-utils.h>

static void free_pixmaps (void);
static GSList *inited_arrays = NULL;

void e_pixmaps_update (BonoboUIComponent *uic, EPixmap *pixcache)
{
	static int done_init = 0;
	int i;

	if (!done_init) {
		g_atexit (free_pixmaps);
		done_init = 1;
	}

	if (g_slist_find (inited_arrays, pixcache) == NULL)
		inited_arrays = g_slist_prepend (inited_arrays, pixcache);

	for (i = 0; pixcache [i].path; i++) {
		if (!pixcache [i].pixbuf) {
			char *path;
			GdkPixbuf *pixbuf;

			path = g_concat_dir_and_file (EVOLUTION_IMAGES,
						      pixcache [i].fname);

			pixbuf = gdk_pixbuf_new_from_file (path);
			if (pixbuf == NULL) {
				g_warning ("Cannot load image -- %s", path);
			} else {
				pixcache [i].pixbuf = bonobo_ui_util_pixbuf_to_xml (pixbuf);
				gdk_pixbuf_unref (pixbuf);
				bonobo_ui_component_set_prop (uic,
					pixcache [i].path, "pixname",
					pixcache [i].pixbuf, NULL);
			}

			g_free (path);
		} else {
			bonobo_ui_component_set_prop (uic, pixcache [i].path,
						      "pixname",
						      pixcache [i].pixbuf,
						      NULL);
		}
	}
}

static void
free_pixmaps (void)
{
	int i;
	GSList *li;

	for (li = inited_arrays; li != NULL; li = li->next) {
		EPixmap *pixcache = li->data;
		for (i = 0; pixcache [i].path; i++)
			g_free (pixcache [i].pixbuf);
	}

	g_slist_free (inited_arrays);
}


/**
 * e_activation_failure_dialog:
 * @parent: parent window of the dialog, or %NULL
 * @msg: the context-specific part of the error message
 * @oafiid: the OAFIID of the component that failed to start
 * @repo_id: the repo_id of the component that failed to start
 *
 * This puts up an error dialog about a failed component activation
 * containing as much information as we can manage to gather about
 * why it failed.
 **/
void
e_activation_failure_dialog (GtkWindow *parent, const char *msg,
			     const char *oafiid, const char *repo_id)
{
	Bonobo_Unknown object;
	CORBA_Environment ev;
	char *errmsg;

	CORBA_exception_init (&ev);
	object = bonobo_get_object (oafiid, repo_id, &ev);
	if (ev._major == CORBA_NO_EXCEPTION) {
		if (object) {
			Bonobo_Unknown_unref (object, &ev);
			CORBA_Object_release (object, &ev);
		}
		errmsg = g_strdup_printf (_("%s\n\nUnknown error."), msg);
	} else if (strcmp (CORBA_exception_id (&ev), ex_OAF_GeneralError) != 0) {
		char *bonobo_err = bonobo_exception_get_text (&ev);
		errmsg = g_strdup_printf (_("%s\n\nThe error from the "
					    "component system is:\n%s"),
					  msg, bonobo_err);
		g_free (bonobo_err);
	} else {
		OAF_GeneralError *errval = CORBA_exception_value (&ev);

		errmsg = g_strdup_printf (_("%s\n\nThe error from the "
					    "activation system is:\n%s"),
					  msg, errval->description);
	}
	CORBA_exception_free (&ev);

	e_notice (parent, GNOME_MESSAGE_BOX_ERROR, errmsg);
	g_free (errmsg);
}
