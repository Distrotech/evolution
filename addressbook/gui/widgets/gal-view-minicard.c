/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gal-view-minicard.c: An Minicard View
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * (C) 2000, 2001 Ximian, Inc.
 */
#include <config.h>
#include "gal-view-minicard.h"
#include <gnome-xml/parser.h>
#include <gal/util/e-xml-utils.h>

#define PARENT_TYPE gal_view_get_type ()
#define d(x) x

static GalViewClass *gal_view_minicard_parent_class;

static void
gal_view_minicard_edit            (GalView *view)
{
	/* GalViewMinicard *minicard_view = GAL_VIEW_MINICARD(view); */
}

static void  
gal_view_minicard_load (GalView *view,
			const char *filename)
{
	xmlDoc *doc;
	doc = xmlParseFile (filename);
	if (doc) {
		xmlNode *root = xmlDocGetRootElement(doc);
		GAL_VIEW_MINICARD (view)->column_width = e_xml_get_double_prop_by_name_with_default (root, "column_width", 150);
		xmlFreeDoc(doc);
	}
}

static void
gal_view_minicard_save (GalView *view,
			const char *filename)
{
	xmlDoc *doc;
	xmlNode *root;

	doc = xmlNewDoc("1.0");
	root = xmlNewNode (NULL, "EMinicardViewState");
	e_xml_set_double_prop_by_name (root, "column_width", GAL_VIEW_MINICARD (view)->column_width);
	xmlDocSetRootElement(doc, root);
	xmlSaveFile(filename, doc);
	xmlFreeDoc(doc);
}

static const char *
gal_view_minicard_get_title       (GalView *view)
{
	return GAL_VIEW_MINICARD(view)->title;
}

static void
gal_view_minicard_set_title       (GalView *view,
				 const char *title)
{
	g_free(GAL_VIEW_MINICARD(view)->title);
	GAL_VIEW_MINICARD(view)->title = g_strdup(title);
}

static const char *
gal_view_minicard_get_type_code (GalView *view)
{
	return "minicard";
}

static GalView *
gal_view_minicard_clone       (GalView *view)
{
	GalViewMinicard *gvm, *new;

	gvm = GAL_VIEW_MINICARD(view);

	new               = gtk_type_new (gal_view_minicard_get_type ());
	new->title        = g_strdup (gvm->title);
	new->column_width = gvm->column_width;

	return GAL_VIEW(new);
}

static void
gal_view_minicard_destroy         (GtkObject *object)
{
	GalViewMinicard *view = GAL_VIEW_MINICARD(object);
	gal_view_minicard_detach (view);
	g_free(view->title);
}

static void
gal_view_minicard_class_init      (GtkObjectClass *object_class)
{
	GalViewClass *gal_view_class  = GAL_VIEW_CLASS(object_class);
	gal_view_minicard_parent_class  = gtk_type_class (PARENT_TYPE);

	gal_view_class->edit          = gal_view_minicard_edit         ;
	gal_view_class->load          = gal_view_minicard_load         ;
	gal_view_class->save          = gal_view_minicard_save         ;
	gal_view_class->get_title     = gal_view_minicard_get_title    ;
	gal_view_class->set_title     = gal_view_minicard_set_title    ;
	gal_view_class->get_type_code = gal_view_minicard_get_type_code;
	gal_view_class->clone         = gal_view_minicard_clone        ;

	object_class->destroy         = gal_view_minicard_destroy      ;
}

static void
gal_view_minicard_init      (GalViewMinicard *gvm)
{
	gvm->title = NULL;
	gvm->column_width = 150.0;

	gvm->emvw = NULL;
	gvm->emvw_column_width_changed_id = 0;
}

/**
 * gal_view_minicard_new
 * @title: The name of the new view.
 *
 * Returns a new GalViewMinicard.  This is primarily for use by
 * GalViewFactoryMinicard.
 *
 * Returns: The new GalViewMinicard.
 */
GalView *
gal_view_minicard_new (const gchar *title)
{
	return gal_view_minicard_construct (gtk_type_new (gal_view_minicard_get_type ()), title);
}

/**
 * gal_view_minicard_construct
 * @view: The view to construct.
 * @title: The name of the new view.
 *
 * constructs the GalViewMinicard.  To be used by subclasses and
 * language bindings.
 *
 * Returns: The GalViewMinicard.
 */
GalView *
gal_view_minicard_construct  (GalViewMinicard *view,
			      const gchar *title)
{
	view->title = g_strdup(title);
	return GAL_VIEW(view);
}

GtkType
gal_view_minicard_get_type        (void)
{
	static guint type = 0;
	
	if (!type)
	{
		GtkTypeInfo info =
		{
			"GalViewMinicard",
			sizeof (GalViewMinicard),
			sizeof (GalViewMinicardClass),
			(GtkClassInitFunc) gal_view_minicard_class_init,
			(GtkObjectInitFunc) gal_view_minicard_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		type = gtk_type_unique (PARENT_TYPE, &info);
	}

	return type;
}

static void
column_width_changed (ETable *table, double width, GalViewMinicard *view)
{
	d(g_print("%s: Old width = %f, New width = %f\n", __FUNCTION__, view->column_width, width));
	if (view->column_width != width) {
		view->column_width = width;
		gal_view_changed(GAL_VIEW(view));
	}
}

void
gal_view_minicard_attach (GalViewMinicard *view, EMinicardViewWidget *emvw)
{
	gal_view_minicard_detach (view);

	view->emvw = emvw;

	gtk_object_ref (GTK_OBJECT (view->emvw));

	gtk_object_set (GTK_OBJECT (view->emvw),
			"column_width", (int) view->column_width,
			NULL);

	view->emvw_column_width_changed_id =
		gtk_signal_connect(GTK_OBJECT(view->emvw), "column_width_changed",
				   GTK_SIGNAL_FUNC (column_width_changed), view);
}

void
gal_view_minicard_detach (GalViewMinicard *view)
{
	if (view->emvw == NULL)
		return;
	if (view->emvw_column_width_changed_id) {
		gtk_signal_disconnect (GTK_OBJECT (view->emvw),
				       view->emvw_column_width_changed_id);
		view->emvw_column_width_changed_id = 0;
	}
	gtk_object_unref (GTK_OBJECT (view->emvw));
	view->emvw = NULL;
}
