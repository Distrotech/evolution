/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _E_TABLE_CLICK_TO_ADD_H_
#define _E_TABLE_CLICK_TO_ADD_H_

#include <libxml/tree.h>
#include <libgnomecanvas/gnome-canvas.h>
#include <gal/e-table/e-table-header.h>
#include <gal/e-table/e-table-sort-info.h>
#include <gal/e-table/e-table-item.h>
#include <gal/e-table/e-table-selection-model.h>

G_BEGIN_DECLS

#define E_TABLE_CLICK_TO_ADD_TYPE        (e_table_click_to_add_get_type ())
#define E_TABLE_CLICK_TO_ADD(o)          (GTK_CHECK_CAST ((o), E_TABLE_CLICK_TO_ADD_TYPE, ETableClickToAdd))
#define E_TABLE_CLICK_TO_ADD_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), E_TABLE_CLICK_TO_ADD_TYPE, ETableClickToAddClass))
#define E_IS_TABLE_CLICK_TO_ADD(o)       (GTK_CHECK_TYPE ((o), E_TABLE_CLICK_TO_ADD_TYPE))
#define E_IS_TABLE_CLICK_TO_ADD_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), E_TABLE_CLICK_TO_ADD_TYPE))

typedef struct {
	GnomeCanvasGroup  parent;

	ETableModel      *one;    /* The ETableOne. */

	ETableModel      *model;  /* The backend model. */
	ETableHeader     *eth;    /* This is just to give to the ETableItem. */

	char             *message;

	GnomeCanvasItem  *row;    /* If row is NULL, we're sitting with no data and a "Click here" message. */
	GnomeCanvasItem  *text;   /* If text is NULL, row shouldn't be. */
	GnomeCanvasItem  *rect;   /* What the heck.  Why not. */
	
	gdouble           width;
	gdouble           height;

	ETableSelectionModel *selection;
} ETableClickToAdd;

typedef struct {
	GnomeCanvasGroupClass parent_class;

	/*
	 * signals
	 */
	void (*cursor_change) (ETableClickToAdd *etcta, gint row, gint col);
} ETableClickToAddClass;

GtkType    e_table_click_to_add_get_type (void);

void       e_table_click_to_add_commit (ETableClickToAdd *etcta);

G_END_DECLS

#endif /* _E_TABLE_CLICK_TO_ADD_H_ */
