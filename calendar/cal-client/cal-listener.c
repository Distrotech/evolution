/* Evolution calendar listener
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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

#include <config.h>
#include "cal-listener.h"



/* Private part of the CalListener structure */
struct CalListenerPrivate {
	/* Notification functions and their closure data */
	CalListenerCalOpenedFn cal_opened_fn;
	CalListenerCalSetModeFn cal_set_mode_fn;
	CalListenerObjUpdatedFn obj_updated_fn;
	CalListenerObjRemovedFn obj_removed_fn;
	CalListenerCategoriesChangedFn categories_changed_fn;
	gpointer fn_data;

	/* Whether notification is desired */
	gboolean notify : 1;
};



static void cal_listener_class_init (CalListenerClass *class);
static void cal_listener_init (CalListener *listener);
static void cal_listener_destroy (GtkObject *object);

static void impl_notifyCalOpened (PortableServer_Servant servant,
				  GNOME_Evolution_Calendar_Listener_OpenStatus status,
				  GNOME_Evolution_Calendar_Cal cal,
				  CORBA_Environment *ev);
static void impl_notifyCalSetMode (PortableServer_Servant servant,
				   GNOME_Evolution_Calendar_Listener_SetModeStatus status,
				   GNOME_Evolution_Calendar_CalMode mode,
				   CORBA_Environment *ev);
static void impl_notifyObjUpdated (PortableServer_Servant servant,
				   GNOME_Evolution_Calendar_CalObjUID uid,
				   CORBA_Environment *ev);
static void impl_notifyObjRemoved (PortableServer_Servant servant,
				   GNOME_Evolution_Calendar_CalObjUID uid,
				   CORBA_Environment *ev);
static void impl_notifyCategoriesChanged (PortableServer_Servant servant,
					  const GNOME_Evolution_Calendar_StringSeq *categories,
					  CORBA_Environment *ev);

static BonoboXObjectClass *parent_class;



BONOBO_X_TYPE_FUNC_FULL (CalListener,
			 GNOME_Evolution_Calendar_Listener,
			 BONOBO_X_OBJECT_TYPE,
			 cal_listener);

/* Class initialization function for the calendar listener */
static void
cal_listener_class_init (CalListenerClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (BONOBO_X_OBJECT_TYPE);

	class->epv.notifyCalOpened = impl_notifyCalOpened;
	class->epv.notifyCalSetMode = impl_notifyCalSetMode;
	class->epv.notifyObjUpdated = impl_notifyObjUpdated;
	class->epv.notifyObjRemoved = impl_notifyObjRemoved;
	class->epv.notifyCategoriesChanged = impl_notifyCategoriesChanged;

	object_class->destroy = cal_listener_destroy;
}

/* Object initialization function for the calendar listener */
static void
cal_listener_init (CalListener *listener)
{
	CalListenerPrivate *priv;

	priv = g_new0 (CalListenerPrivate, 1);
	listener->priv = priv;

	priv->cal_opened_fn = NULL;
	priv->obj_updated_fn = NULL;
	priv->obj_removed_fn = NULL;
	priv->categories_changed_fn = NULL;

	priv->notify = TRUE;
}

/* Destroy handler for the calendar listener */
static void
cal_listener_destroy (GtkObject *object)
{
	CalListener *listener;
	CalListenerPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_CAL_LISTENER (object));

	listener = CAL_LISTENER (object);
	priv = listener->priv;

	priv->cal_opened_fn = NULL;
	priv->obj_updated_fn = NULL;
	priv->obj_removed_fn = NULL;
	priv->categories_changed_fn = NULL;
	priv->fn_data = NULL;

	priv->notify = FALSE;

	g_free (priv);
	listener->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* CORBA servant implementation */

/* ::notifyCalOpened method */
static void
impl_notifyCalOpened (PortableServer_Servant servant,
		      GNOME_Evolution_Calendar_Listener_OpenStatus status,
		      GNOME_Evolution_Calendar_Cal cal,
		      CORBA_Environment *ev)
{
	CalListener *listener;
	CalListenerPrivate *priv;
	CORBA_Environment aev;
	GNOME_Evolution_Calendar_Cal cal_copy;

	listener = CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	CORBA_exception_init (&aev);
	cal_copy = CORBA_Object_duplicate (cal, &aev);

	if (aev._major != CORBA_NO_EXCEPTION) {
		g_message ("Listener_notifyCalOpened(): could not duplicate the calendar");
		CORBA_exception_free (&aev);
		return;
	}
	CORBA_exception_free (&aev);

	g_assert (priv->cal_opened_fn != NULL);
	(* priv->cal_opened_fn) (listener, status, cal, priv->fn_data);
}

/* ::notifyCalSetMode method */
static void
impl_notifyCalSetMode (PortableServer_Servant servant,
		       GNOME_Evolution_Calendar_Listener_SetModeStatus status,
		       GNOME_Evolution_Calendar_CalMode mode,
		       CORBA_Environment *ev)
{
	CalListener *listener;
	CalListenerPrivate *priv;

	listener = CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_assert (priv->cal_set_mode_fn != NULL);
	(* priv->cal_set_mode_fn) (listener, status, mode, priv->fn_data);
}

/* ::notifyObjUpdated method */
static void
impl_notifyObjUpdated (PortableServer_Servant servant,
		       GNOME_Evolution_Calendar_CalObjUID uid,
		       CORBA_Environment *ev)
{
	CalListener *listener;
	CalListenerPrivate *priv;

	listener = CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_assert (priv->obj_updated_fn != NULL);
	(* priv->obj_updated_fn) (listener, uid, priv->fn_data);
}

/* ::notifyObjRemoved method */
static void
impl_notifyObjRemoved (PortableServer_Servant servant,
		       GNOME_Evolution_Calendar_CalObjUID uid,
		       CORBA_Environment *ev)
{
	CalListener *listener;
	CalListenerPrivate *priv;

	listener = CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_assert (priv->obj_removed_fn != NULL);
	(* priv->obj_removed_fn) (listener, uid, priv->fn_data);
}

/* ::notifyCategoriesChanged method */
static void
impl_notifyCategoriesChanged (PortableServer_Servant servant,
			      const GNOME_Evolution_Calendar_StringSeq *categories,
			      CORBA_Environment *ev)
{
	CalListener *listener;
	CalListenerPrivate *priv;

	listener = CAL_LISTENER (bonobo_object_from_servant (servant));
	priv = listener->priv;

	if (!priv->notify)
		return;

	g_assert (priv->categories_changed_fn != NULL);
	(* priv->categories_changed_fn) (listener, categories, priv->fn_data);
}



/**
 * cal_listener_construct:
 * @listener: A calendar listener.
 * @cal_opened_fn: Function that will be called to notify that a calendar was
 * opened.
 * @obj_updated_fn: Function that will be called to notify that an object in the
 * calendar was updated.
 * @obj_removed_fn: Function that will be called to notify that an object in the
 * calendar was removed.
 * @categories_changed_fn: Function that will be called to notify that the list
 * of categories that are present in the calendar's objects has changed.
 * @fn_data: Closure data pointer that will be passed to the notification
 * functions.
 *
 * Constructs a calendar listener by setting the callbacks that it will use for
 * notification from the calendar server.
 *
 * Return value: the same object as the @listener argument.
 **/
CalListener *
cal_listener_construct (CalListener *listener,
			CalListenerCalOpenedFn cal_opened_fn,
			CalListenerCalSetModeFn cal_set_mode_fn,
			CalListenerObjUpdatedFn obj_updated_fn,
			CalListenerObjRemovedFn obj_removed_fn,
			CalListenerCategoriesChangedFn categories_changed_fn,
			gpointer fn_data)
{
	CalListenerPrivate *priv;

	g_return_val_if_fail (listener != NULL, NULL);
	g_return_val_if_fail (IS_CAL_LISTENER (listener), NULL);
	g_return_val_if_fail (cal_opened_fn != NULL, NULL);
 	g_return_val_if_fail (cal_set_mode_fn != NULL, NULL);
	g_return_val_if_fail (obj_updated_fn != NULL, NULL);
	g_return_val_if_fail (obj_removed_fn != NULL, NULL);
	g_return_val_if_fail (categories_changed_fn != NULL, NULL);

	priv = listener->priv;

	priv->cal_opened_fn = cal_opened_fn;
	priv->cal_set_mode_fn = cal_set_mode_fn;
	priv->obj_updated_fn = obj_updated_fn;
	priv->obj_removed_fn = obj_removed_fn;
	priv->categories_changed_fn = categories_changed_fn;
	priv->fn_data = fn_data;

	return listener;
}

/**
 * cal_listener_new:
 * @cal_opened_fn: Function that will be called to notify that a calendar was
 * opened.
 * @obj_updated_fn: Function that will be called to notify that an object in the
 * calendar was updated.
 * @obj_removed_fn: Function that will be called to notify that an object in the
 * calendar was removed.
 * @categories_changed_fn: Function that will be called to notify that the list
 * of categories that are present in the calendar's objects has changed.
 * @fn_data: Closure data pointer that will be passed to the notification
 * functions.
 *
 * Creates a new #CalListener object.
 *
 * Return value: A newly-created #CalListener object.
 **/
CalListener *
cal_listener_new (CalListenerCalOpenedFn cal_opened_fn,
		  CalListenerCalSetModeFn cal_set_mode_fn,
		  CalListenerObjUpdatedFn obj_updated_fn,
		  CalListenerObjRemovedFn obj_removed_fn,
		  CalListenerCategoriesChangedFn categories_changed_fn,
		  gpointer fn_data)
{
	CalListener *listener;

	g_return_val_if_fail (cal_opened_fn != NULL, NULL);
	g_return_val_if_fail (obj_updated_fn != NULL, NULL);
	g_return_val_if_fail (obj_removed_fn != NULL, NULL);
	g_return_val_if_fail (categories_changed_fn != NULL, NULL);

	listener = gtk_type_new (CAL_LISTENER_TYPE);
	return cal_listener_construct (listener,
				       cal_opened_fn,
				       cal_set_mode_fn,
				       obj_updated_fn,
				       obj_removed_fn,
				       categories_changed_fn,
				       fn_data);
}

/**
 * cal_listener_stop_notification:
 * @listener: A calendar listener.
 * 
 * Informs a calendar listener that no further notification is desired.  The
 * callbacks specified when the listener was created will no longer be invoked
 * after this function is called.
 **/
void
cal_listener_stop_notification (CalListener *listener)
{
	CalListenerPrivate *priv;

	g_return_if_fail (listener != NULL);
	g_return_if_fail (IS_CAL_LISTENER (listener));

	priv = listener->priv;
	g_return_if_fail (priv->notify != FALSE);

	priv->notify = FALSE;
}
