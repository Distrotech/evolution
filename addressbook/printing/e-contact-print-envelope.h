/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-contact-print-envelope.h
 * Copyright (C) 2000  Ximian, Inc.
 * Author: Chris Lahey <clahey@ximian.com>
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

#ifndef E_CONTACT_PRINT_ENVELOPE_H
#define E_CONTACT_PRINT_ENVELOPE_H

#include <addressbook/backend/ebook/e-card.h>
#include <gtk/gtkwidget.h>
#include "e-contact-print-types.h"

GtkWidget *e_contact_print_envelope_dialog_new(ECard *card);
GtkWidget *e_contact_print_envelope_list_dialog_new(GList *list);

#endif /* E_CONTACT_PRINT_ENVELOPE_H */
