/*
 * evolution-wizard.c
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Iain Holmes  <iain@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkmarshal.h>
#include <gtk/gtktypeutils.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-event-source.h>

#include "evolution-wizard.h"
#include "Evolution.h"

struct _EvolutionWizardPrivate {
	EvolutionWizardGetControlFn get_fn;
	BonoboEventSource *event_source;

	void *closure;
	int page_count;
};

enum {
	NEXT,
	PREPARE,
	BACK,
	FINISH,
	CANCEL,
	HELP,
	LAST_SIGNAL
};

#define PARENT_TYPE BONOBO_X_OBJECT_TYPE

static GtkObjectClass *parent_class;
static guint32 signals[LAST_SIGNAL] = { 0 };

static CORBA_long
impl_GNOME_Evolution_Wizard__get_pageCount (PortableServer_Servant servant,
					    CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EvolutionWizard *wizard;
	EvolutionWizardPrivate *priv;

	bonobo_object = bonobo_object_from_servant (servant);
	wizard = EVOLUTION_WIZARD (bonobo_object);
	priv = wizard->priv;

	return priv->page_count;
}

static Bonobo_Control
impl_GNOME_Evolution_Wizard_getControl (PortableServer_Servant servant,
					CORBA_long pagenumber,
					CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EvolutionWizard *wizard;
	EvolutionWizardPrivate *priv;
	BonoboControl *control;

	bonobo_object = bonobo_object_from_servant (servant);
	wizard = EVOLUTION_WIZARD (bonobo_object);
	priv = wizard->priv;

	if (pagenumber < 0 || pagenumber >= priv->page_count) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Wizard_NoPage, NULL);
		return CORBA_OBJECT_NIL;
	}

	control = priv->get_fn (wizard, pagenumber, priv->closure);
	if (control == NULL)
		return CORBA_OBJECT_NIL;

	return (Bonobo_Control) CORBA_Object_duplicate (BONOBO_OBJREF (control), ev);
}

static void
impl_GNOME_Evolution_Wizard_notifyAction (PortableServer_Servant servant,
					  CORBA_long pagenumber,
					  GNOME_Evolution_Wizard_Action action,
					  CORBA_Environment *ev)
{
	BonoboObject *bonobo_object;
	EvolutionWizard *wizard;
	EvolutionWizardPrivate *priv;

	bonobo_object = bonobo_object_from_servant (servant);
	wizard = EVOLUTION_WIZARD (bonobo_object);
	priv = wizard->priv;

	if (pagenumber < 0 || pagenumber >= priv->page_count) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_GNOME_Evolution_Wizard_NoPage, NULL);
		return;
	}

	switch (action) {
	case GNOME_Evolution_Wizard_NEXT:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[NEXT],
				 pagenumber);
		break;

	case GNOME_Evolution_Wizard_PREPARE:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[PREPARE],
				 pagenumber);
		break;

	case GNOME_Evolution_Wizard_BACK:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[BACK],
				 pagenumber);
		break;

	case GNOME_Evolution_Wizard_FINISH:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[FINISH],
				 pagenumber);
		break;

	case GNOME_Evolution_Wizard_CANCEL:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[CANCEL],
				 pagenumber);
		break;

	case GNOME_Evolution_Wizard_HELP:
		gtk_signal_emit (GTK_OBJECT (bonobo_object), signals[HELP],
				 pagenumber);
		break;

	default:
		break;
	}
}


static void
evolution_wizard_destroy (GtkObject *object)
{
	EvolutionWizard *wizard;

	wizard = EVOLUTION_WIZARD (object);
	if (wizard->priv == NULL) {
		return;
	}

	g_free (wizard->priv);
	wizard->priv = NULL;

	parent_class->destroy (object);
}

static void
evolution_wizard_class_init (EvolutionWizardClass *klass)
{
	GtkObjectClass *object_class;
	POA_GNOME_Evolution_Wizard__epv *epv = &klass->epv;

	object_class = GTK_OBJECT_CLASS (klass);
	object_class->destroy = evolution_wizard_destroy;

	signals[NEXT] = gtk_signal_new ("next", GTK_RUN_FIRST,
					object_class->type,
					GTK_SIGNAL_OFFSET (EvolutionWizardClass, next),
					gtk_marshal_NONE__INT, GTK_TYPE_NONE, 
					1, GTK_TYPE_INT);
	signals[PREPARE] = gtk_signal_new ("prepare", GTK_RUN_FIRST,
					   object_class->type,
					   GTK_SIGNAL_OFFSET (EvolutionWizardClass, prepare),
					   gtk_marshal_NONE__INT, GTK_TYPE_NONE,
					   1, GTK_TYPE_INT);
	signals[BACK] = gtk_signal_new ("back", GTK_RUN_FIRST,
					object_class->type,
					GTK_SIGNAL_OFFSET (EvolutionWizardClass, back),
					gtk_marshal_NONE__INT, GTK_TYPE_NONE,
					1, GTK_TYPE_INT);
	signals[FINISH] = gtk_signal_new ("finish", GTK_RUN_FIRST,
					  object_class->type,
					  GTK_SIGNAL_OFFSET (EvolutionWizardClass, finish),
					  gtk_marshal_NONE__INT, GTK_TYPE_NONE,
					  1, GTK_TYPE_INT);
	signals[CANCEL] = gtk_signal_new ("cancel", GTK_RUN_FIRST,
					  object_class->type,
					  GTK_SIGNAL_OFFSET (EvolutionWizardClass, cancel),
					  gtk_marshal_NONE__INT, GTK_TYPE_NONE,
					  1, GTK_TYPE_INT);
	signals[HELP] = gtk_signal_new ("help", GTK_RUN_FIRST,
					object_class->type,
					GTK_SIGNAL_OFFSET (EvolutionWizardClass, help),
					gtk_marshal_NONE__INT, GTK_TYPE_NONE,
					1, GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, signals, LAST_SIGNAL);

	parent_class = gtk_type_class (PARENT_TYPE);

	epv->_get_pageCount = impl_GNOME_Evolution_Wizard__get_pageCount;
	epv->getControl = impl_GNOME_Evolution_Wizard_getControl;
	epv->notifyAction = impl_GNOME_Evolution_Wizard_notifyAction;
}

static void
evolution_wizard_init (EvolutionWizard *wizard)
{
	wizard->priv = g_new0 (EvolutionWizardPrivate, 1);
}

BONOBO_X_TYPE_FUNC_FULL (EvolutionWizard, GNOME_Evolution_Wizard, 
			 PARENT_TYPE, evolution_wizard);

EvolutionWizard *
evolution_wizard_construct (EvolutionWizard *wizard,
			    BonoboEventSource *event_source,
			    EvolutionWizardGetControlFn get_fn,
			    int num_pages,
			    void *closure)
{
	EvolutionWizardPrivate *priv;

	g_return_val_if_fail (BONOBO_IS_EVENT_SOURCE (event_source), NULL);
	g_return_val_if_fail (IS_EVOLUTION_WIZARD (wizard), NULL);

	priv = wizard->priv;
	priv->get_fn = get_fn;
	priv->page_count = num_pages;
	priv->closure = closure;

	priv->event_source = event_source;
	bonobo_object_add_interface (BONOBO_OBJECT (wizard),
				     BONOBO_OBJECT (event_source));

	return wizard;
}

EvolutionWizard *
evolution_wizard_new_full (EvolutionWizardGetControlFn get_fn,
			   int num_pages,
			   BonoboEventSource *event_source,
			   void *closure)
{
	EvolutionWizard *wizard;

	g_return_val_if_fail (num_pages > 0, NULL);
	g_return_val_if_fail (BONOBO_IS_EVENT_SOURCE (event_source), NULL);

	wizard = gtk_type_new (evolution_wizard_get_type ());

	return evolution_wizard_construct (wizard, event_source, get_fn, num_pages, closure);
}

EvolutionWizard *
evolution_wizard_new (EvolutionWizardGetControlFn get_fn,
		      int num_pages,
		      void *closure)
{
	BonoboEventSource *event_source;

	g_return_val_if_fail (num_pages > 0, NULL);

	event_source = bonobo_event_source_new ();

	return evolution_wizard_new_full (get_fn, num_pages, event_source, closure);
}

void
evolution_wizard_set_buttons_sensitive (EvolutionWizard *wizard,
					gboolean back_sensitive,
					gboolean next_sensitive,
					gboolean cancel_sensitive,
					CORBA_Environment *opt_ev)
{
	EvolutionWizardPrivate *priv;
	CORBA_Environment ev;
	CORBA_any any;
	CORBA_short s;

	g_return_if_fail (IS_EVOLUTION_WIZARD (wizard));

	priv = wizard->priv;

	if (opt_ev == NULL) {
		CORBA_exception_init (&ev);
	} else {
		ev = *opt_ev;
	}

	s = back_sensitive << 2 | next_sensitive << 1 | cancel_sensitive;
	any._type = (CORBA_TypeCode) TC_short;
	any._value = &s;

	bonobo_event_source_notify_listeners (priv->event_source,
					      EVOLUTION_WIZARD_SET_BUTTONS_SENSITIVE,
					      &any, &ev);
	if (opt_ev == NULL && BONOBO_EX (&ev)) {
		g_warning ("ERROR(%s): %s", __FUNCTION__, CORBA_exception_id (&ev));
	}

	if (opt_ev == NULL) {
		CORBA_exception_free (&ev);
	}
}

void
evolution_wizard_set_show_finish (EvolutionWizard *wizard,
				  gboolean show_finish,
				  CORBA_Environment *opt_ev)
{
	EvolutionWizardPrivate *priv;
	CORBA_Environment ev;
	CORBA_any any;
	CORBA_boolean b;

	g_return_if_fail (IS_EVOLUTION_WIZARD (wizard));

	priv = wizard->priv;
	if (opt_ev == NULL) {
		CORBA_exception_init (&ev);
	} else {
		ev = *opt_ev;
	}

	b = show_finish;
	any._type = (CORBA_TypeCode) TC_boolean;
	any._value = &b;

	bonobo_event_source_notify_listeners (priv->event_source,
					      EVOLUTION_WIZARD_SET_SHOW_FINISH,
					      &any, &ev);
	if (opt_ev == NULL && BONOBO_EX (&ev)) {
		g_warning ("ERROR(%s): %s", __FUNCTION__, CORBA_exception_id (&ev));
	}

	if (opt_ev == NULL) {
		CORBA_exception_free (&ev);
	}
}

void
evolution_wizard_set_page (EvolutionWizard *wizard,
			   int page_number,
			   CORBA_Environment *opt_ev)
{
	EvolutionWizardPrivate *priv;
	CORBA_Environment ev;
	CORBA_any any;
	CORBA_short s;

	g_return_if_fail (IS_EVOLUTION_WIZARD (wizard));

	priv = wizard->priv;

	g_return_if_fail (page_number >= 0 && page_number < priv->page_count);

	if (opt_ev == NULL) {
		CORBA_exception_init (&ev);
	} else {
		ev = *opt_ev;
	}

	s = page_number;
	any._type = (CORBA_TypeCode) TC_short;
	any._value = &s;

	bonobo_event_source_notify_listeners (priv->event_source,
					      EVOLUTION_WIZARD_SET_PAGE,
					      &any, &ev);

	if (opt_ev == NULL && BONOBO_EX (&ev)) {
		g_warning ("ERROR(%s): %s", __FUNCTION__, CORBA_exception_id (&ev));
	}

	if (opt_ev == NULL) {
		CORBA_exception_free (&ev);
	}
}

BonoboEventSource *
evolution_wizard_get_event_source (EvolutionWizard *wizard)
{
	g_return_val_if_fail (IS_EVOLUTION_WIZARD (wizard), NULL);

	return wizard->priv->event_source;
}
