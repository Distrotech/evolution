/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-table-sorted.c
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <stdlib.h>
#include <gtk/gtksignal.h>
#include <string.h>
#include "gal/util/e-util.h"
#include "e-table-sorted.h"
#include "e-table-sorting-utils.h"

#define d(x)

#define PARENT_TYPE E_TABLE_SUBSET_TYPE

#define INCREMENT_AMOUNT 100

/* maximum insertions between an idle event that we will do without scheduling an idle sort */
#define ETS_INSERT_MAX (4)

static ETableSubsetClass *ets_parent_class;

static void ets_sort_info_changed        (ETableSortInfo *info, ETableSorted *ets);
static void ets_sort                     (ETableSorted *ets);
static void ets_proxy_model_changed      (ETableSubset *etss, ETableModel *source);
static void ets_proxy_model_row_changed  (ETableSubset *etss, ETableModel *source, int row);
static void ets_proxy_model_cell_changed (ETableSubset *etss, ETableModel *source, int col, int row);
static void ets_proxy_model_rows_inserted (ETableSubset *etss, ETableModel *source, int row, int count);
static void ets_proxy_model_rows_deleted  (ETableSubset *etss, ETableModel *source, int row, int count);

static void
ets_destroy (GtkObject *object)
{
	ETableSorted *ets = E_TABLE_SORTED (object);

	if (ets->sort_idle_id) {
		g_source_remove(ets->sort_idle_id);
	}
	if (ets->insert_idle_id) {
		g_source_remove(ets->insert_idle_id);
	}

	if (ets->sort_info) {
		gtk_signal_disconnect (GTK_OBJECT (ets->sort_info),
				       ets->sort_info_changed_id);
		gtk_object_unref(GTK_OBJECT(ets->sort_info));
	}

	if (ets->full_header)
		gtk_object_unref(GTK_OBJECT(ets->full_header));

	GTK_OBJECT_CLASS (ets_parent_class)->destroy (object);
}

static void
ets_class_init (GtkObjectClass *object_class)
{
	ETableSubsetClass *etss_class = E_TABLE_SUBSET_CLASS(object_class);

	ets_parent_class = gtk_type_class (PARENT_TYPE);

	etss_class->proxy_model_changed = ets_proxy_model_changed;
	etss_class->proxy_model_row_changed = ets_proxy_model_row_changed;
	etss_class->proxy_model_cell_changed = ets_proxy_model_cell_changed;
	etss_class->proxy_model_rows_inserted = ets_proxy_model_rows_inserted;
	etss_class->proxy_model_rows_deleted = ets_proxy_model_rows_deleted;

	object_class->destroy = ets_destroy;
}

static void
ets_init (ETableSorted *ets)
{
	ets->full_header = NULL;
	ets->sort_info = NULL;

	ets->sort_info_changed_id = 0;

	ets->sort_idle_id = 0;
	ets->insert_count = 0;
}

E_MAKE_TYPE(e_table_sorted, "ETableSorted", ETableSorted, ets_class_init, ets_init, PARENT_TYPE);

static gboolean
ets_sort_idle(ETableSorted *ets)
{
	gtk_object_ref(GTK_OBJECT(ets));
	ets_sort(ets);
	ets->sort_idle_id = 0;
	ets->insert_count = 0;
	gtk_object_unref(GTK_OBJECT(ets));
	return FALSE;
}

static gboolean
ets_insert_idle(ETableSorted *ets)
{
	ets->insert_count = 0;
	ets->insert_idle_id = 0;
	return FALSE;
}

ETableModel *
e_table_sorted_new (ETableModel *source, ETableHeader *full_header, ETableSortInfo *sort_info)
{
	ETableSorted *ets = gtk_type_new (E_TABLE_SORTED_TYPE);
	ETableSubset *etss = E_TABLE_SUBSET (ets);

	if (ets_parent_class->proxy_model_pre_change)
		(ets_parent_class->proxy_model_pre_change) (etss, source);

	if (e_table_subset_construct (etss, source, 0) == NULL){
		gtk_object_unref (GTK_OBJECT (ets));
		return NULL;
	}

	ets->sort_info = sort_info;
	gtk_object_ref(GTK_OBJECT(ets->sort_info));
	ets->full_header = full_header;
	gtk_object_ref(GTK_OBJECT(ets->full_header));

	ets_proxy_model_changed(etss, source);

	ets->sort_info_changed_id = gtk_signal_connect (GTK_OBJECT (sort_info), "sort_info_changed",
							 GTK_SIGNAL_FUNC (ets_sort_info_changed), ets);

	return E_TABLE_MODEL(ets);
}

static void
ets_sort_info_changed (ETableSortInfo *info, ETableSorted *ets)
{
	ets_sort(ets);
}

static void
ets_proxy_model_changed (ETableSubset *subset, ETableModel *source)
{
	int rows, i;

	rows = e_table_model_row_count(source);

	g_free(subset->map_table);
	subset->n_map = rows;
	subset->map_table = g_new(int, rows);

	for (i = 0; i < rows; i++) {
		subset->map_table[i] = i;
	}

	if (!E_TABLE_SORTED(subset)->sort_idle_id)
		E_TABLE_SORTED(subset)->sort_idle_id = g_idle_add_full(50, (GSourceFunc) ets_sort_idle, subset, NULL);

	e_table_model_changed(E_TABLE_MODEL(subset));
}

static void
ets_proxy_model_row_changed (ETableSubset *subset, ETableModel *source, int row)
{
	if (!E_TABLE_SORTED(subset)->sort_idle_id)
		E_TABLE_SORTED(subset)->sort_idle_id = g_idle_add_full(50, (GSourceFunc) ets_sort_idle, subset, NULL);
	
	if (ets_parent_class->proxy_model_row_changed)
		(ets_parent_class->proxy_model_row_changed) (subset, source, row);
}

static void
ets_proxy_model_cell_changed (ETableSubset *subset, ETableModel *source, int col, int row)
{
	ETableSorted *ets = E_TABLE_SORTED(subset);
	if (e_table_sorting_utils_affects_sort(ets->sort_info, ets->full_header, col))
		ets_proxy_model_row_changed(subset, source, row);
	else if (ets_parent_class->proxy_model_cell_changed)
		(ets_parent_class->proxy_model_cell_changed) (subset, source, col, row);
}

static void
ets_proxy_model_rows_inserted (ETableSubset *etss, ETableModel *source, int row, int count)
{
 	ETableModel *etm = E_TABLE_MODEL(etss);
	ETableSorted *ets = E_TABLE_SORTED(etss);
	int i;
	gboolean full_change = FALSE;

	if (count == 0) {
		e_table_model_no_change (etm);
		return;
	}

	if (row != etss->n_map) {
		full_change = TRUE;
		for (i = 0; i < etss->n_map; i++) {
			if (etss->map_table[i] >= row) {
				etss->map_table[i] += count;
			}
		}
	}

	etss->map_table = g_realloc (etss->map_table, (etss->n_map + count) * sizeof(int));

	for (; count > 0; count --) {
		if (!full_change)
			e_table_model_pre_change (etm);
		i = etss->n_map;
		if (ets->sort_idle_id == 0) {
			/* this is to see if we're inserting a lot of things between idle loops.
			   If we are, we're busy, its faster to just append and perform a full sort later */
			ets->insert_count++;
			if (ets->insert_count > ETS_INSERT_MAX) {
				/* schedule a sort, and append instead */
				ets->sort_idle_id = g_idle_add_full(50, (GSourceFunc) ets_sort_idle, ets, NULL);
			} else {
				/* make sure we have an idle handler to reset the count every now and then */
				if (ets->insert_idle_id == 0) {
					ets->insert_idle_id = g_idle_add_full(40, (GSourceFunc) ets_insert_idle, ets, NULL);
				}
				i = e_table_sorting_utils_insert(etss->source, ets->sort_info, ets->full_header, etss->map_table, etss->n_map, row);
				memmove(etss->map_table + i + 1, etss->map_table + i, (etss->n_map - i) * sizeof(int));
			}
		}
		etss->map_table[i] = row;
		etss->n_map++;
		if (!full_change) {
			e_table_model_row_inserted (etm, i);
		}

		d(g_print("inserted row %d", row));
		row++;
	}
	if (full_change)
		e_table_model_changed (etm);
	else
		e_table_model_no_change (etm);
	d(e_table_subset_print_debugging(etss));
}

static void
ets_proxy_model_rows_deleted (ETableSubset *etss, ETableModel *source, int row, int count)
{
	ETableModel *etm = E_TABLE_MODEL(etss);
	int i;
	gboolean shift;
	int j;

	shift = row == etss->n_map - count;

	for (j = 0; j < count; j++) {
		for (i = 0; i < etss->n_map; i++){
			if (etss->map_table[i] == row + j) {
				if (shift)
					e_table_model_pre_change (etm);
				memmove (etss->map_table + i, etss->map_table + i + 1, (etss->n_map - i - 1) * sizeof(int));
				etss->n_map --;
				if (shift)
					e_table_model_row_deleted (etm, i);
			}
		}
	}
	if (!shift) {
		for (i = 0; i < etss->n_map; i++) {
			if (etss->map_table[i] >= row)
				etss->map_table[i] -= count;
		}

		e_table_model_changed (etm);
	} else {
		e_table_model_no_change (etm);
	}

	d(g_print("deleted row %d count %d", row, count));
	d(e_table_subset_print_debugging(etss));
}

static void
ets_sort(ETableSorted *ets)
{
	ETableSubset *etss = E_TABLE_SUBSET(ets);
	static int reentering = 0;
	if (reentering)
		return;
	reentering = 1;

	e_table_model_pre_change(E_TABLE_MODEL(ets));

	e_table_sorting_utils_sort(etss->source, ets->sort_info, ets->full_header, etss->map_table, etss->n_map);

	e_table_model_changed (E_TABLE_MODEL(ets));
	reentering = 0;
}
