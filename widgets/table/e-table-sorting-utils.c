/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <string.h>
#include <e-table-sorting-utils.h>

#define d(x)

/* This takes source rows. */
static int
etsu_compare(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int row1, int row2)
{
	int j;
	int sort_count = e_table_sort_info_sorting_get_count(sort_info);
	int comp_val = 0;
	int ascending = 1;

	for (j = 0; j < sort_count; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *col;
		col = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (col == NULL)
			col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		comp_val = (*col->compare)(e_table_model_value_at (source, col->col_idx, row1),
					   e_table_model_value_at (source, col->col_idx, row2));
		ascending = column.ascending;
		if (comp_val != 0)
			break;
	}
	if (comp_val == 0) {
		if (row1 < row2)
			comp_val = -1;
		if (row1 > row2)
			comp_val = 1;
	}
	if (!ascending)
		comp_val = -comp_val;
	return comp_val;
}

static ETableSortInfo *sort_info_closure;

static void **vals_closure;
static int cols_closure;
static int *ascending_closure;
static GCompareFunc *compare_closure;

/* FIXME: Make it not cache the second and later columns (as if anyone cares.) */

static int
qsort_callback(const void *data1, const void *data2)
{
	gint row1 = *(int *)data1;
	gint row2 = *(int *)data2;
	int j;
	int sort_count = e_table_sort_info_sorting_get_count(sort_info_closure);
	int comp_val = 0;
	int ascending = 1;
	for (j = 0; j < sort_count; j++) {
		comp_val = (*(compare_closure[j]))(vals_closure[cols_closure * row1 + j], vals_closure[cols_closure * row2 + j]);
		ascending = ascending_closure[j];
		if (comp_val != 0)
			break;
	}
	if (comp_val == 0) {
		if (row1 < row2)
			comp_val = -1;
		if (row1 > row2)
			comp_val = 1;
	}
	if (!ascending)
		comp_val = -comp_val;
	return comp_val;
}

void
e_table_sorting_utils_sort(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows)
{
	int total_rows;
	int i;
	int j;
	int cols;

	g_return_if_fail(source != NULL);
	g_return_if_fail(E_IS_TABLE_MODEL(source));
	g_return_if_fail(sort_info != NULL);
	g_return_if_fail(E_IS_TABLE_SORT_INFO(sort_info));
	g_return_if_fail(full_header != NULL);
	g_return_if_fail(E_IS_TABLE_HEADER(full_header));

	total_rows = e_table_model_row_count(source);
	cols = e_table_sort_info_sorting_get_count(sort_info);
	cols_closure = cols;
	vals_closure = g_new(void *, total_rows * cols);
	sort_info_closure = sort_info;
	ascending_closure = g_new(int, cols);
	compare_closure = g_new(GCompareFunc, cols);
	for (j = 0; j < cols; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *col;
		col = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (col == NULL)
			col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		for (i = 0; i < rows; i++) {
			vals_closure[map_table[i] * cols + j] = e_table_model_value_at (source, col->col_idx, map_table[i]);
		}
		compare_closure[j] = col->compare;
		ascending_closure[j] = column.ascending;
	}

	qsort(map_table, rows, sizeof(int), qsort_callback);

	g_free(vals_closure);
	g_free(ascending_closure);
	g_free(compare_closure);
}

gboolean
e_table_sorting_utils_affects_sort  (ETableModel    *source,
				     ETableSortInfo *sort_info,
				     ETableHeader   *full_header,
				     int             col)
{
	int j;
	int cols;

	g_return_val_if_fail(source != NULL, TRUE);
	g_return_val_if_fail(E_IS_TABLE_MODEL(source), TRUE);
	g_return_val_if_fail(sort_info != NULL, TRUE);
	g_return_val_if_fail(E_IS_TABLE_SORT_INFO(sort_info), TRUE);
	g_return_val_if_fail(full_header != NULL, TRUE);
	g_return_val_if_fail(E_IS_TABLE_HEADER(full_header), TRUE);

	cols = e_table_sort_info_sorting_get_count(sort_info);

	for (j = 0; j < cols; j++) {
		ETableSortColumn column = e_table_sort_info_sorting_get_nth(sort_info, j);
		ETableCol *tablecol;
		tablecol = e_table_header_get_column_by_col_idx(full_header, column.column);
		if (tablecol == NULL)
			tablecol = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);
		if (col == tablecol->col_idx)
			return TRUE;
	}
	return FALSE;
}


int
e_table_sorting_utils_insert(ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows, int row)
{
	int i;

	i = 0;
	/* handle insertions when we have a 'sort group' */
	while (i < rows && etsu_compare(source, sort_info, full_header, map_table[i], row) < 0)
		i++;

	return i;
}

int
e_table_sorting_utils_check_position (ETableModel *source, ETableSortInfo *sort_info, ETableHeader *full_header, int *map_table, int rows, int view_row)
{
	int i;
	int row;

	i = view_row;
	row = map_table[i];

	i = view_row;
	if (i < rows && etsu_compare(source, sort_info, full_header, map_table[i + 1], row) < 0) {
		i ++;
		while (i < rows - 1 && etsu_compare(source, sort_info, full_header, map_table[i], row) < 0)
			i ++;
	} else if (i > 0 && etsu_compare(source, sort_info, full_header, map_table[i - 1], row) > 0) {
		i --;
		while (i > 0 && etsu_compare(source, sort_info, full_header, map_table[i], row) > 0)
			i --;
	}
	return i;
}

