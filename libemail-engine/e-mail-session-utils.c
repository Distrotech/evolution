/*
 * e-mail-session-utils.c
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
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "e-mail-session-utils.h"

#include "e-mail-utils.h"

#include <glib/gi18n-lib.h>

#include "mail-tools.h"
#include "e-mail-local.h"
#include "e-mail-folder-utils.h"
#include <libemail-utils/e-account-utils.h>

/* X-Mailer header value */
#define X_MAILER ("Evolution Mail Data Server" PACKAGE_VERSION)

/* FIXME: Do this after we move filter/ to eds */
#define E_FILTER_SOURCE_OUTGOING  "outgoing"/* performed on outgoing mail */

typedef struct _AsyncContext AsyncContext;

struct _AsyncContext {
	CamelFolder *sent_folder;

	CamelMimeMessage *message;
	CamelMessageInfo *info;

	CamelAddress *from;
	CamelAddress *recipients;

	CamelFilterDriver *driver;

	GCancellable *cancellable;
	gint io_priority;

	/* X-Evolution headers */
	struct _camel_header_raw *xev;

	GPtrArray *post_to_uris;

	gchar *folder_uri;
	gchar *message_uid;
	gchar *transport_uid;
	gchar *sent_folder_uri;
};

static void
async_context_free (AsyncContext *context)
{
	if (context->sent_folder != NULL)
		g_object_unref (context->sent_folder);

	if (context->message != NULL)
		g_object_unref (context->message);

	if (context->info != NULL)
		camel_message_info_free (context->info);

	if (context->from != NULL)
		g_object_unref (context->from);

	if (context->recipients != NULL)
		g_object_unref (context->recipients);

	if (context->driver != NULL)
		g_object_unref (context->driver);

	if (context->cancellable != NULL) {
		camel_operation_pop_message (context->cancellable);
		g_object_unref (context->cancellable);
	}

	if (context->xev != NULL)
		camel_header_raw_clear (&context->xev);

	if (context->post_to_uris != NULL) {
		g_ptr_array_foreach (
			context->post_to_uris, (GFunc) g_free, NULL);
		g_ptr_array_free (context->post_to_uris, TRUE);
	}

	g_free (context->folder_uri);
	g_free (context->message_uid);
	g_free (context->transport_uid);
	g_free (context->sent_folder_uri);

	g_slice_free (AsyncContext, context);
}

GQuark
e_mail_error_quark (void)
{
	static GQuark quark = 0;

	if (G_UNLIKELY (quark == 0)) {
		const gchar *string = "e-mail-error-quark";
		quark = g_quark_from_static_string (string);
	}

	return quark;
}

static void
mail_session_handle_draft_headers_thread (GSimpleAsyncResult *simple,
                                          EMailSession *session,
                                          GCancellable *cancellable)
{
	AsyncContext *context;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	e_mail_session_handle_draft_headers_sync (
		session, context->message, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

gboolean
e_mail_session_handle_draft_headers_sync (EMailSession *session,
                                          CamelMimeMessage *message,
                                          GCancellable *cancellable,
                                          GError **error)
{
	CamelFolder *folder;
	CamelMedium *medium;
	const gchar *folder_uri;
	const gchar *message_uid;
	const gchar *header_name;
	gboolean success;

	g_return_val_if_fail (E_IS_MAIL_SESSION (session), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	medium = CAMEL_MEDIUM (message);

	header_name = "X-Evolution-Draft-Folder";
	folder_uri = camel_medium_get_header (medium, header_name);

	header_name = "X-Evolution-Draft-Message";
	message_uid = camel_medium_get_header (medium, header_name);

	/* Don't report errors about missing X-Evolution-Draft
	 * headers.  These headers are optional, so their absence
	 * is handled by doing nothing. */
	if (folder_uri == NULL || message_uid == NULL)
		return TRUE;

	folder = e_mail_session_uri_to_folder_sync (
		session, folder_uri, 0, cancellable, error);

	if (folder == NULL)
		return FALSE;

	camel_folder_set_message_flags (
		folder, message_uid,
		CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN,
		CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN);

	success = camel_folder_synchronize_message_sync (
		folder, message_uid, cancellable, error);

	g_object_unref (folder);

	return success;
}

void
e_mail_session_handle_draft_headers (EMailSession *session,
                                     CamelMimeMessage *message,
                                     gint io_priority,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *context;

	g_return_if_fail (E_IS_MAIL_SESSION (session));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	context = g_slice_new0 (AsyncContext);
	context->message = g_object_ref (message);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_mail_session_handle_draft_headers);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		mail_session_handle_draft_headers_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

gboolean
e_mail_session_handle_draft_headers_finish (EMailSession *session,
                                            GAsyncResult *result,
                                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_mail_session_handle_draft_headers), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
mail_session_handle_source_headers_thread (GSimpleAsyncResult *simple,
                                           EMailSession *session,
                                           GCancellable *cancellable)
{
	AsyncContext *context;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	e_mail_session_handle_source_headers_sync (
		session, context->message, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

gboolean
e_mail_session_handle_source_headers_sync (EMailSession *session,
                                           CamelMimeMessage *message,
                                           GCancellable *cancellable,
                                           GError **error)
{
	CamelFolder *folder;
	CamelMedium *medium;
	CamelMessageFlags flags = 0;
	const gchar *folder_uri;
	const gchar *message_uid;
	const gchar *flag_string;
	const gchar *header_name;
	gboolean success;
	guint length, ii;
	gchar **tokens;
	gchar *string;

	g_return_val_if_fail (E_IS_MAIL_SESSION (session), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);

	medium = CAMEL_MEDIUM (message);

	header_name = "X-Evolution-Source-Folder";
	folder_uri = camel_medium_get_header (medium, header_name);

	header_name = "X-Evolution-Source-Message";
	message_uid = camel_medium_get_header (medium, header_name);

	header_name = "X-Evolution-Source-Flags";
	flag_string = camel_medium_get_header (medium, header_name);

	/* Don't report errors about missing X-Evolution-Source
	 * headers.  These headers are optional, so their absence
	 * is handled by doing nothing. */
	if (folder_uri == NULL || message_uid == NULL || flag_string == NULL)
		return TRUE;

	/* Convert the flag string to CamelMessageFlags. */

	string = g_strstrip (g_strdup (flag_string));
	tokens = g_strsplit (string, " ", 0);
	g_free (string);

	/* If tokens is NULL, a length of 0 will skip the loop. */
	length = (tokens != NULL) ? g_strv_length (tokens) : 0;

	for (ii = 0; ii < length; ii++) {
		/* Note: We're only checking for flags known to
		 * be used in X-Evolution-Source-Flags headers.
		 * Add more as needed. */
		if (g_strcmp0 (tokens[ii], "ANSWERED") == 0)
			flags |= CAMEL_MESSAGE_ANSWERED;
		else if (g_strcmp0 (tokens[ii], "ANSWERED_ALL") == 0)
			flags |= CAMEL_MESSAGE_ANSWERED_ALL;
		else if (g_strcmp0 (tokens[ii], "FORWARDED") == 0)
			flags |= CAMEL_MESSAGE_FORWARDED;
		else if (g_strcmp0 (tokens[ii], "SEEN") == 0)
			flags |= CAMEL_MESSAGE_SEEN;
		else
			g_warning (
				"Unknown flag '%s' in %s",
				tokens[ii], header_name);
	}

	g_strfreev (tokens);

	folder = e_mail_session_uri_to_folder_sync (
		session, folder_uri, 0, cancellable, error);

	if (folder == NULL)
		return FALSE;

	camel_folder_set_message_flags (
		folder, message_uid, flags, flags);

	success = camel_folder_synchronize_message_sync (
		folder, message_uid, cancellable, error);

	g_object_unref (folder);

	return success;
}

void
e_mail_session_handle_source_headers (EMailSession *session,
                                      CamelMimeMessage *message,
                                      gint io_priority,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *context;

	g_return_if_fail (E_IS_MAIL_SESSION (session));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	context = g_slice_new0 (AsyncContext);
	context->message = g_object_ref (message);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_mail_session_handle_source_headers);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		mail_session_handle_source_headers_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

gboolean
e_mail_session_handle_source_headers_finish (EMailSession *session,
                                             GAsyncResult *result,
                                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_mail_session_handle_draft_headers), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
mail_session_send_to_thread (GSimpleAsyncResult *simple,
                             EMailSession *session,
                             GCancellable *cancellable)
{
	AsyncContext *context;
	CamelFolder *local_sent_folder;
	GString *error_messages;
	gboolean copy_to_sent = TRUE;
	guint ii;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	/* Send the message to all recipients. */
	if (camel_address_length (context->recipients) > 0) {
		CamelProvider *provider;
		CamelService *service;
		gboolean did_connect = FALSE;

		service = camel_session_get_service (
			CAMEL_SESSION (session), context->transport_uid);

		if (!CAMEL_IS_TRANSPORT (service)) {
			g_simple_async_result_set_error (simple,
				CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_URL_INVALID,
				_("Cannot get transport for account '%s'"),
				context->transport_uid);
			return;
		}

		if (camel_service_get_connection_status (service) != CAMEL_SERVICE_CONNECTED) {
			did_connect = TRUE;

			/* XXX This API does not allow for cancellation. */
			if (!em_utils_connect_service_sync (service, cancellable, &error)) {
				g_simple_async_result_set_from_error (simple, error);
				g_error_free (error);
				return;
			}
		}

		provider = camel_service_get_provider (service);

		if (provider->flags & CAMEL_PROVIDER_DISABLE_SENT_FOLDER)
			copy_to_sent = FALSE;

		camel_transport_send_to_sync (
			CAMEL_TRANSPORT (service),
			context->message, context->from,
			context->recipients, cancellable, &error);

		if (did_connect)
			em_utils_disconnect_service_sync (
				service, error == NULL,
				cancellable, error ? NULL : &error);

		if (error != NULL) {
			g_simple_async_result_set_from_error (simple, error);
			g_error_free (error);
			return;
		}
	}

	/* Post the message to requested folders. */
	for (ii = 0; ii < context->post_to_uris->len; ii++) {
		CamelFolder *folder;
		const gchar *folder_uri;

		folder_uri = g_ptr_array_index (context->post_to_uris, ii);

		folder = e_mail_session_uri_to_folder_sync (
			session, folder_uri, 0, cancellable, &error);

		if (error != NULL) {
			g_warn_if_fail (folder == NULL);
			g_simple_async_result_set_from_error (simple, error);
			g_error_free (error);
			return;
		}

		g_return_if_fail (CAMEL_IS_FOLDER (folder));

		camel_folder_append_message_sync (
			folder, context->message, context->info,
			NULL, cancellable, &error);

		g_object_unref (folder);

		if (error != NULL) {
			g_simple_async_result_set_from_error (simple, error);
			g_error_free (error);
			return;
		}
	}

	/*** Post Processing ***/

	/* This accumulates error messages during post-processing. */
	error_messages = g_string_sized_new (256);

	mail_tool_restore_xevolution_headers (context->message, context->xev);

	/* Run filters on the outgoing message. */
	if (context->driver != NULL) {
		camel_filter_driver_filter_message (
			context->driver, context->message, context->info,
			NULL, NULL, NULL, "", cancellable, &error);

		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			goto exit;

		if (error != NULL) {
			g_string_append_printf (
				error_messages,
				_("Failed to apply outgoing filters: %s"),
				error->message);
			g_clear_error (&error);
		}
	}

	if (!copy_to_sent)
		goto cleanup;

	/* Append the sent message to a Sent folder. */

	local_sent_folder = e_mail_local_get_folder (E_MAIL_LOCAL_FOLDER_SENT);

	/* Try to extract a CamelFolder from the Sent folder URI. */
	if (context->sent_folder_uri != NULL) {
		context->sent_folder = e_mail_session_uri_to_folder_sync (
			session, context->sent_folder_uri, 0,
			cancellable, &error);
		if (error != NULL) {
			g_warn_if_fail (context->sent_folder == NULL);
			if (error_messages->len > 0)
				g_string_append (error_messages, "\n\n");
			g_string_append_printf (
				error_messages,
				_("Failed to append to %s: %s\n"
				  "Appending to local 'Sent' folder instead."),
				context->sent_folder_uri, error->message);
			g_clear_error (&error);
		}
	}

	/* Fall back to the local Sent folder. */
	if (context->sent_folder == NULL)
		context->sent_folder = g_object_ref (local_sent_folder);

	/* Append the message. */
	camel_folder_append_message_sync (
		context->sent_folder, context->message,
		context->info, NULL, cancellable, &error);

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		goto exit;

	if (error == NULL)
		goto cleanup;

	/* If appending to a remote Sent folder failed,
	 * try appending to the local Sent folder. */
	if (context->sent_folder != local_sent_folder) {
		const gchar *description;

		description = camel_folder_get_description (
			context->sent_folder);

		if (error_messages->len > 0)
			g_string_append (error_messages, "\n\n");
		g_string_append_printf (
			error_messages,
			_("Failed to append to %s: %s\n"
			  "Appending to local 'Sent' folder instead."),
			description, error->message);
		g_clear_error (&error);

		camel_folder_append_message_sync (
			local_sent_folder, context->message,
			context->info, NULL, cancellable, &error);
	}

	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		goto exit;

	/* We can't even append to the local Sent folder?
	 * In that case just leave the message in Outbox. */
	if (error != NULL) {
		if (error_messages->len > 0)
			g_string_append (error_messages, "\n\n");
		g_string_append_printf (
			error_messages,
			_("Failed to append to local 'Sent' folder: %s"),
			error->message);
		g_clear_error (&error);
		goto exit;
	}

cleanup:

	/* The send operation was successful; ignore cleanup errors. */

	/* Mark the draft message for deletion, if present. */
	e_mail_session_handle_draft_headers_sync (
		session, context->message, cancellable, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}

	/* Set flags on the original source message, if present.
	 * Source message refers to the message being forwarded
	 * or replied to. */
	e_mail_session_handle_source_headers_sync (
		session, context->message, cancellable, &error);
	if (error != NULL) {
		g_warning ("%s", error->message);
		g_clear_error (&error);
	}

exit:

	/* If we were cancelled, disregard any other errors. */
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);

	/* Stuff the accumulated error messages in a GError. */
	} else if (error_messages->len > 0) {
		g_simple_async_result_set_error (
			simple, E_MAIL_ERROR,
			E_MAIL_ERROR_POST_PROCESSING,
			"%s", error_messages->str);
	}

	/* Synchronize the Sent folder. */
	if (context->sent_folder != NULL)
		camel_folder_synchronize_sync (
			context->sent_folder, FALSE, cancellable, NULL);

	g_string_free (error_messages, TRUE);
}

void
e_mail_session_send_to (EMailSession *session,
                        CamelMimeMessage *message,
                        gint io_priority,
                        GCancellable *cancellable,
                        CamelFilterGetFolderFunc get_folder_func,
                        gpointer get_folder_data,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *context;
	CamelAddress *from;
	CamelAddress *recipients;
	CamelMedium *medium;
	CamelMessageInfo *info;
	EAccount *account = NULL;
	GPtrArray *post_to_uris;
	struct _camel_header_raw *xev;
	struct _camel_header_raw *header;
	const gchar *string;
	const gchar *resent_from;
	gchar *transport_uid = NULL;
	gchar *sent_folder_uri = NULL;
	GError *error = NULL;

	g_return_if_fail (E_IS_MAIL_SESSION (session));
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	medium = CAMEL_MEDIUM (message);

	camel_medium_set_header (medium, "X-Mailer", X_MAILER);

	xev = mail_tool_remove_xevolution_headers (message);

	/* Extract directives from X-Evolution headers. */

	string = camel_header_raw_find (&xev, "X-Evolution-Account", NULL);
	if (string != NULL) {
		gchar *account_uid;

		account_uid = g_strstrip (g_strdup (string));
		account = e_get_account_by_uid (account_uid);
		g_free (account_uid);
	}

	if (account != NULL) {
		if (account->transport != NULL) {

			/* XXX Transport UIDs are kludgy right now.  We
			 *     use the EAccount's regular UID and tack on
			 *     "-transport".  Will be better soon. */
			transport_uid = g_strconcat (
				account->uid, "-transport", NULL);

			/* to reprompt password on sending if needed */
			account->transport->get_password_canceled = FALSE;
		}
		sent_folder_uri = g_strdup (account->sent_folder_uri);
	}

	string = camel_header_raw_find (&xev, "X-Evolution-Fcc", NULL);
	if (sent_folder_uri == NULL && string != NULL)
		sent_folder_uri = g_strstrip (g_strdup (string));

	string = camel_header_raw_find (&xev, "X-Evolution-Transport", NULL);
	if (transport_uid == NULL && string != NULL)
		transport_uid = g_strstrip (g_strdup (string));

	post_to_uris = g_ptr_array_new ();
	for (header = xev; header != NULL; header = header->next) {
		gchar *folder_uri;

		if (g_strcmp0 (header->name, "X-Evolution-PostTo") != 0)
			continue;

		folder_uri = g_strstrip (g_strdup (header->value));
		g_ptr_array_add (post_to_uris, folder_uri);
	}

	/* Collect sender and recipients from headers. */

	from = (CamelAddress *) camel_internet_address_new ();
	recipients = (CamelAddress *) camel_internet_address_new ();
	resent_from = camel_medium_get_header (medium, "Resent-From");

	if (resent_from != NULL) {
		const CamelInternetAddress *addr;
		const gchar *type;

		camel_address_decode (from, resent_from);

		type = CAMEL_RECIPIENT_TYPE_RESENT_TO;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));

		type = CAMEL_RECIPIENT_TYPE_RESENT_CC;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));

		type = CAMEL_RECIPIENT_TYPE_RESENT_BCC;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));

	} else {
		const CamelInternetAddress *addr;
		const gchar *type;

		addr = camel_mime_message_get_from (message);
		camel_address_copy (from, CAMEL_ADDRESS (addr));

		type = CAMEL_RECIPIENT_TYPE_TO;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));

		type = CAMEL_RECIPIENT_TYPE_CC;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));

		type = CAMEL_RECIPIENT_TYPE_BCC;
		addr = camel_mime_message_get_recipients (message, type);
		camel_address_cat (recipients, CAMEL_ADDRESS (addr));
	}

	/* Miscellaneous preparations. */

	info = camel_message_info_new (NULL);
	camel_message_info_set_flags (info, CAMEL_MESSAGE_SEEN, ~0);

	/* The rest of the processing happens in a thread. */

	context = g_slice_new0 (AsyncContext);
	context->message = g_object_ref (message);
	context->io_priority = io_priority;
	context->from = from;
	context->recipients = recipients;
	context->message = g_object_ref (message);
	context->info = info;
	context->xev = xev;
	context->post_to_uris = post_to_uris;
	context->transport_uid = transport_uid;
	context->sent_folder_uri = sent_folder_uri;

	if (G_IS_CANCELLABLE (cancellable))
		context->cancellable = g_object_ref (cancellable);

	/* Failure here emits a runtime warning but is non-fatal. */
	context->driver = camel_session_get_filter_driver (
		CAMEL_SESSION (session), E_FILTER_SOURCE_OUTGOING, &error);
	if (context->driver != NULL && get_folder_func)
		camel_filter_driver_set_folder_func (
			context->driver, get_folder_func, get_folder_data);
	if (error != NULL) {
		g_warn_if_fail (context->driver == NULL);
		g_warning ("%s", error->message);
		g_error_free (error);
	}

	/* This gets popped in async_context_free(). */
	camel_operation_push_message (
		context->cancellable, _("Sending message"));

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback,
		user_data, e_mail_session_send_to);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		mail_session_send_to_thread,
		context->io_priority,
		context->cancellable);

	g_object_unref (simple);
}

gboolean
e_mail_session_send_to_finish (EMailSession *session,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_mail_session_send_to), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
mail_session_unsubscribe_folder_thread (GSimpleAsyncResult *simple,
                                        EMailSession *session,
                                        GCancellable *cancellable)
{
	AsyncContext *context;
	GError *error = NULL;

	context = g_simple_async_result_get_op_res_gpointer (simple);

	e_mail_session_unsubscribe_folder_sync (
		session, context->folder_uri, cancellable, &error);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		g_error_free (error);
	}
}

gboolean
e_mail_session_unsubscribe_folder_sync (EMailSession *session,
                                        const gchar *folder_uri,
                                        GCancellable *cancellable,
                                        GError **error)
{
	CamelStore *store = NULL;
	gchar *folder_name = NULL;
	const gchar *message;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_MAIL_SESSION (session), FALSE);
	g_return_val_if_fail (folder_uri != NULL, FALSE);

	success = e_mail_folder_uri_parse (
		CAMEL_SESSION (session), folder_uri,
		&store, &folder_name, error);

	if (!success)
		return FALSE;

	message = _("Unsubscribing from folder '%s'");
	camel_operation_push_message (cancellable, message, folder_name);

	/* FIXME This should take our GCancellable. */
	success =
		em_utils_connect_service_sync (
			CAMEL_SERVICE (store), cancellable, error) &&
		camel_subscribable_unsubscribe_folder_sync (
			CAMEL_SUBSCRIBABLE (store),
			folder_name, cancellable, error);

	camel_operation_pop_message (cancellable);

	g_object_unref (store);
	g_free (folder_name);

	return success;
}

void
e_mail_session_unsubscribe_folder (EMailSession *session,
                                   const gchar *folder_uri,
                                   gint io_priority,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *context;

	g_return_if_fail (E_IS_MAIL_SESSION (session));
	g_return_if_fail (folder_uri != NULL);

	context = g_slice_new0 (AsyncContext);
	context->folder_uri = g_strdup (folder_uri);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data,
		e_mail_session_unsubscribe_folder);

	g_simple_async_result_set_op_res_gpointer (
		simple, context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		mail_session_unsubscribe_folder_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

gboolean
e_mail_session_unsubscribe_folder_finish (EMailSession *session,
                                          GAsyncResult *result,
                                          GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session),
		e_mail_session_unsubscribe_folder), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}
