/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Categories configuration.
 *
 * Author:
 *   Rodrigo Moya <rodrigo@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifndef __E_CATEGORIES_CONFIG_H__
#define __E_CATEGORIES_CONFIG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean e_categories_config_get_icon_for (const char *category,
					   GdkPixmap **icon,
					   GdkBitmap **mask);
void     e_categories_config_open_dialog_for_entry (GtkEntry *entry);

G_END_DECLS

#endif
