/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* evolution-shell-view.c
 *
 * Copyright (C) 2000  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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
 * Author: Ettore Perazzoli
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtksignal.h>

#include <gal/util/e-util.h>

#include "evolution-shell-view.h"


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _EvolutionShellViewPrivate {
	int dummy;
};

enum {
	SET_MESSAGE,
	UNSET_MESSAGE,
	CHANGE_VIEW,
	SET_TITLE,
	SET_FOLDER_BAR_LABEL,
	LAST_SIGNAL
};
static int signals[LAST_SIGNAL] = { 0 };


/* CORBA interface implementation.  */

static POA_GNOME_Evolution_ShellView__vepv ShellView_vepv;

static POA_GNOME_Evolution_ShellView *
create_servant (void)
{
	POA_GNOME_Evolution_ShellView *servant;
	CORBA_Environment ev;

	servant = (POA_GNOME_Evolution_ShellView *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &ShellView_vepv;
	CORBA_exception_init (&ev);

	POA_GNOME_Evolution_ShellView__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION) {
		g_free (servant);
		CORBA_exception_free (&ev);
		return NULL;
	}

	CORBA_exception_free (&ev);

	return servant;
}

static void
impl_ShellView_set_message (PortableServer_Servant servant,
			    const CORBA_char *message,
			    const CORBA_boolean busy,
			    CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;

	bonobo_object = bonobo_object_from_servant (servant);
	gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[SET_MESSAGE], message, busy);
}

static void
impl_ShellView_unset_message (PortableServer_Servant servant,
			      CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;

	bonobo_object = bonobo_object_from_servant (servant);
	gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[UNSET_MESSAGE]);
}

static void
impl_ShellView_change_current_view (PortableServer_Servant servant,
				    const CORBA_char *uri,
				    CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;

	bonobo_object = bonobo_object_from_servant (servant);
	gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[CHANGE_VIEW],
			 uri);
}

static void
impl_ShellView_set_title (PortableServer_Servant servant,
			  const CORBA_char *title,
			  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;

	bonobo_object = bonobo_object_from_servant (servant);
	gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[SET_TITLE],
			 title);
}

static void
impl_ShellView_set_folder_bar_label (PortableServer_Servant servant,
				     const CORBA_char  *text,
				     CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;

	bonobo_object = bonobo_object_from_servant (servant);
	gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[SET_FOLDER_BAR_LABEL],
			 text);
}


/* GtkObject methods.  */
static void
destroy (GtkObject *object)
{
	EvolutionShellView *shell_view;
	EvolutionShellViewPrivate *priv;

	shell_view = EVOLUTION_SHELL_VIEW (object);
	priv = shell_view->priv;

	g_free (priv);

	(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


static void
corba_class_init (void)
{
	POA_GNOME_Evolution_ShellView__vepv *vepv;
	POA_GNOME_Evolution_ShellView__epv *epv;
	PortableServer_ServantBase__epv *base_epv;

	base_epv = g_new0 (PortableServer_ServantBase__epv, 1);
	base_epv->_private    = NULL;
	base_epv->finalize    = NULL;
	base_epv->default_POA = NULL;

	epv = g_new0 (POA_GNOME_Evolution_ShellView__epv, 1);
	epv->setMessage        = impl_ShellView_set_message;
	epv->unsetMessage      = impl_ShellView_unset_message;
	epv->changeCurrentView = impl_ShellView_change_current_view;
	epv->setTitle          = impl_ShellView_set_title;
	epv->setFolderBarLabel = impl_ShellView_set_folder_bar_label;

	vepv = &ShellView_vepv;
	vepv->_base_epv               = base_epv;
	vepv->Bonobo_Unknown_epv      = bonobo_object_get_epv ();
	vepv->GNOME_Evolution_ShellView_epv = epv;
}

static void
class_init (EvolutionShellViewClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = destroy;

	signals[SET_MESSAGE]
		= gtk_signal_new ("set_message",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EvolutionShellViewClass, set_message),
				  gtk_marshal_NONE__POINTER_INT,
				  GTK_TYPE_NONE, 2,
				  GTK_TYPE_STRING,
				  GTK_TYPE_BOOL);

	signals[UNSET_MESSAGE]
		= gtk_signal_new ("unset_message",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EvolutionShellViewClass, unset_message),
				  gtk_marshal_NONE__NONE,
				  GTK_TYPE_NONE, 0);

	signals[CHANGE_VIEW]
		= gtk_signal_new ("change_current_view",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EvolutionShellViewClass, change_current_view),
				  gtk_marshal_NONE__POINTER,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[SET_TITLE]
		= gtk_signal_new ("set_title",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EvolutionShellViewClass, set_title),
				  gtk_marshal_NONE__POINTER,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	signals[SET_FOLDER_BAR_LABEL]
		= gtk_signal_new ("set_folder_bar_label",
				  GTK_RUN_FIRST,
				  object_class->type,
				  GTK_SIGNAL_OFFSET (EvolutionShellViewClass, set_folder_bar_label),
				  gtk_marshal_NONE__POINTER,
				  GTK_TYPE_NONE, 1,
				  GTK_TYPE_STRING);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	parent_class = gtk_type_class (bonobo_object_get_type ());

	corba_class_init ();
}

static void
init (EvolutionShellView *shell_view)
{
	EvolutionShellViewPrivate *priv;

	priv = g_new (EvolutionShellViewPrivate, 1);
	priv->dummy = 0;

	shell_view->priv = priv;
}


/**
 * evolution_shell_view_construct:
 * @shell_view: 
 * @corba_object: 
 * 
 * Construct @shell_view with the specified @corba_object.
 **/
void
evolution_shell_view_construct (EvolutionShellView *shell_view,
				GNOME_Evolution_ShellView corba_object)
{
	g_return_if_fail (shell_view != NULL);
	g_return_if_fail (EVOLUTION_IS_SHELL_VIEW (shell_view));

	bonobo_object_construct (BONOBO_OBJECT (shell_view), corba_object);
}

/**
 * evolution_shell_view_new:
 *
 * Create a new EvolutionShellView object.
 * 
 * Return value: The new EvolutionShellView object.
 **/
EvolutionShellView *
evolution_shell_view_new (void)
{
	POA_GNOME_Evolution_ShellView *servant;
	GNOME_Evolution_ShellView corba_object;
	EvolutionShellView *new;

	servant = create_servant ();
	if (servant == NULL)
		return NULL;

	new = gtk_type_new (evolution_shell_view_get_type ());

	corba_object = bonobo_object_activate_servant (BONOBO_OBJECT (new), servant);

	evolution_shell_view_construct (new, corba_object);

	return new;
}


E_MAKE_TYPE (evolution_shell_view, "EvolutionShellView", EvolutionShellView, class_init, init, PARENT_TYPE)
