/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-tasks.c
 *
 * Copyright (C) 2001  Ximian, Inc.
 * Copyright (C) 2001  Ximian, Inc.
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
 * Authors: Federico Mena Quintero <federico@ximian.com>
 *	    Damon Chaplin <damon@ximian.com>
 */

#include <config.h>
#include <gnome.h>
#include <gal/util/e-util.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/menus/gal-view-collection.h>
#include <gal/menus/gal-view-factory-etable.h>
#include <gal/menus/gal-view-etable.h>
#include "widgets/menus/gal-view-menus.h"
#include "dialogs/task-editor.h"
#include "cal-search-bar.h"
#include "calendar-config.h"
#include "component-factory.h"

#include "e-tasks.h"

/* A list of all of the ETasks widgets in use. We use this to update the
   user preference settings. This will change when we switch to GConf. */
static GList *all_tasks = NULL;


/* Private part of the GnomeCalendar structure */
struct _ETasksPrivate {
	/* The calendar client object we monitor */
	CalClient   *client;

	/* The ECalendarTable showing the tasks. */
	GtkWidget   *tasks_view;

	/* Calendar search bar for tasks */
	GtkWidget *search_bar;

	/* View collection and the view menus handler */
	GalViewCollection *view_collection;
	GalViewMenus *view_menus;
};


static void e_tasks_class_init (ETasksClass *class);
static void e_tasks_init (ETasks *tasks);
static void setup_widgets (ETasks *tasks);
static void e_tasks_destroy (GtkObject *object);

static void cal_opened_cb (CalClient *client, CalClientOpenStatus status, gpointer data);

static char* e_tasks_get_config_filename (ETasks *tasks);

/* Signal IDs */
enum {
	SELECTION_CHANGED,
	LAST_SIGNAL
};

static GtkTableClass *parent_class;
static guint e_tasks_signals[LAST_SIGNAL] = { 0 };


E_MAKE_TYPE (e_tasks, "ETasks", ETasks,
	     e_tasks_class_init, e_tasks_init,
	     GTK_TYPE_TABLE)


/* Class initialization function for the gnome calendar */
static void
e_tasks_class_init (ETasksClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (GTK_TYPE_TABLE);

	e_tasks_signals[SELECTION_CHANGED] =
		gtk_signal_new ("selection_changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETasksClass, selection_changed),
				gtk_marshal_NONE__INT,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, e_tasks_signals, LAST_SIGNAL);

	object_class->destroy = e_tasks_destroy;

	class->selection_changed = NULL;
}


/* Object initialization function for the gnome calendar */
static void
e_tasks_init (ETasks *tasks)
{
	ETasksPrivate *priv;

	priv = g_new0 (ETasksPrivate, 1);
	tasks->priv = priv;

	priv->view_collection = NULL;
	priv->view_menus = NULL;

	setup_widgets (tasks);
}

/* Callback used when the selection changes in the table. */
static void
table_selection_change_cb (ETable *etable, gpointer data)
{
	ETasks *tasks;
	int n_selected;

	tasks = E_TASKS (data);

	n_selected = e_table_selected_count (etable);
	gtk_signal_emit (GTK_OBJECT (tasks), e_tasks_signals[SELECTION_CHANGED],
			 n_selected);
}

/* Callback used when the sexp in the search bar changes */
static void
search_bar_sexp_changed_cb (CalSearchBar *cal_search, const char *sexp, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	CalendarModel *model;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	calendar_model_set_query (model, sexp);
}

/* Callback used when the selected category in the search bar changes */
static void
search_bar_category_changed_cb (CalSearchBar *cal_search, const char *category, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	CalendarModel *model;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	calendar_model_set_default_category (model, category);
}

#define E_TASKS_TABLE_DEFAULT_STATE					\
	"<?xml version=\"1.0\"?>"					\
	"<ETableState>"							\
	"<column source=\"13\"/>"					\
	"<column source=\"14\"/>"					\
	"<column source=\"9\"/>"					\
	"<column source=\"5\"/>"					\
	"<grouping/>"							\
	"</ETableState>"


static void
setup_widgets (ETasks *tasks)
{
	ETasksPrivate *priv;
	ETable *etable;
	CalendarModel *model;

	priv = tasks->priv;

	priv->search_bar = cal_search_bar_new ();
	gtk_signal_connect (GTK_OBJECT (priv->search_bar), "sexp_changed",
			    GTK_SIGNAL_FUNC (search_bar_sexp_changed_cb), tasks);
	gtk_signal_connect (GTK_OBJECT (priv->search_bar), "category_changed",
			    GTK_SIGNAL_FUNC (search_bar_category_changed_cb), tasks);

	gtk_table_attach (GTK_TABLE (tasks), priv->search_bar, 0, 1, 0, 1,
			  GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0, 0);
	gtk_widget_show (priv->search_bar);

	priv->tasks_view = e_calendar_table_new ();
	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	calendar_model_set_new_comp_vtype (model, CAL_COMPONENT_TODO);

	etable = e_table_scrolled_get_table (
		E_TABLE_SCROLLED (E_CALENDAR_TABLE (priv->tasks_view)->etable));
	e_table_set_state (etable, E_TASKS_TABLE_DEFAULT_STATE);
	gtk_table_attach (GTK_TABLE (tasks), priv->tasks_view, 0, 1, 1, 2,
			  GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show (priv->tasks_view);

	calendar_config_configure_e_calendar_table (E_CALENDAR_TABLE (priv->tasks_view));

	gtk_signal_connect (GTK_OBJECT (etable), "selection_change",
			    GTK_SIGNAL_FUNC (table_selection_change_cb), tasks);
}

/* Callback used when the set of categories changes in the calendar client */
static void
client_categories_changed_cb (CalClient *client, GPtrArray *categories, gpointer data)
{
	ETasks *tasks;
	ETasksPrivate *priv;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	cal_search_bar_set_categories (CAL_SEARCH_BAR (priv->search_bar), categories);
}

GtkWidget *
e_tasks_construct (ETasks *tasks)
{
	ETasksPrivate *priv;
	CalendarModel *model;

	g_return_val_if_fail (tasks != NULL, NULL);
	g_return_val_if_fail (E_IS_TASKS (tasks), NULL);

	priv = tasks->priv;

	priv->client = cal_client_new ();
	if (!priv->client)
		return NULL;

	gtk_signal_connect (GTK_OBJECT (priv->client), "cal_opened",
			    GTK_SIGNAL_FUNC (cal_opened_cb), tasks);
	gtk_signal_connect (GTK_OBJECT (priv->client), "categories_changed",
			    GTK_SIGNAL_FUNC (client_categories_changed_cb), tasks);

	model = e_calendar_table_get_model (E_CALENDAR_TABLE (priv->tasks_view));
	g_assert (model != NULL);

	calendar_model_set_cal_client (model, priv->client, CALOBJ_TYPE_TODO);

	return GTK_WIDGET (tasks);
}


GtkWidget *
e_tasks_new (void)
{
	ETasks *tasks;

	tasks = gtk_type_new (e_tasks_get_type ());

	if (!e_tasks_construct (tasks)) {
		g_message ("e_tasks_new(): Could not construct the tasks GUI");
		gtk_object_unref (GTK_OBJECT (tasks));
		return NULL;
	}

	all_tasks = g_list_prepend (all_tasks, tasks);

	return GTK_WIDGET (tasks);
}


static void
e_tasks_destroy (GtkObject *object)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	char *config_filename;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_TASKS (object));

	tasks = E_TASKS (object);
	priv = tasks->priv;

	/* Save the ETable layout. */
	config_filename = e_tasks_get_config_filename (tasks);
	e_calendar_table_save_state (E_CALENDAR_TABLE (priv->tasks_view),
				     config_filename);
	g_free (config_filename);

	if (priv->client) {
		gtk_object_unref (GTK_OBJECT (priv->client));
		priv->client = NULL;
	}

	g_free (priv);
	tasks->priv = NULL;

	all_tasks = g_list_remove (all_tasks, tasks);

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}


gboolean
e_tasks_open			(ETasks		*tasks,
				 char		*file)
{
	ETasksPrivate *priv;
	char *config_filename;

	g_return_val_if_fail (tasks != NULL, FALSE);
	g_return_val_if_fail (E_IS_TASKS (tasks), FALSE);
	g_return_val_if_fail (file != NULL, FALSE);

	priv = tasks->priv;

	if (!cal_client_open_calendar (priv->client, file, FALSE)) {
		g_message ("e_tasks_open(): Could not issue the request");
		return FALSE;
	}

	config_filename = e_tasks_get_config_filename (tasks);
	e_calendar_table_load_state (E_CALENDAR_TABLE (priv->tasks_view),
				     config_filename);
	g_free (config_filename);

	return TRUE;
}


/* Displays an error to indicate that loading a calendar failed */
static void
load_error				(ETasks		*tasks,
					 const char	*uri)
{
	char *msg;

	msg = g_strdup_printf (_("Could not load the tasks in `%s'"), uri);
	gnome_error_dialog_parented (msg, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tasks))));
	g_free (msg);
}

/* Displays an error to indicate that the specified URI method is not supported */
static void
method_error				(ETasks		*tasks,
					 const char	*uri)
{
	char *msg;

	msg = g_strdup_printf (_("The method required to load `%s' is not supported"), uri);
	gnome_error_dialog_parented (msg, GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (tasks))));
	g_free (msg);
}

/* Callback from the calendar client when a calendar is opened */
static void
cal_opened_cb				(CalClient	*client,
					 CalClientOpenStatus status,
					 gpointer	 data)
{
	ETasks *tasks;
	ETasksPrivate *priv;

	tasks = E_TASKS (data);
	priv = tasks->priv;

	switch (status) {
	case CAL_CLIENT_OPEN_SUCCESS:
		/* Everything is OK */
		return;

	case CAL_CLIENT_OPEN_ERROR:
		load_error (tasks, cal_client_get_uri (client));
		break;

	case CAL_CLIENT_OPEN_NOT_FOUND:
		/* bullshit; we did not specify only_if_exists */
		g_assert_not_reached ();
		return;

	case CAL_CLIENT_OPEN_METHOD_NOT_SUPPORTED:
		method_error (tasks, cal_client_get_uri (client));
		break;

	default:
		g_assert_not_reached ();
	}
}


static char*
e_tasks_get_config_filename		(ETasks		*tasks)
{
	ETasksPrivate *priv;
	char *url, *filename;

	priv = tasks->priv;

	url = g_strdup (cal_client_get_uri (priv->client));

	/* This turns all funny characters into '_', in the string itself. */
	e_filename_make_safe (url);
	
	filename = g_strdup_printf ("%s/config/et-header-%s", evolution_dir,
				    url);
	g_free (url);
	
	return filename;
}


/**
 * e_tasks_get_cal_client:
 * @tasks: An #ETasks.
 *
 * Queries the calendar client interface object that a tasks view is using.
 *
 * Return value: A calendar client interface object.
 **/
CalClient *
e_tasks_get_cal_client			(ETasks		*tasks)
{
	ETasksPrivate *priv;

	g_return_val_if_fail (E_IS_TASKS (tasks), NULL);

	priv = tasks->priv;

	return priv->client;
}


void
e_tasks_new_task			(ETasks		*tasks)
{
	ETasksPrivate *priv;
	TaskEditor *tedit;
	CalComponent *comp;
	const char *category;

	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	tedit = task_editor_new ();
	comp_editor_set_cal_client (COMP_EDITOR (tedit), priv->client);

	comp = cal_component_new ();
	cal_component_set_new_vtype (comp, CAL_COMPONENT_TODO);

	category = cal_search_bar_get_category (CAL_SEARCH_BAR (priv->search_bar));
	cal_component_set_categories (comp, category);

	comp_editor_edit_comp (COMP_EDITOR (tedit), comp);
	gtk_object_unref (GTK_OBJECT (comp));

	comp_editor_focus (COMP_EDITOR (tedit));
}

/**
 * e_tasks_delete_selected:
 * @tasks: A tasks control widget.
 * 
 * Deletes the selected tasks in the task list.
 **/
void
e_tasks_delete_selected (ETasks *tasks)
{
	ETasksPrivate *priv;
	ECalendarTable *cal_table;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	cal_table = E_CALENDAR_TABLE (priv->tasks_view);
	e_calendar_table_delete_selected (cal_table);
}

/* Callback used from the view collection when we need to display a new view */
static void
display_view_cb (GalViewCollection *collection, GalView *view, gpointer data)
{
	ETasks *tasks;

	tasks = E_TASKS (data);

	if (GAL_IS_VIEW_ETABLE (view)) {
		e_table_set_state_object (e_table_scrolled_get_table (E_TABLE_SCROLLED (E_CALENDAR_TABLE (tasks->priv->tasks_view)->etable)),
					  GAL_VIEW_ETABLE (view)->state);
	}
}

/**
 * e_tasks_setup_view_menus:
 * @tasks: A tasks widget.
 * @uic: UI controller to use for the menus.
 * 
 * Sets up the #GalView menus for a tasks control.  This function should be
 * called from the Bonobo control activation callback for this tasks control.
 * Also, the menus should be discarded using e_tasks_discard_view_menus().
 **/
void
e_tasks_setup_view_menus (ETasks *tasks, BonoboUIComponent *uic)
{
	ETasksPrivate *priv;
	GalViewFactory *factory;
	ETableSpecification *spec;
	char *dir;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));
	g_return_if_fail (uic != NULL);
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (uic));

	priv = tasks->priv;

	g_return_if_fail (priv->view_collection == NULL);

	g_assert (priv->view_collection == NULL);
	g_assert (priv->view_menus == NULL);

	/* Create the view collection */

	priv->view_collection = gal_view_collection_new ();

	dir = gnome_util_prepend_user_home ("/evolution/views/tasks/");
	gal_view_collection_set_storage_directories (priv->view_collection,
						     EVOLUTION_DATADIR "/evolution/views/tasks/",
						     dir);
	g_free (dir);

	/* Create the views */

	spec = e_table_specification_new ();
	e_table_specification_load_from_file (spec, 
					      EVOLUTION_ETSPECDIR "/e-calendar-table.etspec");

	factory = gal_view_factory_etable_new (spec);
	gtk_object_unref (GTK_OBJECT (spec));
	gal_view_collection_add_factory (priv->view_collection, factory);
	gtk_object_unref (GTK_OBJECT (factory));

	/* Load the collection and create the menus */

	gal_view_collection_load (priv->view_collection);

	priv->view_menus = gal_view_menus_new (priv->view_collection);
	gal_view_menus_apply (priv->view_menus, uic, NULL);
	gtk_signal_connect (GTK_OBJECT (priv->view_collection), "display_view",
			    GTK_SIGNAL_FUNC (display_view_cb), tasks);
}

/**
 * e_tasks_discard_view_menus:
 * @tasks: A tasks widget.
 * 
 * Discards the #GalView menus used by a tasks control.  This function should be
 * called from the Bonobo control deactivation callback for this tasks control.
 * The menus should have been set up with e_tasks_setup_view_menus().
 **/
void
e_tasks_discard_view_menus (ETasks *tasks)
{
	ETasksPrivate *priv;

	g_return_if_fail (tasks != NULL);
	g_return_if_fail (E_IS_TASKS (tasks));

	priv = tasks->priv;

	g_return_if_fail (priv->view_collection != NULL);

	g_assert (priv->view_collection != NULL);
	g_assert (priv->view_menus != NULL);

	gtk_object_unref (GTK_OBJECT (priv->view_collection));
	priv->view_collection = NULL;

	gtk_object_unref (GTK_OBJECT (priv->view_menus));
	priv->view_menus = NULL;
}

/**
 * e_tasks_get_calendar_table:
 * @tasks: A tasks widget.
 * 
 * Queries the #ECalendarTable contained in a tasks widget.
 * 
 * Return value: The #ECalendarTable that the tasks widget uses to display its
 * information.
 **/
ECalendarTable *
e_tasks_get_calendar_table (ETasks *tasks)
{
	ETasksPrivate *priv;

	g_return_val_if_fail (tasks != NULL, NULL);
	g_return_val_if_fail (E_IS_TASKS (tasks), NULL);

	priv = tasks->priv;
	return E_CALENDAR_TABLE (priv->tasks_view);
}

/* This updates all the preference settings for all the ETasks widgets in use.
 */
void
e_tasks_update_all_config_settings	(void)
{
	ETasks *tasks;
	ETasksPrivate *priv;
	GList *elem;

	for (elem = all_tasks; elem; elem = elem->next) {
		tasks = E_TASKS (elem->data);
		priv = tasks->priv;
		calendar_config_configure_e_calendar_table (E_CALENDAR_TABLE (priv->tasks_view));
	}
}
