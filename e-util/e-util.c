/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-xml-utils.c
 * Copyright (C) 2000  Helix Code, Inc.
 * Author: Chris Lahey <clahey@helixcode.com>
 *
 * This library is free software; you can redistribute it and/or
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <glib.h>
#include <gtk/gtkobject.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "e-util.h"
#if 0
#include <libgnomevfs/gnome-vfs.h>
#endif

int
g_str_compare(const void *x, const void *y)
{
  return strcmp(x, y);
}

int
g_int_compare(const void *x, const void *y)
{
  if ( GPOINTER_TO_INT(x) < GPOINTER_TO_INT(y) )
    return -1;
  else if ( GPOINTER_TO_INT(x) == GPOINTER_TO_INT(y) )
    return 0;
  else
    return 1;
}

char *
e_strdup_strip(const char *string)
{
	int i;
	int length = 0;
	int initial = 0;
	for ( i = 0; string[i]; i++ ) {
		if (initial == i && isspace((unsigned char) string[i])) {
			initial ++;
		}
		if (!isspace((unsigned char) string[i])) {
			length = i - initial + 1;
		}
	}
	return g_strndup(string + initial, length);
}

void
e_free_object_list (GList *list)
{
	GList *p;

	for (p = list; p != NULL; p = p->next)
		gtk_object_unref (GTK_OBJECT (p->data));

	g_list_free (list);
}

void
e_free_string_list (GList *list)
{
	GList *p;

	for (p = list; p != NULL; p = p->next)
		g_free (p->data);

	g_list_free (list);
}

#define BUFF_SIZE 1024

char *
e_read_file(const char *filename)
{
	int fd;
	char buffer[BUFF_SIZE];
	GList *list = NULL, *list_iterator;
	GList *lengths = NULL, *lengths_iterator;
	int length = 0;
	int bytes;
	char *ret_val;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return NULL;
	bytes = read(fd, buffer, BUFF_SIZE);
	while (bytes) {
		if (bytes > 0) {
			char *temp = g_malloc(bytes);
			memcpy (temp, buffer, bytes);
			list = g_list_prepend(list, temp);
			lengths = g_list_prepend(lengths, GINT_TO_POINTER(bytes));
			length += bytes;
		} else {
			if (errno != EINTR) {
				close(fd);
				g_list_foreach(list, (GFunc) g_free, NULL);
				g_list_free(list);
				g_list_free(lengths);
				return NULL;
			}
		}
		bytes = read(fd, buffer, BUFF_SIZE);
	}
	ret_val = g_new(char, length + 1);
	ret_val[length] = 0;
	lengths_iterator = lengths;
	list_iterator = list;
	for ( ; list_iterator; list_iterator = list_iterator->next, lengths_iterator = lengths_iterator->next) {
		int this_length = GPOINTER_TO_INT(lengths_iterator->data);
		length -= this_length;
		memcpy(ret_val + length, list_iterator->data, this_length);
	}
	close(fd);
	g_list_foreach(list, (GFunc) g_free, NULL);
	g_list_free(list);
	g_list_free(lengths);
	return ret_val;
}

gint
e_write_file(const char *filename, const char *data, int flags)
{
	int fd;
	int length = strlen(data);
	int bytes;
	fd = open(filename, flags, 0666);
	if (fd == -1)
		return errno;
	while (length > 0) {
		bytes = write(fd, data, length);
		if (bytes > 0) {
			length -= bytes;
			data += bytes;
		} else {
			if (errno != EINTR && errno != EAGAIN) {
				int save_errno = errno;
				close(fd);
				return save_errno;
			}
		}
	}
	close(fd);
	return 0;
}

/**
 * e_mkdir_hier:
 * @path: a directory path
 * @mode: a mode, as for mkdir(2)
 *
 * This creates the named directory with the given @mode, creating
 * any necessary intermediate directories (with the same @mode).
 *
 * Return value: 0 on success, -1 on error, in which case errno will
 * be set as for mkdir(2).
 **/
int
e_mkdir_hier(const char *path, mode_t mode)
{
	char *copy, *p;

	p = copy = g_strdup (path);
	do {
		p = strchr (p + 1, '/');
		if (p)
			*p = '\0';
		if (access (copy, F_OK) == -1) {
			if (mkdir (copy, mode) == -1) {
				g_free (copy);
				return -1;
			}
		}
		if (p)
			*p = '/';
	} while (p);

	g_free (copy);
	return 0;
}

#if 0
char *
e_read_uri(const char *uri)
{
	GnomeVFSHandle *handle;
	GList *list = NULL, *list_iterator;
	GList *lengths = NULL, *lengths_iterator;
	gchar buffer[1025];
	gchar *ret_val;
	int length = 0;
	GnomeVFSFileSize bytes;

	gnome_vfs_open(&handle, uri, GNOME_VFS_OPEN_READ);
	
	gnome_vfs_read(handle, buffer, 1024, &bytes);
	while (bytes) {
		if (bytes) {
			char *temp = g_malloc(bytes);
			memcpy (temp, buffer, bytes);
			list = g_list_prepend(list, temp);
			lengths = g_list_prepend(lengths, GINT_TO_POINTER((gint) bytes));
			length += bytes;
		}
		gnome_vfs_read(handle, buffer, 1024, &bytes);
	}

	ret_val = g_new(char, length + 1);
	ret_val[length] = 0;
	lengths_iterator = lengths;
	list_iterator = list;
	for ( ; list_iterator; list_iterator = list_iterator->next, lengths_iterator = lengths_iterator->next) {
		int this_length = GPOINTER_TO_INT(lengths_iterator->data);
		length -= this_length;
		memcpy(ret_val + length, list_iterator->data, this_length);
	}
	gnome_vfs_close(handle);
	g_list_foreach(list, (GFunc) g_free, NULL);
	g_list_free(list);
	g_list_free(lengths);
	return ret_val;
}
#endif

typedef gint (*GtkSignal_INT__INT_INT_POINTER) (GtkObject * object,
						gint arg1,
						gint arg2,
						gpointer arg3,
						gpointer user_data);

void
e_marshal_INT__INT_INT_POINTER (GtkObject * object,
				GtkSignalFunc func,
				gpointer func_data, GtkArg * args)
{
	GtkSignal_INT__INT_INT_POINTER rfunc;
	gint *return_val;
	return_val = GTK_RETLOC_INT (args[3]);
	rfunc = (GtkSignal_INT__INT_INT_POINTER) func;
	*return_val = (*rfunc) (object,
				GTK_VALUE_INT     (args[0]),
				GTK_VALUE_INT     (args[1]),
				GTK_VALUE_POINTER (args[2]),
				func_data);
}

typedef gint (*GtkSignal_INT__INT_POINTER_INT_POINTER) (GtkObject * object,
							gint arg1,
							gpointer arg2,
							gint arg3,
							gpointer arg4,
							gpointer user_data);

void
e_marshal_INT__INT_POINTER_INT_POINTER (GtkObject * object,
					GtkSignalFunc func,
					gpointer func_data, GtkArg * args)
{
	GtkSignal_INT__INT_POINTER_INT_POINTER rfunc;
	gint *return_val;
	return_val = GTK_RETLOC_INT (args[3]);
	rfunc = (GtkSignal_INT__INT_POINTER_INT_POINTER) func;
	*return_val = (*rfunc) (object,
				GTK_VALUE_INT     (args[0]),
				GTK_VALUE_POINTER (args[1]),
				GTK_VALUE_INT     (args[2]),
				GTK_VALUE_POINTER (args[3]),
				func_data);
}

typedef void (*GtkSignal_NONE__OBJECT_DOUBLE_DOUBLE_BOOL) (GtkObject * object,
							      GtkObject *arg1,
							      gdouble arg2,
							      gdouble arg3,
							      gboolean arg4,
							      gpointer user_data);

void
e_marshal_NONE__OBJECT_DOUBLE_DOUBLE_BOOL (GtkObject * object,
					   GtkSignalFunc func,
					   gpointer func_data, GtkArg * args)
{
	GtkSignal_NONE__OBJECT_DOUBLE_DOUBLE_BOOL rfunc;
	rfunc = (GtkSignal_NONE__OBJECT_DOUBLE_DOUBLE_BOOL) func;
	(*rfunc) (object,
		  GTK_VALUE_OBJECT (args[0]),
		  GTK_VALUE_DOUBLE (args[1]),
		  GTK_VALUE_DOUBLE (args[2]),
		  GTK_VALUE_BOOL   (args[3]),
		  func_data);
}

typedef gdouble (*GtkSignal_DOUBLE__OBJECT_DOUBLE_DOUBLE_BOOL) (GtkObject * object,
								GtkObject *arg1,
								gdouble arg2,
								gdouble arg3,
								gboolean arg4,
								gpointer user_data);

void
e_marshal_DOUBLE__OBJECT_DOUBLE_DOUBLE_BOOL (GtkObject * object,
					GtkSignalFunc func,
					gpointer func_data, GtkArg * args)
{
	GtkSignal_DOUBLE__OBJECT_DOUBLE_DOUBLE_BOOL rfunc;
	gdouble *return_val;
	return_val = GTK_RETLOC_DOUBLE (args[4]);
	rfunc = (GtkSignal_DOUBLE__OBJECT_DOUBLE_DOUBLE_BOOL) func;
	*return_val = (*rfunc) (object,
				GTK_VALUE_OBJECT (args[0]),
				GTK_VALUE_DOUBLE (args[1]),
				GTK_VALUE_DOUBLE (args[2]),
				GTK_VALUE_BOOL   (args[3]),
				func_data);
}

typedef gdouble (*GtkSignal_BOOL__OBJECT_DOUBLE_DOUBLE_BOOL) (GtkObject * object,
							      GtkObject *arg1,
							      gdouble arg2,
							      gdouble arg3,
							      gboolean arg4,
							      gpointer user_data);

void
e_marshal_BOOL__OBJECT_DOUBLE_DOUBLE_BOOL (GtkObject * object,
					GtkSignalFunc func,
					gpointer func_data, GtkArg * args)
{
	GtkSignal_BOOL__OBJECT_DOUBLE_DOUBLE_BOOL rfunc;
	gboolean *return_val;
	return_val = GTK_RETLOC_BOOL (args[4]);
	rfunc = (GtkSignal_BOOL__OBJECT_DOUBLE_DOUBLE_BOOL) func;
	*return_val = (*rfunc) (object,
				GTK_VALUE_OBJECT (args[0]),
				GTK_VALUE_DOUBLE (args[1]),
				GTK_VALUE_DOUBLE (args[2]),
				GTK_VALUE_BOOL   (args[3]),
				func_data);
}

typedef void (*GtkSignal_NONE__INT_INT_POINTER_POINTER_INT_INT) (GtkObject * object,
								 gint arg1, 
								 gint arg2,
								 gpointer arg3,
								 gpointer arg4,
								 gint arg5,
								 gint arg6,
								 gpointer user_data);
void
e_marshal_NONE__INT_INT_POINTER_POINTER_INT_INT (GtkObject * object,
					   GtkSignalFunc func,
					   gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__INT_INT_POINTER_POINTER_INT_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_INT_POINTER_POINTER_INT_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), GTK_VALUE_INT (args[1]), 
	    GTK_VALUE_POINTER (args[2]),
	    GTK_VALUE_POINTER (args[3]),
	    GTK_VALUE_INT (args[4]), GTK_VALUE_INT (args[5]), func_data);
}

typedef void (*GtkSignal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT) (GtkObject * object,
									 gint arg1, 
									 gpointer arg2,
									 gint arg3,
									 gpointer arg4,
									 gpointer arg5,
									 gint arg6,
									 gint arg7,
									 gpointer user_data);
void
e_marshal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT (GtkObject * object,
							 GtkSignalFunc func,
							 gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_POINTER_INT_POINTER_POINTER_INT_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), GTK_VALUE_POINTER (args[1]), GTK_VALUE_INT (args[2]),
	    GTK_VALUE_POINTER (args[3]),
	    GTK_VALUE_POINTER (args[4]),
	    GTK_VALUE_INT (args[5]), GTK_VALUE_INT (args[6]), func_data);
}

typedef void (*GtkSignal_NONE__INT_INT_POINTER_INT) (GtkObject * object,
						     gint arg1, 
						     gint arg2,
						     gpointer arg3,
						     gint arg4, gpointer user_data);
void
e_marshal_NONE__INT_INT_POINTER_INT (GtkObject * object,
			       GtkSignalFunc func,
			       gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__INT_INT_POINTER_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_INT_POINTER_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), GTK_VALUE_INT (args[1]), 
	    GTK_VALUE_POINTER (args[2]), GTK_VALUE_INT (args[3]), func_data);
}

typedef void (*GtkSignal_NONE__INT_POINTER_INT_POINTER_INT) (GtkObject * object,
							     gint arg1, 
							     gpointer arg2,
							     gint arg3,
							     gpointer arg4,
							     gint arg5, gpointer user_data);
void
e_marshal_NONE__INT_POINTER_INT_POINTER_INT (GtkObject * object,
					     GtkSignalFunc func,
					     gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__INT_POINTER_INT_POINTER_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_POINTER_INT_POINTER_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), GTK_VALUE_POINTER (args[1]), GTK_VALUE_INT (args[2]), 
	    GTK_VALUE_POINTER (args[3]), GTK_VALUE_INT (args[4]), func_data);
}

typedef gboolean (*GtkSignal_BOOL__INT_INT_POINTER_INT_INT_INT) (GtkObject * object,
								 gint arg1, 
								 gint arg2,
								 gpointer arg3,
								 gint arg4,
								 gint arg5,
								 gint arg6,
								 gpointer user_data);
void
e_marshal_BOOL__INT_INT_POINTER_INT_INT_INT (GtkObject * object,
				       GtkSignalFunc func,
				       gpointer func_data, GtkArg * args)
{
  GtkSignal_BOOL__INT_INT_POINTER_INT_INT_INT rfunc;
  gboolean *return_val;
  return_val = GTK_RETLOC_BOOL (args[6]);
  rfunc = (GtkSignal_BOOL__INT_INT_POINTER_INT_INT_INT) func;
  *return_val = (*rfunc) (object,
			  GTK_VALUE_INT (args[0]),
			  GTK_VALUE_INT (args[1]), 
			  GTK_VALUE_POINTER (args[2]),
			  GTK_VALUE_INT (args[3]),
			  GTK_VALUE_INT (args[4]),
			  GTK_VALUE_INT (args[5]), func_data);
}

typedef gboolean (*GtkSignal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT) (GtkObject * object,
									 gint arg1, 
									 gpointer arg2,
									 gint arg3,
									 gpointer arg4,
									 gint arg5,
									 gint arg6,
									 gint arg7,
									 gpointer user_data);
void
e_marshal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT (GtkObject * object,
						     GtkSignalFunc func,
						     gpointer func_data, GtkArg * args)
{
  GtkSignal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT rfunc;
  gboolean *return_val;
  return_val = GTK_RETLOC_BOOL (args[6]);
  rfunc = (GtkSignal_BOOL__INT_POINTER_INT_POINTER_INT_INT_INT) func;
  *return_val = (*rfunc) (object,
			  GTK_VALUE_INT (args[0]),
			  GTK_VALUE_POINTER (args[1]),
			  GTK_VALUE_INT (args[2]), 
			  GTK_VALUE_POINTER (args[3]),
			  GTK_VALUE_INT (args[4]),
			  GTK_VALUE_INT (args[5]),
			  GTK_VALUE_INT (args[6]), func_data);
}

typedef void (*GtkSignal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT) (GtkObject *
									 object,
									 gint arg1, 
									 gint arg2,
									 gpointer
									 arg3,
									 gint arg4,
									 gint arg5,
									 gpointer
									 arg6,
									 gint arg7,
									 gint arg8,
									 gpointer
									 user_data);

void
e_marshal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT (GtkObject * object,
						   GtkSignalFunc func,
						   gpointer func_data,
						   GtkArg * args)
{
  GtkSignal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_INT_POINTER_INT_INT_POINTER_INT_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), GTK_VALUE_INT (args[1]), 
	    GTK_VALUE_POINTER (args[2]),
	    GTK_VALUE_INT (args[3]),
	    GTK_VALUE_INT (args[4]),
	    GTK_VALUE_POINTER (args[5]),
	    GTK_VALUE_INT (args[6]), GTK_VALUE_INT (args[7]), func_data);
}

typedef void (*GtkSignal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT) (GtkObject *
										 object,
										 gint arg1, 
										 gpointer arg2,
										 gint arg3,
										 gpointer arg4,
										 gint arg5,
										 gint arg6,
										 gpointer arg7,
										 gint arg8,
										 gint arg9,
										 gpointer user_data);

void
e_marshal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT (GtkObject * object,
								 GtkSignalFunc func,
								 gpointer func_data,
								 GtkArg * args)
{
  GtkSignal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT rfunc;
  rfunc = (GtkSignal_NONE__INT_POINTER_INT_POINTER_INT_INT_POINTER_INT_INT) func;
  (*rfunc) (object,
	    GTK_VALUE_INT (args[0]), 
	    GTK_VALUE_POINTER (args[1]),
	    GTK_VALUE_INT (args[2]), 
	    GTK_VALUE_POINTER (args[3]),
	    GTK_VALUE_INT (args[4]),
	    GTK_VALUE_INT (args[5]),
	    GTK_VALUE_POINTER (args[6]),
	    GTK_VALUE_INT (args[7]), GTK_VALUE_INT (args[8]), func_data);
}

typedef void (*GtkSignal_NONE__POINTER_POINTER_INT) (GtkObject *, gpointer,
						     gpointer, gint, gpointer);

void
e_marshal_NONE__POINTER_POINTER_INT (GtkObject * object, GtkSignalFunc func,
				     gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__POINTER_POINTER_INT rfunc;
  rfunc = (GtkSignal_NONE__POINTER_POINTER_INT) func;
  (*rfunc) (object, GTK_VALUE_POINTER (args[0]), GTK_VALUE_POINTER (args[1]),
	    GTK_VALUE_INT (args[2]), func_data);
}	

typedef void (*GtkSignal_NONE__INT_POINTER_INT_POINTER) (GtkObject *, gint, gpointer,
							 gint, gpointer, gpointer);

void
e_marshal_NONE__INT_POINTER_INT_POINTER (GtkObject * object, GtkSignalFunc func,
					 gpointer func_data, GtkArg * args)
{
  GtkSignal_NONE__INT_POINTER_INT_POINTER rfunc;
  rfunc = (GtkSignal_NONE__INT_POINTER_INT_POINTER) func;
  (*rfunc) (object, GTK_VALUE_INT (args[0]), GTK_VALUE_POINTER (args[1]),
	    GTK_VALUE_INT (args[2]), GTK_VALUE_POINTER (args[3]), func_data);
}	

typedef int (*GtkSignal_INT__POINTER_POINTER) (GtkObject *,
					       gpointer, gpointer, 
					       gpointer user_data);
void
e_marshal_INT__POINTER_POINTER (GtkObject *object,
				GtkSignalFunc func,
				gpointer func_data,
				GtkArg *args)
{
	GtkSignal_INT__POINTER_POINTER rfunc;
	int *return_val;

	rfunc = (GtkSignal_INT__POINTER_POINTER) func;
	return_val = GTK_RETLOC_INT (args[2]);

	*return_val = (*rfunc) (object,
				GTK_VALUE_POINTER (args[0]),
				GTK_VALUE_POINTER (args[1]), 
				func_data);
}

typedef int (*GtkSignal_INT__POINTER_POINTER_POINTER) (GtkObject *,
						       gpointer, gpointer, gpointer, 
						       gpointer user_data);
void
e_marshal_INT__POINTER_POINTER_POINTER (GtkObject *object,
					GtkSignalFunc func,
					gpointer func_data,
					GtkArg *args)
{
	GtkSignal_INT__POINTER_POINTER_POINTER rfunc;
	int *return_val;

	rfunc = (GtkSignal_INT__POINTER_POINTER_POINTER) func;
	return_val = GTK_RETLOC_INT (args[3]);

	*return_val = (*rfunc) (object,
				GTK_VALUE_POINTER (args[0]),
				GTK_VALUE_POINTER (args[1]), 
				GTK_VALUE_POINTER (args[2]),
				func_data);
}

typedef int (*GtkSignal_INT__POINTER_POINTER_POINTER_POINTER) (GtkObject *,
							       gpointer, gpointer, gpointer, gpointer,
							       gpointer user_data);
void
e_marshal_INT__POINTER_POINTER_POINTER_POINTER (GtkObject *object,
						GtkSignalFunc func,
						gpointer func_data,
						GtkArg *args)
{
	GtkSignal_INT__POINTER_POINTER_POINTER_POINTER rfunc;
	int *return_val;

	rfunc = (GtkSignal_INT__POINTER_POINTER_POINTER_POINTER) func;
	return_val = GTK_RETLOC_INT (args[4]);

	*return_val = (*rfunc) (object,
				GTK_VALUE_POINTER (args[0]),
				GTK_VALUE_POINTER (args[1]), 
				GTK_VALUE_POINTER (args[2]),
				GTK_VALUE_POINTER (args[3]),
				func_data);
}

gchar**
e_strsplit (const gchar *string,
	    const gchar *delimiter,
	    gint         max_tokens)
{
  GSList *string_list = NULL, *slist;
  gchar **str_array, *s;
  guint i, n = 1;

  g_return_val_if_fail (string != NULL, NULL);
  g_return_val_if_fail (delimiter != NULL, NULL);

  if (max_tokens < 1)
    max_tokens = G_MAXINT;

  s = strstr (string, delimiter);
  if (s)
    {
      guint delimiter_len = strlen (delimiter);

      do
	{
	  guint len;
	  gchar *new_string;

	  len = s - string;
	  new_string = g_new (gchar, len + 1);
	  strncpy (new_string, string, len);
	  new_string[len] = 0;
	  string_list = g_slist_prepend (string_list, new_string);
	  n++;
	  string = s + delimiter_len;
	  s = strstr (string, delimiter);
	}
      while (--max_tokens && s);
    }

  n++;
  string_list = g_slist_prepend (string_list, g_strdup (string));

  str_array = g_new (gchar*, n);

  i = n - 1;

  str_array[i--] = NULL;
  for (slist = string_list; slist; slist = slist->next)
    str_array[i--] = slist->data;

  g_slist_free (string_list);

  return str_array;
}

gchar *
e_strstrcase (const gchar *haystack, const gchar *needle)
{
	/* find the needle in the haystack neglecting case */
	const gchar *ptr;
	guint len;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	len = strlen(needle);
	if (len > strlen(haystack))
		return NULL;

	if (len == 0)
		return (gchar *) haystack;

	for (ptr = haystack; *(ptr + len - 1) != '\0'; ptr++)
		if (!g_strncasecmp (ptr, needle, len))
			return (gchar *) ptr;

	return NULL;
}

void
e_filename_make_safe (gchar *string)
{
	gchar *p;
	
	g_return_if_fail (string != NULL);
	
	for (p = string; *p; p++) {
		if (!isprint ((unsigned char)*p) || strchr (" /'\"`&();|<>${}!", *p))
			*p = '_';
	}
}

static gint
epow10 (gint number) {
	gint value;

	for (value = 1; number > 0; number --) {
		value *= 10;
	}
	return value;
}

gchar *
e_format_number (gint number)
{
	GList *iterator, *list = NULL;
	struct lconv *locality;
	gint char_length = 0;
	gint group_count = 0;
	guchar *grouping;
	int last_count = 3;
	int divider;
	char *value;
	char *value_iterator;
	int initial_grouping;

	locality = localeconv();
	grouping = locality->grouping;
	initial_grouping = *grouping;
	while (number) {
		char *group;
		switch (*grouping) {
		default:
			last_count = *grouping;
			grouping++;
		case 0:
			divider = epow10(last_count);
			if(!list && (number/divider) > 0) {
				group = g_strdup_printf("%0*d", initial_grouping, number % divider);
			} else {
				group = g_strdup_printf("%d", number % divider);
			}
			number /= divider;
			break;
		case CHAR_MAX:
			group = g_strdup_printf("%d", number);
			number = 0;
			break;
		}
		char_length += strlen(group);
		list = g_list_prepend(list, group);
		group_count ++;
	}

	if (list) {
		value = g_new(char, char_length + (group_count - 1) * strlen(locality->thousands_sep));

		iterator = list;
		value_iterator = value;

		strcpy(value_iterator, iterator->data);
		value_iterator += strlen(iterator->data);
		for (iterator = iterator->next; iterator; iterator = iterator->next) {
			strcpy(value_iterator, locality->thousands_sep);
			value_iterator += strlen(locality->thousands_sep);

			strcpy(value_iterator, iterator->data);
			value_iterator += strlen(iterator->data);
		}
		e_free_string_list (list);
		return value;
	} else {
		return g_strdup("0");
	}
}

gboolean
e_create_directory (gchar *directory)
{
	gchar *full_name;
	gchar *position;
	gchar *current_dir = g_get_current_dir();
	struct stat info;
	gboolean return_value = TRUE;

	if (directory[0] == '/') {
		full_name = g_malloc0 (strlen (directory) + 1);
		strcpy (full_name, directory);
	} else {
		full_name = g_malloc0 (strlen (directory) + strlen (current_dir) + 2);
		sprintf (full_name, "%s/%s", current_dir, directory);
	}

	if ((position = strrchr (full_name, '/')) == full_name) {
		if (stat (full_name, &info)) {
			switch (errno) {
			case ENOENT:
				if (mkdir (full_name, 0777)) {
					switch (errno) {
					default:
						return_value = FALSE;
						break;
					}
				}
				break;
			default:
				return_value = FALSE;
				break;
			}
		}
	} else {
		*position = 0;
		e_create_directory (full_name);
		*position = '/';
		if (stat (full_name, &info)) {
			switch (errno) {
			case ENOENT:
				if (mkdir (full_name, 0777)) {
					switch (errno) {
					default:
						return_value = FALSE;
						break;
					}
				}
				break;
			default:
				return_value = FALSE;
				break;
			}
		}
	}

	g_free (current_dir);
	g_free (full_name);

	return (return_value);
}


/* Perform a binary search for key in base which has nmemb elements
   of size bytes each.  The comparisons are done by (*compare)().  */
void      e_bsearch                                                        (const void       *key,
									    const void       *base,
									    size_t            nmemb,
									    size_t            size,
									    ESortCompareFunc  compare,
									    gpointer          closure,
									    size_t	     *start,
									    size_t	     *end)
{
	size_t l, u, idx;
	const void *p;
	int comparison;
	if (!(start || end))
		return;

	l = 0;
	u = nmemb;
	while (l < u) {
		idx = (l + u) / 2;
		p = (void *) (((const char *) base) + (idx * size));
		comparison = (*compare) (key, p, closure);
		if (comparison < 0)
			u = idx;
		else if (comparison > 0)
			l = idx + 1;
		else {
			size_t lsave, usave;
			lsave = l;
			usave = u;
			if (start) {
				while (l < u) {
					idx = (l + u) / 2;
					p = (void *) (((const char *) base) + (idx * size));
					comparison = (*compare) (key, p, closure);
					if (comparison <= 0)
						u = idx;
					else
						l = idx + 1;
				}
				*start = l;
				
				l = lsave;
				u = usave;
			}
			if (end) {
				while (l < u) {
					idx = (l + u) / 2;
					p = (void *) (((const char *) base) + (idx * size));
					comparison = (*compare) (key, p, closure);
					if (comparison < 0)
						u = idx;
					else
						l = idx + 1;
				}
				*end = l;
			}
			return;
		}
	}

	if (start)
		*start = l;
	if (end)
		*end = l;
}

gpointer closure_closure;
ESortCompareFunc compare_closure;

static int
qsort_callback(const void *data1, const void *data2)
{
	return (*compare_closure) (data1, data2, closure_closure);
}

/* Forget it.  We're just going to use qsort.  I lost the need for a stable sort. */
void
e_sort (void             *base,
	size_t            nmemb,
	size_t            size,
	ESortCompareFunc  compare,
	gpointer          closure)
{
	closure_closure = closure;
	compare_closure = compare;
	qsort(base, nmemb, size, qsort_callback);
#if 0
	void *base_copy;
	int i;
	base_copy = g_malloc(nmemb * size);

	for (i = 0; i < nmemb; i++) {
		int position;
		e_bsearch(base + (i * size), base_copy, i, size, compare, closure, NULL, &position);
		memmove(base_copy + (position + 1) * size, base_copy + position * size, (i - position) * size);
		memcpy(base_copy + position * size, base + i * size, size);
	}
	memcpy(base, base_copy, nmemb * size);
	g_free(base_copy);
#endif
}
