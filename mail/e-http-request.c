/*
 * e-http-request.c
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

#include "e-http-request.h"

#define LIBSOUP_USE_UNSTABLE_REQUEST_API
#include <libsoup/soup.h>
#include <libsoup/soup-requester.h>
#include <libsoup/soup-request-http.h>
#include <camel/camel.h>
#include <webkit/webkit.h>

#include <e-util/e-util.h>
#include <mail/em-utils.h>
#include <libemail-engine/e-mail-enumtypes.h>

#include <string.h>

#include <em-format/e-mail-formatter.h>
#include <shell/e-shell.h>

#define d(x)

#define E_HTTP_REQUEST_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_HTTP_REQUEST, EHTTPRequestPrivate))

struct _EHTTPRequestPrivate {
	gchar *content_type;
	gint content_length;

	EMailPartList *parts_list;
};

G_DEFINE_TYPE (EHTTPRequest, e_http_request, SOUP_TYPE_REQUEST)

struct http_request_async_data {
	EFlag *flag;

	GMainLoop *loop;
	GCancellable *cancellable;
	CamelDataCache *cache;
	gchar *cache_key;

	CamelStream *cache_stream;
	gchar *content_type;
	goffset content_length;

	gchar *buff;
};

static void
http_request_write_to_cache (GInputStream *stream,
                             GAsyncResult *res,
                             struct http_request_async_data *data)
{
	GError *error;
	gssize len;

	error = NULL;
	len = g_input_stream_read_finish (stream, res, &error);

	/* Error while reading data */
	if (len == -1) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_message ("Error while reading input stream: %s",
				error ? error->message : "Unknown error");
			g_clear_error (&error);
		}

		/* Don't keep broken data in cache */
		camel_data_cache_remove (data->cache, "http", data->cache_key, NULL);

		goto cleanup;
	}

	/* EOF */
	if (len == 0) {
		goto cleanup;
	}

	/* Write chunk to cache and read another block of data. */
	camel_stream_write (data->cache_stream, data->buff, len,
		data->cancellable, NULL);

	g_input_stream_read_async (stream, data->buff, 4096,
		G_PRIORITY_DEFAULT, data->cancellable,
		(GAsyncReadyCallback) http_request_write_to_cache, data);

	return;

 cleanup:
	if (data->buff)
		g_free (data->buff);

	g_object_unref (stream);

	g_main_loop_quit (data->loop);
	e_flag_set (data->flag);
}

static void
http_request_finished (SoupRequest *request,
                       GAsyncResult *res,
                       struct http_request_async_data *data)
{
	GError *error;
	SoupMessage *message;
	GInputStream *stream;

	error = NULL;
	stream = soup_request_send_finish (request, res, &error);
	/* If there is an error or the operation was canceled, do nothing */
	if (error) {
		g_main_loop_quit (data->loop);
		e_flag_set (data->flag);
		g_error_free (error);
		return;
	}

	if (!stream) {
		g_warning("HTTP request failed: %s", error ? error->message: "Unknown error");
		g_clear_error (&error);
		g_main_loop_quit (data->loop);
		e_flag_set (data->flag);
		return;
	}

	message = soup_request_http_get_message (SOUP_REQUEST_HTTP (request));
	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		g_warning ("HTTP request failed: HTTP code %d", message->status_code);
		g_object_unref (message);
		g_main_loop_quit (data->loop);
		e_flag_set (data->flag);
		return;
	}

	g_object_unref (message);

	data->content_length = soup_request_get_content_length (request);
	data->content_type = g_strdup (soup_request_get_content_type (request));

	if (!data->cache_stream || g_cancellable_is_cancelled (data->cancellable)) {
		g_main_loop_quit (data->loop);
		e_flag_set (data->flag);
		return;
	}

	data->buff = g_malloc (4096);
	g_input_stream_read_async (stream, data->buff, 4096,
		G_PRIORITY_DEFAULT, data->cancellable,
		(GAsyncReadyCallback) http_request_write_to_cache, data);
}

static gssize
copy_stream_to_stream (CamelStream *input,
                       GMemoryInputStream *output,
                       GCancellable *cancellable)
{
	gchar *buff;
	gssize read_len = 0;
	gssize total_len = 0;

	g_seekable_seek (G_SEEKABLE (input), 0, G_SEEK_SET, cancellable, NULL);

	buff = g_malloc (4096);
	while ((read_len = camel_stream_read (input, buff, 4096, cancellable, NULL)) > 0) {

		g_memory_input_stream_add_data (output, buff, read_len, g_free);

		total_len += read_len;

		buff = g_malloc (4096);
	}

	/* Free the last unused buffer */
	g_free (buff);

	return total_len;
}

static void
quit_main_loop (GCancellable *cancellable,
                GMainLoop *loop)
{
	if (g_main_loop_is_running (loop)) {
		g_main_loop_quit (loop);
	}
}

static void
handle_http_request (GSimpleAsyncResult *res,
                     GObject *object,
                     GCancellable *cancellable)
{
	EHTTPRequest *request = E_HTTP_REQUEST (object);
	SoupURI *soup_uri;
	gchar *evo_uri, *uri;
	gchar *mail_uri;
	GInputStream *stream;
	gboolean force_load_images = FALSE;
	EMailImageLoadingPolicy image_policy;
	gchar *uri_md5;
	EMailFormatter *formatter;
	EShell *shell;

	const gchar *user_cache_dir;
	CamelDataCache *cache;
	CamelStream *cache_stream;

	GHashTable *query;
	gint uri_len;

	if (g_cancellable_is_cancelled (cancellable)) {
		return;
	}

	formatter = e_mail_formatter_new ();

	/* Remove the __evo-mail query */
	soup_uri = soup_request_get_uri (SOUP_REQUEST (request));
	query = soup_form_decode (soup_uri->query);
	mail_uri = g_hash_table_lookup (query, "__evo-mail");
	if (mail_uri)
		mail_uri = g_strdup (mail_uri);

	g_hash_table_remove (query, "__evo-mail");

	/* Remove __evo-load-images if present (and in such case set
	 * force_load_images to TRUE) */
	force_load_images = g_hash_table_remove (query, "__evo-load-images");

	soup_uri_set_query_from_form (soup_uri, query);
	g_hash_table_unref (query);

	evo_uri = soup_uri_to_string (soup_uri, FALSE);

	/* Remove the "evo-" prefix from scheme */
	uri_len = strlen (evo_uri);
	if (evo_uri && (uri_len > 5)) {

		/* Remove trailing "?" if there is no URI query */
		if (evo_uri[uri_len - 1] == '?') {
			uri = g_strndup (evo_uri + 4, uri_len - 5);
		} else {
			uri = g_strdup (evo_uri + 4);
		}
		g_free (evo_uri);
	}

	g_return_if_fail (uri && *uri);

	/* Use MD5 hash of the URI as a filname of the resourec cache file.
	 * We were previously using the URI as a filename but the URI is
	 * sometimes too long for a filename. */
	uri_md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, uri, -1);

	/* Open Evolution's cache */
	user_cache_dir = e_get_user_cache_dir ();
	cache = camel_data_cache_new (user_cache_dir, NULL);
	if (cache) {
		camel_data_cache_set_expire_age (cache, 24 * 60 * 60);
		camel_data_cache_set_expire_access (cache, 2 * 60 * 60);
	}

	/* Found item in cache! */
	cache_stream = camel_data_cache_get (cache, "http", uri_md5, NULL);
	if (cache_stream) {

		gssize len;

		stream = g_memory_input_stream_new ();

		len = copy_stream_to_stream (cache_stream,
			       G_MEMORY_INPUT_STREAM (stream), cancellable);
		request->priv->content_length = len;

		g_object_unref (cache_stream);

		/* When succesfully read some data from cache then
		 * get mimetype and return the stream to WebKit.
		 * Otherwise try to fetch the resource again from the network. */
		if ((len != -1) && (request->priv->content_length > 0)) {
			GFile *file;
			GFileInfo *info;
			gchar *path;

			path = camel_data_cache_get_filename (cache, "http", uri_md5, NULL);
			file = g_file_new_for_path (path);
			info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
					0, cancellable, NULL);

			request->priv->content_type = g_strdup (
				g_file_info_get_content_type (info));

			d(printf ("'%s' found in cache (%d bytes, %s)\n",
				uri, request->priv->content_length,
				request->priv->content_type));

			g_object_unref (info);
			g_object_unref (file);
			g_free (path);

			/* Set result and quit the thread */
			g_simple_async_result_set_op_res_gpointer (res, stream, NULL);

			goto cleanup;
		} else {
			d(printf("Failed to load '%s' from cache.\n", uri));
		}
	}

	/* If the item is not in the cache and Evolution is in offline mode then
	 * quit regardless any image loading policy */
	shell = e_shell_get_default ();
	if (!e_shell_get_online (shell)) {
		goto cleanup;
	}

	/* Item not found in cache, but image loading policy allows us to fetch
	 * it from the interwebs */
	image_policy = e_mail_formatter_get_image_loading_policy (formatter);
	if (!force_load_images && mail_uri &&
	    (image_policy == E_MAIL_IMAGE_LOADING_POLICY_SOMETIMES)) {
		CamelObjectBag *registry;
		gchar *decoded_uri;
		EMailPartList *part_list;

		registry = e_mail_part_list_get_registry ();
		decoded_uri = soup_uri_decode (mail_uri);

		part_list = camel_object_bag_get (registry, decoded_uri);
		if (part_list) {
			EShell *shell;
			ESourceRegistry *registry;
			CamelInternetAddress *addr;

			shell = e_shell_get_default ();
			registry = e_shell_get_registry (shell);
			addr = camel_mime_message_get_from (part_list->message);
			force_load_images = em_utils_in_addressbook (
					registry, addr, FALSE, cancellable);

			g_object_unref (part_list);
		}

		g_free (decoded_uri);
	}

	if ((image_policy == E_MAIL_IMAGE_LOADING_POLICY_ALWAYS) ||
	    force_load_images) {

		SoupRequester *requester;
		SoupRequest *http_request;
		SoupSession *session;
		GMainContext *context;
		GError *error;
		gulong id;

		struct http_request_async_data data = { 0 };

		context = g_main_context_get_thread_default ();
		session = soup_session_async_new_with_options (
					SOUP_SESSION_ASYNC_CONTEXT, context, NULL);

		requester = soup_requester_new ();
		soup_session_add_feature (session, SOUP_SESSION_FEATURE (requester));

		http_request = soup_requester_request (requester, uri, NULL);

		error = NULL;
		data.flag = e_flag_new ();
		data.loop = g_main_loop_new (context, TRUE);
		data.cancellable = cancellable;
		data.cache = cache;
		data.cache_key = uri_md5;
		data.cache_stream = camel_data_cache_add (cache, "http", uri_md5, &error);

		if (!data.cache_stream) {
			g_warning ("Failed to create cache file for '%s': %s",
				uri, error ? error->message : "Unknown error");
			g_clear_error (&error);

			/* We rely on existence of the stream. If CamelDataCache
			 * failed to create a cache file, then store it at least
			 * temporarily. */
			data.cache_stream = camel_stream_mem_new ();
		}

		/* Send the request and waint in mainloop until it's finished
		 * and copied to cache */
		d(printf(" '%s' not in cache, sending HTTP request\n", uri));
		soup_request_send_async (http_request, cancellable,
			(GAsyncReadyCallback) http_request_finished, &data);

		id = g_cancellable_connect (cancellable,
			G_CALLBACK (quit_main_loop), data.loop, NULL);

		/* Wait for the asynchronous HTTP GET to finish */
		g_main_loop_run (data.loop);
		d(printf (" '%s' fetched from internet and (hopefully) stored in"
			  " cache\n", uri));

		g_cancellable_disconnect (cancellable, id);

		/* Wait until all asynchronous operations are finished working
		 * with the 'data' structure so that it's not free'd too early. */
		e_flag_wait (data.flag);
		e_flag_free (data.flag);

		g_main_loop_unref (data.loop);

		g_object_unref (session);
		g_object_unref (http_request);
		g_object_unref (requester);

		/* Copy the content of cache stream to GInputStream that can be
		 * returned to WebKit */
		stream = g_memory_input_stream_new ();
		copy_stream_to_stream (data.cache_stream,
			G_MEMORY_INPUT_STREAM (stream), cancellable);

		camel_stream_close (data.cache_stream, cancellable, NULL);
		g_object_unref (data.cache_stream);

		request->priv->content_length = data.content_length;
		request->priv->content_type = data.content_type;

		g_simple_async_result_set_op_res_gpointer (res, stream, NULL);

		goto cleanup;

	}

 cleanup:
	g_free (uri);
	g_free (uri_md5);
	if (mail_uri)
		g_free (mail_uri);
}

static void
http_request_finalize (GObject *object)
{
	EHTTPRequest *request = E_HTTP_REQUEST (object);

	if (request->priv->content_type) {
		g_free (request->priv->content_type);
		request->priv->content_type = NULL;
	}

	if (request->priv->parts_list) {
		g_object_unref (request->priv->parts_list);
		request->priv->parts_list = NULL;
	}

	G_OBJECT_CLASS (e_http_request_parent_class)->finalize (object);
}

static gboolean
http_request_check_uri (SoupRequest *request,
                       SoupURI *uri,
                       GError **error)
{
	return ((strcmp (uri->scheme, "evo-http") == 0) ||
		(strcmp (uri->scheme, "evo-https") == 0));
}

static void
http_request_send_async (SoupRequest *request,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	EHTTPRequest *ehr;
	GSimpleAsyncResult *simple;
	gchar *mail_uri;
	SoupURI *uri;
	const gchar *enc;
	CamelObjectBag *registry;
	GHashTable *query;

	ehr = E_HTTP_REQUEST (request);
	uri = soup_request_get_uri (request);
	query = soup_form_decode (uri->query);

	d(printf("received request for %s\n", soup_uri_to_string (uri, FALSE)));

	enc = g_hash_table_lookup (query, "__evo-mail");

	if (!enc || !*enc) {
		g_hash_table_destroy (query);
		return;
	}

	mail_uri = soup_uri_decode (enc);

	registry = e_mail_part_list_get_registry ();
	ehr->priv->parts_list = camel_object_bag_get (registry, mail_uri);
	g_free (mail_uri);

	g_return_if_fail (ehr->priv->parts_list);

	simple = g_simple_async_result_new (
		G_OBJECT (request), callback,
		user_data, http_request_send_async);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_run_in_thread (
		simple, handle_http_request,
		G_PRIORITY_DEFAULT, cancellable);

	g_object_unref (simple);

	g_hash_table_destroy (query);
}

static GInputStream *
http_request_send_finish (SoupRequest *request,
                          GAsyncResult *result,
                          GError **error)
{
	GInputStream *stream;

	stream = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

	/* Reset the stream before passing it back to webkit */
	if (stream && G_IS_SEEKABLE (stream))
		g_seekable_seek (G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, NULL);

	if (!stream) /* We must always return something */
		stream = g_memory_input_stream_new ();

	return stream;
}

static goffset
http_request_get_content_length (SoupRequest *request)
{
	EHTTPRequest *efr = E_HTTP_REQUEST (request);

	d(printf("Content-Length: %d bytes\n", efr->priv->content_length));
	return efr->priv->content_length;
}

static const gchar *
http_request_get_content_type (SoupRequest *request)
{
	EHTTPRequest *efr = E_HTTP_REQUEST (request);

	d(printf("Content-Type: %s\n", efr->priv->content_type));

	return efr->priv->content_type;
}

static const gchar *data_schemes[] = { "evo-http", "evo-https", NULL };

static void
e_http_request_class_init (EHTTPRequestClass *class)
{
	GObjectClass *object_class;
	SoupRequestClass *request_class;

	g_type_class_add_private (class, sizeof (EHTTPRequestPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = http_request_finalize;

	request_class = SOUP_REQUEST_CLASS (class);
	request_class->schemes = data_schemes;
	request_class->send_async = http_request_send_async;
	request_class->send_finish = http_request_send_finish;
	request_class->get_content_type = http_request_get_content_type;
	request_class->get_content_length = http_request_get_content_length;
	request_class->check_uri = http_request_check_uri;
}

static void
e_http_request_init (EHTTPRequest *request)
{
	request->priv = E_HTTP_REQUEST_GET_PRIVATE (request);
}

