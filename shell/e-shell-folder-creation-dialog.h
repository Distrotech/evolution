/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-shell.c
 *
 * Copyright (C) 2000  Ximian, Inc.
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
 * Author: Ettore Perazzoli
 */

#ifndef E_SHELL_FOLDER_CREATION_DIALOG_H
#define E_SHELL_FOLDER_CREATION_DIALOG_H

#include <gtk/gtkwindow.h>

#include "e-shell.h"

enum _EShellFolderCreationDialogResult {
	E_SHELL_FOLDER_CREATION_DIALOG_RESULT_SUCCESS,
	E_SHELL_FOLDER_CREATION_DIALOG_RESULT_FAIL,
	E_SHELL_FOLDER_CREATION_DIALOG_RESULT_CANCEL
};
typedef enum _EShellFolderCreationDialogResult EShellFolderCreationDialogResult;

typedef void (* EShellFolderCreationDialogCallback) (EShell *shell,
						     EShellFolderCreationDialogResult result,
						     const char *path,
						     void *data);

void  e_shell_show_folder_creation_dialog  (EShell                             *shell,
					    GtkWindow                          *parent,
					    const char                         *default_parent_folder,
					    const char                         *default_type,
					    EShellFolderCreationDialogCallback  result_callback,
					    void                               *result_callback_data);

#endif /* E_SHELL_FOLDER_CREATION_DIALOG_H */
