/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-gray-bar.h
 *
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
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifndef _E_GRAY_BAR_H_
#define _E_GRAY_BAR_H_

#include <gtk/gtkeventbox.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_GRAY_BAR			(e_gray_bar_get_type ())
#define E_GRAY_BAR(obj)			(GTK_CHECK_CAST ((obj), E_TYPE_GRAY_BAR, EGrayBar))
#define E_GRAY_BAR_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), E_TYPE_GRAY_BAR, EGrayBarClass))
#define E_IS_GRAY_BAR(obj)			(GTK_CHECK_TYPE ((obj), E_TYPE_GRAY_BAR))
#define E_IS_GRAY_BAR_CLASS(klass)		(GTK_CHECK_CLASS_TYPE ((obj), E_TYPE_GRAY_BAR))


typedef struct _EGrayBar        EGrayBar;
typedef struct _EGrayBarPrivate EGrayBarPrivate;
typedef struct _EGrayBarClass   EGrayBarClass;

struct _EGrayBar {
	GtkEventBox parent;
};

struct _EGrayBarClass {
	GtkEventBoxClass parent_class;
};


GtkType    e_gray_bar_get_type (void);
GtkWidget *e_gray_bar_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_GRAY_BAR_H_ */
