/*
 * e-shell-view.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

/**
 * SECTION: e-shell-view
 * @short_description: views within the main window
 * @include: shell/e-shell-view.h
 **/

#ifndef E_SHELL_VIEW_H
#define E_SHELL_VIEW_H

#include <shell/e-shell-common.h>
#include <shell/e-shell-backend.h>
#include <shell/e-shell-content.h>
#include <shell/e-shell-sidebar.h>
#include <shell/e-shell-taskbar.h>
#include <shell/e-shell-window.h>

#include <widgets/menus/gal-view-collection.h>
#include <widgets/menus/gal-view-instance.h>

/* Standard GObject macros */
#define E_TYPE_SHELL_VIEW \
	(e_shell_view_get_type ())
#define E_SHELL_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SHELL_VIEW, EShellView))
#define E_SHELL_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SHELL_VIEW, EShellViewClass))
#define E_IS_SHELL_VIEW(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SHELL_VIEW))
#define E_IS_SHELL_VIEW_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SHELL_VIEW))
#define E_SHELL_VIEW_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SHELL_VIEW, EShellViewClass))

G_BEGIN_DECLS

typedef struct _EShellView EShellView;
typedef struct _EShellViewClass EShellViewClass;
typedef struct _EShellViewPrivate EShellViewPrivate;

/**
 * EShellView:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 **/
struct _EShellView {
	GObject parent;
	EShellViewPrivate *priv;
};

/**
 * EShellViewClass:
 * @parent_class:	The parent class structure.
 * @label:		The initial value for the switcher action's
 *			#GtkAction:label property.  See
 *			e_shell_view_get_action().
 * @icon_name:		The initial value for the switcher action's
 *			#GtkAction:icon-name property.  See
 *			e_shell_view_get_action().
 * @ui_definition:	Base name of the UI definintion file to add
 *			when the shell view is activated.
 * @ui_manager_id:	The #GtkUIManager ID for #EPluginUI.  Plugins
 *			should use to this ID in their "eplug" files to
 *			add menu and toolbar items to the shell view.
 * @search_options:	Widget path in the UI definition to the search
 *			options popup menu.  The menu gets shown when the
 *			user clicks the "find" icon in the search entry.
 * @search_rules:	Base name of the XML file containing predefined
 *			search rules for this shell view.  The XML files
 *			are usually named something like <filename>
 *			<emphasis>view</emphasis>types.xml</filename>.
 * @view_collection:	A unique #GalViewCollection instance is created
 *			for each subclass and shared across all instances
 *			of that subclass.  That much is done automatically
 *			for subclasses, but subclasses are still responsible
 *			for adding the appropriate #GalView factories to the
 *			view collection.
 * @new_shell_content:	Factory method for the shell view's #EShellContent.
 *			See e_shell_view_get_shell_content().
 * @new_shell_sidebar:	Factory method for the shell view's #EShellSidebar.
 *			See e_shell_view_get_shell_sidebar().
 * @new_shell_taskbar:	Factory method for the shell view's #EShellTaskbar.
 *			See e_shell_view_get_shell_taskbar().
 * @toggled:		Class method for the #EShellView::toggled signal.
 *			Subclasses should rarely need to override the
 *			default behavior.
 * @update_actions:	Class method for the #EShellView::update_actions
 *			signal.  There is no default behavior; subclasses
 *			should override this.
 *
 * #EShellViewClass contains a number of important settings for subclasses.
 **/
struct _EShellViewClass {
	GObjectClass parent_class;

	/* Initial switcher action values. */
	const gchar *label;
	const gchar *icon_name;

	/* Base name of the UI definition file. */
	const gchar *ui_definition;

	/* GtkUIManager identifier for use with EPluginUI.
	 * Usually "org.gnome.evolution.$(VIEW_NAME)". */
	const gchar *ui_manager_id;

	/* Widget path to the search options popup menu. */
	const gchar *search_options;

	/* Base name of the search rule definition file. */
	const gchar *search_rules;

	/* A unique instance is created for each subclass. */
	GalViewCollection *view_collection;

	/* This is set by the corresponding EShellBackend. */
	EShellBackend *shell_backend;

	/* Factory Methods */
	GtkWidget *	(*new_shell_content)	(EShellView *shell_view);
	GtkWidget *	(*new_shell_sidebar)	(EShellView *shell_view);
	GtkWidget *	(*new_shell_taskbar)	(EShellView *shell_view);

	/* Signals */
	void		(*toggled)		(EShellView *shell_view);
	void		(*update_actions)	(EShellView *shell_view);
};

GType		e_shell_view_get_type		(void);
const gchar *	e_shell_view_get_name		(EShellView *shell_view);
GtkAction *	e_shell_view_get_action		(EShellView *shell_view);
const gchar *	e_shell_view_get_title		(EShellView *shell_view);
void		e_shell_view_set_title		(EShellView *shell_view,
						 const gchar *title);
const gchar *	e_shell_view_get_view_id	(EShellView *shell_view);
void		e_shell_view_set_view_id	(EShellView *shell_view,
						 const gchar *view_id);
gboolean	e_shell_view_is_active		(EShellView *shell_view);
gint		e_shell_view_get_page_num	(EShellView *shell_view);
void		e_shell_view_set_page_num	(EShellView *shell_view,
						 gint page_num);
GtkSizeGroup *	e_shell_view_get_size_group	(EShellView *shell_view);
EShellBackend *	e_shell_view_get_shell_backend	(EShellView *shell_view);
EShellContent *	e_shell_view_get_shell_content	(EShellView *shell_view);
EShellSidebar *	e_shell_view_get_shell_sidebar	(EShellView *shell_view);
EShellTaskbar *	e_shell_view_get_shell_taskbar	(EShellView *shell_view);
EShellWindow *	e_shell_view_get_shell_window	(EShellView *shell_view);
GKeyFile *	e_shell_view_get_state_key_file	(EShellView *shell_view);
void		e_shell_view_set_state_dirty	(EShellView *shell_view);
void		e_shell_view_update_actions	(EShellView *shell_view);
void		e_shell_view_show_popup_menu	(EShellView *shell_view,
						 const gchar *widget_path,
						 GdkEventButton *event);
GalViewInstance *
		e_shell_view_new_view_instance	(EShellView *shell_view,
						 const gchar *instance_id);

G_END_DECLS

#endif /* E_SHELL_VIEW_H */
