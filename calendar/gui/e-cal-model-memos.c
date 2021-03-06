/*
 * Evolution memos - Data model for ETable
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
 * Authors:
 *		Rodrigo Moya <rodrigo@ximian.com>
 *      Nathan Owens <pianocomp81@yahoo.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n.h>
#include "e-cal-model-memos.h"
#include "e-cell-date-edit-text.h"
#include "misc.h"

#define d(x) (x)

/* Forward Declarations */
static void	e_cal_model_memos_table_model_init
					(ETableModelInterface *interface);

static ETableModelInterface *table_model_parent_interface;

G_DEFINE_TYPE_WITH_CODE (
	ECalModelMemos,
	e_cal_model_memos,
	E_TYPE_CAL_MODEL,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_TABLE_MODEL,
		e_cal_model_memos_table_model_init))

static void
cal_model_memos_fill_component_from_model (ECalModel *model,
                                           ECalModelComponent *comp_data,
                                           ETableModel *source_model,
                                           gint row)
{
	icaltimetype start;
	g_return_if_fail (E_IS_CAL_MODEL_MEMOS (model));
	g_return_if_fail (comp_data != NULL);
	g_return_if_fail (E_IS_TABLE_MODEL (source_model));

	start = icalcomponent_get_dtstart (comp_data->icalcomp);
	if (icaltime_compare_date_only (start, icaltime_null_time ()) == 0) {
		start = icaltime_today ();
		icalcomponent_set_dtstart (comp_data->icalcomp, start);
	}
}

static gint
cal_model_memos_column_count (ETableModel *etm)
{
	return E_CAL_MODEL_MEMOS_FIELD_LAST;
}

static gpointer
cal_model_memos_value_at (ETableModel *etm,
                          gint col,
                          gint row)
{
	ECalModelComponent *comp_data;
	ECalModelMemos *model = (ECalModelMemos *) etm;

	g_return_val_if_fail (E_IS_CAL_MODEL_MEMOS (model), NULL);

	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, NULL);
	g_return_val_if_fail (row >= 0 && row < e_table_model_row_count (etm), NULL);

	if (col < E_CAL_MODEL_FIELD_LAST)
		return table_model_parent_interface->value_at (etm, col, row);

	comp_data = e_cal_model_get_component_at (E_CAL_MODEL (model), row);
	if (!comp_data)
		return (gpointer) "";

	return (gpointer) "";
}

static void
cal_model_memos_set_value_at (ETableModel *etm,
                              gint col,
                              gint row,
                              gconstpointer value)
{
	ECalModelComponent *comp_data;
	ECalModelMemos *model = (ECalModelMemos *) etm;
	GError *error = NULL;

	g_return_if_fail (E_IS_CAL_MODEL_MEMOS (model));
	g_return_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST);
	g_return_if_fail (row >= 0 && row < e_table_model_row_count (etm));

	if (col < E_CAL_MODEL_FIELD_LAST) {
		table_model_parent_interface->set_value_at (etm, col, row, value);
		return;
	}

	comp_data = e_cal_model_get_component_at (E_CAL_MODEL (model), row);
	if (!comp_data) {
		g_warning ("couldn't get component data: row == %d", row);
		return;
	}

	/* TODO ask about mod type */
	e_cal_client_modify_object_sync (
		comp_data->client, comp_data->icalcomp,
		CALOBJ_MOD_ALL, NULL, &error);

	if (error != NULL) {
		g_warning (
			G_STRLOC ": Could not modify the object! %s",
			error->message);

		/* TODO Show error dialog */
		g_error_free (error);
	}
}

static gboolean
cal_model_memos_is_cell_editable (ETableModel *etm,
                                  gint col,
                                  gint row)
{
	ECalModelMemos *model = (ECalModelMemos *) etm;
	gboolean retval = FALSE;

	g_return_val_if_fail (E_IS_CAL_MODEL_MEMOS (model), FALSE);
	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, FALSE);
	g_return_val_if_fail (row >= -1 || (row >= 0 && row < e_table_model_row_count (etm)), FALSE);

	if (!e_cal_model_test_row_editable (E_CAL_MODEL (etm), row))
		return FALSE;

	if (col < E_CAL_MODEL_FIELD_LAST)
		retval = table_model_parent_interface->is_cell_editable (etm, col, row);

	return retval;
}

static gpointer
cal_model_memos_duplicate_value (ETableModel *etm,
                                 gint col,
                                 gconstpointer value)
{
	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, NULL);

	if (col < E_CAL_MODEL_FIELD_LAST)
		return table_model_parent_interface->duplicate_value (etm, col, value);

	return NULL;
}

static void
cal_model_memos_free_value (ETableModel *etm,
                            gint col,
                            gpointer value)
{
	g_return_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST);

	if (col < E_CAL_MODEL_FIELD_LAST) {
		table_model_parent_interface->free_value (etm, col, value);
		return;
	}
}

static gpointer
cal_model_memos_initialize_value (ETableModel *etm,
                                  gint col)
{
	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, NULL);

	if (col < E_CAL_MODEL_FIELD_LAST)
		return table_model_parent_interface->initialize_value (etm, col);

	return NULL;
}

static gboolean
cal_model_memos_value_is_empty (ETableModel *etm,
                                gint col,
                                gconstpointer value)
{
	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, TRUE);

	if (col < E_CAL_MODEL_FIELD_LAST)
		return table_model_parent_interface->value_is_empty (etm, col, value);

	return TRUE;
}

static gchar *
cal_model_memos_value_to_string (ETableModel *etm,
                                 gint col,
                                 gconstpointer value)
{
	g_return_val_if_fail (col >= 0 && col < E_CAL_MODEL_MEMOS_FIELD_LAST, g_strdup (""));

	if (col < E_CAL_MODEL_FIELD_LAST)
		return table_model_parent_interface->value_to_string (etm, col, value);

	return g_strdup ("");
}

static void
e_cal_model_memos_class_init (ECalModelMemosClass *class)
{
	ECalModelClass *model_class;

	model_class = E_CAL_MODEL_CLASS (class);
	model_class->fill_component_from_model = cal_model_memos_fill_component_from_model;
}

static void
e_cal_model_memos_table_model_init (ETableModelInterface *interface)
{
	table_model_parent_interface =
		g_type_interface_peek_parent (interface);

	interface->column_count = cal_model_memos_column_count;

	interface->value_at = cal_model_memos_value_at;
	interface->set_value_at = cal_model_memos_set_value_at;
	interface->is_cell_editable = cal_model_memos_is_cell_editable;

	interface->duplicate_value = cal_model_memos_duplicate_value;
	interface->free_value = cal_model_memos_free_value;
	interface->initialize_value = cal_model_memos_initialize_value;
	interface->value_is_empty = cal_model_memos_value_is_empty;
	interface->value_to_string = cal_model_memos_value_to_string;
}

static void
e_cal_model_memos_init (ECalModelMemos *model)
{
	e_cal_model_set_component_kind (
		E_CAL_MODEL (model), ICAL_VJOURNAL_COMPONENT);
}

ECalModel *
e_cal_model_memos_new (ESourceRegistry *registry)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	return g_object_new (
		E_TYPE_CAL_MODEL_MEMOS,
		"registry", registry, NULL);
}

