/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-summary-url.h
 *
 * Authors: Iain Holmes <iain@ximian.com>
 *
 * Copyright (C) 2000  Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
 */

#ifndef _E_SUMMARY_URL_H__
#define _E_SUMMARY_URL_H__

void e_summary_url_request (GtkHTML *html,
			    const gchar *url,
			    GtkHTMLStream *stream);
void e_summary_url_click (GtkWidget *widget,
			  const char *url,
			  ESummary *esummary);
void e_summary_url_over (GtkHTML *html,
			 const char *uri,
			 ESummary *esummary);

#endif
