/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-util.h
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

#ifndef _E_UTIL_H_
#define _E_UTIL_H_

#include <sys/types.h>
#include <gtk/gtktypeutils.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_MAKE_TYPE(l,str,t,ci,i,parent) \
GtkType l##_get_type(void)\
{\
	static GtkType type = 0;\
	if (!type){\
		GtkTypeInfo info = {\
			str,\
			sizeof (t),\
			sizeof (t##Class),\
			(GtkClassInitFunc) ci,\
			(GtkObjectInitFunc) i,\
			NULL, /* reserved 1 */\
			NULL, /* reserved 2 */\
			(GtkClassInitFunc) NULL\
		};\
                type = gtk_type_unique (parent, &info);\
	}\
	return type;\
}


#define E_MAKE_X_TYPE(l,str,t,ci,i,parent,poa_init,offset) \
GtkType l##_get_type(void)\
{\
	static GtkType type = 0;\
	if (!type){\
		GtkTypeInfo info = {\
			str,\
			sizeof (t),\
			sizeof (t##Class),\
			(GtkClassInitFunc) ci,\
			(GtkObjectInitFunc) i,\
			NULL, /* reserved 1 */\
			NULL, /* reserved 2 */\
			(GtkClassInitFunc) NULL\
		};\
                type = bonobo_x_type_unique (\
			parent, poa_init, NULL,\
			offset, &info);\
	}\
	return type;\
}

#if 1
#  define E_OBJECT_CLASS_ADD_SIGNALS(oc,sigs,last) \
	gtk_object_class_add_signals (oc, sigs, last)
#  define E_OBJECT_CLASS_TYPE(oc) (oc)->type
#else
#  define E_OBJECT_CLASS_ADD_SIGNALS(oc,sigs,last)
#  define E_OBJECT_CLASS_TYPE(oc) G_TYPE_FROM_CLASS (oc)
#endif


typedef enum {
	E_FOCUS_NONE,
	E_FOCUS_CURRENT,
	E_FOCUS_START,
	E_FOCUS_END
} EFocus;
int       g_str_compare                                                    (const void        *x,
									    const void        *y);
int       g_int_compare                                                    (const void        *x,
									    const void        *y);
char     *e_strdup_strip                                                   (const char        *string);
void      e_free_object_list                                               (GList             *list);
void      e_free_object_slist                                              (GSList            *list);
void      e_free_string_list                                               (GList             *list);
void      e_free_string_slist                                              (GSList            *list);
char     *e_read_file                                                      (const char        *filename);
int       e_write_file                                                     (const char        *filename,
									    const char        *data,
									    int                flags);
int       e_write_file_mkstemp                                             (char              *filename,
									    const char        *data);
int       e_mkdir_hier                                                     (const char        *path,
									    mode_t             mode);

gchar   **e_strsplit                                                	   (const gchar      *string,
								    	    const gchar      *delimiter,
								    	    gint              max_tokens);
gchar    *e_strstrcase                                                     (const gchar       *haystack,
									    const gchar       *needle);
void      e_filename_make_safe                                             (gchar             *string);
gchar    *e_format_number                                                  (gint               number);
gchar    *e_format_number_float                                            (gfloat             number);
gboolean  e_create_directory                                               (gchar             *directory);


typedef int (*ESortCompareFunc) (const void *first,
				 const void *second,
				 gpointer    closure);
void      e_sort                                                           (void              *base,
									    size_t             nmemb,
									    size_t             size,
									    ESortCompareFunc   compare,
									    gpointer           closure);
void      e_bsearch                                                        (const void        *key,
									    const void        *base,
									    size_t             nmemb,
									    size_t             size,
									    ESortCompareFunc   compare,
									    gpointer           closure,
									    size_t            *start,
									    size_t            *end);
size_t    e_strftime_fix_am_pm                                             (char              *s,
									    size_t             max,
									    const char        *fmt,
									    const struct tm   *tm);


/* String to/from double conversion functions */
gdouble   e_flexible_strtod                                                (const gchar       *nptr,
									    gchar            **endptr);
/* 29 bytes should enough for all possible values that
 * g_ascii_dtostr can produce with the %.17g format.
 * Then add 10 for good measure */
#define E_ASCII_DTOSTR_BUF_SIZE (29 + 10)
gchar    *e_ascii_dtostr                                                   (gchar             *buffer,
									    gint               buf_len,
									    const gchar       *format,
									    gdouble            d);

/* Alternating char * and int arguments with a NULL char * to end.
   Less than 0 for the int means copy the whole string. */
gchar    *e_strdup_append_strings                                          (gchar             *first_string,
									    ...);

/* Marshallers */
void      e_marshal_INT__INT_INT_POINTER                                   (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__INT_POINTER_INT_POINTER                           (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_NONE__OBJECT_DOUBLE_DOUBLE_BOOL                        (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_DOUBLE__OBJECT_DOUBLE_DOUBLE_BOOL                      (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_BOOL__OBJECT_DOUBLE_DOUBLE_BOOL                        (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_INT_POINTER_POINTER_UINT_UINT e_marshal_NONE__INT_INT_POINTER_POINTER_INT_INT
void      e_marshal_NONE__INT_INT_POINTER_POINTER_INT_INT                  (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_POINTER_INT_POINTER_POINTER_UINT_UINT e_marshal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT
void      e_marshal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT          (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_INT_POINTER_UINT e_marshal_NONE__INT_INT_POINTER_INT
void      e_marshal_NONE__INT_INT_POINTER_INT                              (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_POINTER_INT_POINTER_UINT e_marshal_NONE__INT_POINTER_INT_POINTER_INT
void      e_marshal_NONE__INT_POINTER_INT_POINTER_INT                      (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_BOOL__INT_INT_POINTER_INT_INT_UINT e_marshal_BOOL__INT_INT_POINTER_INT_INT_INT
void      e_marshal_BOOL__INT_INT_POINTER_INT_INT_INT                      (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_UINT e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT
void      e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT              (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_INT_POINTER_INT_INT_POINTER_UINT_UINT e_marshal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT
void      e_marshal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT          (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);

#define e_marshal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_UINT_UINT e_marshal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT
void      e_marshal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT  (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_NONE__POINTER_POINTER_INT                              (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_NONE__INT_POINTER_INT_POINTER                          (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__POINTER_POINTER                                   (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__POINTER_POINTER_POINTER                           (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__POINTER_POINTER_POINTER_POINTER                   (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__POINTER_POINTER_POINTER_POINTER_POINTER           (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_NONE__POINTER_POINTER_POINTER_BOOL	                   (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_NONE__POINTER_INT_INT_INT                              (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);
void      e_marshal_INT__OBJECT_POINTER                                    (GtkObject         *object,
									    GtkSignalFunc      func,
									    gpointer           func_data,
									    GtkArg            *args);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_UTIL_H_ */
