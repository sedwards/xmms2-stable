
#include "xmms/xmms.h"
#include "xmms/plugin.h"
#include "xmms/transport.h"
#include "xmms/util.h"
#include "xmms/magic.h"
#include "xmms/ringbuf.h"
#include "xmms/medialib.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include <string.h>

#include <curl/curl.h>

/*
 * Type definitions
 */

typedef struct {
	CURL *curl_easy;
	CURLM *curl_multi;

	gint running_handles, maxfd;
	guint length, bytes_since_meta, meta_offset;

	fd_set fdread, fdwrite, fdexcp;

	gchar *url;

	gboolean error, first_header, running, know_length, stream_with_meta, know_meta_offset, know_mime;

	struct curl_slist *http_aliases;
	struct curl_slist *http_headers;

	xmms_ringbuf_t *ringbuf;

	GThread *thread;
	GMutex *mutex;

	xmms_error_t status;

	xmms_medialib_entry_t pl_entry;
} xmms_curl_data_t;

typedef void (*handler_func_t) (xmms_transport_t *transport, gchar *header);

static void header_handler_contenttype (xmms_transport_t *transport, gchar *header);
static void header_handler_contentlength (xmms_transport_t *transport, gchar *header);
static void header_handler_icy_metaint (xmms_transport_t *transport, gchar *header);
static void header_handler_icy_name (xmms_transport_t *transport, gchar *header);
static void header_handler_icy_genre (xmms_transport_t *transport, gchar *header);
static void header_handler_icy_br (xmms_transport_t *transport, gchar *header);
static void header_handler_last (xmms_transport_t *transport, gchar *header);
static handler_func_t header_handler_find (gchar *header);

typedef struct {
	gchar *name;
	handler_func_t func;
} handler_t;

handler_t handlers[] = {
	{ "content-type", header_handler_contenttype },
	{ "content-length", header_handler_contentlength },
	{ "icy-metaint", header_handler_icy_metaint },
	{ "icy-name", header_handler_icy_name },
	{ "icy-genre", header_handler_icy_genre },
	{ "icy-br", header_handler_icy_br },
	{ "\r\n", header_handler_last },
	{ NULL, NULL }
};

/*
 * Function prototypes
 */

static gboolean xmms_curl_can_handle (const gchar *url);
static gboolean xmms_curl_init (xmms_transport_t *transport, const gchar *url);
static void xmms_curl_close (xmms_transport_t *transport);
static gint xmms_curl_read (xmms_transport_t *transport, gchar *buffer, guint len, xmms_error_t *error);
static gint xmms_curl_size (xmms_transport_t *transport);
static size_t xmms_curl_callback_write (void *ptr, size_t size, size_t nmemb, void *stream);
static size_t xmms_curl_callback_header (void *ptr, size_t size, size_t nmemb, void *stream);
static void xmms_curl_thread (xmms_transport_t *transport);

/*
 * Plugin header
 */

xmms_plugin_t *
xmms_plugin_get (void)
{
	xmms_plugin_t *plugin;

	plugin = xmms_plugin_new (XMMS_PLUGIN_TYPE_TRANSPORT, "curl_http",
			"Curl transport for HTTP " XMMS_VERSION,
			"HTTP transport using CURL");

	xmms_plugin_info_add (plugin, "URL", "http://www.xmms.org");
	xmms_plugin_info_add (plugin, "INFO", "http://curl.haxx.se/libcurl");
	xmms_plugin_info_add (plugin, "Author", "XMMS Team");

	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_CAN_HANDLE, xmms_curl_can_handle);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_INIT, xmms_curl_init);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_READ, xmms_curl_read);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_SIZE, xmms_curl_size);
	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_CLOSE, xmms_curl_close);

	xmms_plugin_config_value_register (plugin, "shoutcastinfo", "1", NULL, NULL);
	xmms_plugin_config_value_register (plugin, "buffersize", "131072", NULL, NULL);
	xmms_plugin_config_value_register (plugin, "verbose", "0", NULL, NULL);
	xmms_plugin_config_value_register (plugin, "connecttimeout", "15", NULL, NULL);

	return plugin;
}

/*
 * Member functions
 */

static gboolean
xmms_curl_can_handle (const gchar *url)
{
	g_return_val_if_fail (url, FALSE);

	XMMS_DBG ("xmms_curl_can_handle (%s)", url);

	if ((g_strncasecmp (url, "http", 4) == 0) || (url[0] == '/')) {
		return TRUE;
	}

	return FALSE;
}

static gboolean
xmms_curl_init (xmms_transport_t *transport, const gchar *url)
{
	xmms_curl_data_t *data;
	xmms_config_value_t *val;
	gint bufsize, metaint, verbose, connecttimeout;

	g_return_val_if_fail (transport, FALSE);
	g_return_val_if_fail (url, FALSE);

	data = g_new0 (xmms_curl_data_t, 1);

	val = xmms_plugin_config_lookup (xmms_transport_plugin_get (transport), "buffersize");
	bufsize = xmms_config_value_int_get (val);

	val = xmms_plugin_config_lookup (xmms_transport_plugin_get (transport), "connecttimeout");
	connecttimeout = xmms_config_value_int_get (val);

	val = xmms_plugin_config_lookup (xmms_transport_plugin_get (transport), "shoutcastinfo");
	metaint = xmms_config_value_int_get (val);

	val = xmms_plugin_config_lookup (xmms_transport_plugin_get (transport),
	                                 "verbose");
	verbose = xmms_config_value_int_get (val);

	data->ringbuf = xmms_ringbuf_new (bufsize);
	data->mutex = g_mutex_new ();
	data->pl_entry = xmms_transport_medialib_entry_get (transport);
	data->url = g_strdup (url);

	/* Set up easy handle */

	data->http_aliases = curl_slist_append (data->http_aliases, "ICY 200 OK");
	data->http_aliases = curl_slist_append (data->http_aliases, "ICY 402 Service Unavailabe");
	data->http_headers = curl_slist_append (data->http_headers, "Icy-MetaData: 1");

	data->curl_easy = curl_easy_init ();

	curl_easy_setopt (data->curl_easy, CURLOPT_URL, data->url);
	curl_easy_setopt (data->curl_easy, CURLOPT_HEADER, 0);	/* No, we _dont_ want headers in body */
	curl_easy_setopt (data->curl_easy, CURLOPT_HTTPGET, 1);
	curl_easy_setopt (data->curl_easy, CURLOPT_FOLLOWLOCATION, 1);	/* Doesn't work in multi though... */
	curl_easy_setopt (data->curl_easy, CURLOPT_AUTOREFERER, 1);
	curl_easy_setopt (data->curl_easy, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt (data->curl_easy, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt (data->curl_easy, CURLOPT_USERAGENT, "XMMS/" XMMS_VERSION);
	curl_easy_setopt (data->curl_easy, CURLOPT_WRITEHEADER, transport);
	curl_easy_setopt (data->curl_easy, CURLOPT_WRITEDATA, transport);
	curl_easy_setopt (data->curl_easy, CURLOPT_HTTP200ALIASES, data->http_aliases);
	curl_easy_setopt (data->curl_easy, CURLOPT_WRITEFUNCTION, xmms_curl_callback_write);
	curl_easy_setopt (data->curl_easy, CURLOPT_HEADERFUNCTION, xmms_curl_callback_header);
	curl_easy_setopt (data->curl_easy, CURLOPT_CONNECTTIMEOUT, connecttimeout);
	curl_easy_setopt (data->curl_easy, CURLOPT_NOSIGNAL, 1);

	if (metaint == 1) {
		curl_easy_setopt (data->curl_easy, CURLOPT_HTTPHEADER, data->http_headers);
		data->stream_with_meta = TRUE;
	}

	/* For some debugging output set this to 1 */
	curl_easy_setopt (data->curl_easy, CURLOPT_VERBOSE, verbose);

	/* Set up multi handle */

	data->curl_multi = curl_multi_init ();

	curl_multi_add_handle (data->curl_multi, data->curl_easy);

	xmms_transport_private_data_set (transport, data);

	/* And add the final touch of complexity to this mess: start up another thread */

	data->running = TRUE;
	data->thread = g_thread_create ((GThreadFunc) xmms_curl_thread, (gpointer) transport, TRUE, NULL);
	g_return_val_if_fail (data->thread, FALSE);

	return TRUE;
}

static gint
xmms_curl_read (xmms_transport_t *transport, gchar *buffer, guint len, xmms_error_t *error)
{
	xmms_curl_data_t *data;
	gint ret;

	g_return_val_if_fail (transport, -1);
	g_return_val_if_fail (buffer, -1);
	g_return_val_if_fail (error, -1);

	data = xmms_transport_private_data_get (transport);
	g_return_val_if_fail (data, -1);

	g_mutex_lock (data->mutex);

	if (len > xmms_ringbuf_size (data->ringbuf)) {
		len = xmms_ringbuf_size (data->ringbuf);
	}

	/* Perhaps we should only wait for 1 byte? */
	xmms_ringbuf_wait_used (data->ringbuf, 1, data->mutex);

	if (xmms_ringbuf_iseos (data->ringbuf)) {
		gint val = -1;

		if (data->status.code == XMMS_ERROR_EOS) {
			val = 0;
		}

		xmms_error_set (error, data->status.code, NULL);

		g_mutex_unlock (data->mutex);
		return val;
	}

	/* normal file transfer */

	if (!data->know_meta_offset || !data->stream_with_meta) {
		ret = xmms_ringbuf_read_wait (data->ringbuf, buffer, len, data->mutex);
		g_mutex_unlock (data->mutex);
		return ret;
	}

	/* stream transfer with shoutcast metainfo */

	if (data->bytes_since_meta == data->meta_offset) {
		gchar **tags;
		gchar *metadata;
		guchar magic;
		gint i;

		data->bytes_since_meta = 0;

		xmms_ringbuf_read_wait (data->ringbuf, &magic, 1, data->mutex);

		if (magic == 0) {
			goto cont;
		}

		metadata = g_malloc0 (magic * 16);
		g_return_val_if_fail (metadata, -1);

		ret = xmms_ringbuf_read_wait (data->ringbuf, metadata, magic * 16, data->mutex);

		XMMS_DBG ("Shoutcast metadata: %s", metadata);

		tags = g_strsplit (metadata, ";", 0);
		for (i = 0; tags[i] != NULL; i++) {
			if (g_strncasecmp (tags[i], "StreamTitle=", 12) == 0) {
				gint r, w;
				gchar *tmp, *tmp2;

				tmp = tags[i] + 13;
				tmp[strlen (tmp) - 1] = '\0';

				tmp2 = g_convert (tmp, strlen (tmp), "UTF-8", "ISO-8859-1",
				                  &r, &w, NULL);

				xmms_medialib_entry_property_set (xmms_transport_medialib_entry_get (transport),
								  XMMS_MEDIALIB_ENTRY_PROPERTY_TITLE, tmp2);
				xmms_medialib_entry_send_update (data->pl_entry);
				g_free (tmp2);
			}
		}
		g_strfreev (tags);
		g_free (metadata);

		/* we want to read more data anyway, so fall thru here */
	}

cont:

	/* are we trying to read past metadata? */

	if (data->bytes_since_meta + len > data->meta_offset) {
		ret = xmms_ringbuf_read_wait (data->ringbuf, buffer,
		                         data->meta_offset - data->bytes_since_meta, data->mutex);
		data->bytes_since_meta += ret;

		g_mutex_unlock (data->mutex);
		return ret;
	}

	ret = xmms_ringbuf_read_wait (data->ringbuf, buffer, len, data->mutex);
	data->bytes_since_meta += ret;

	g_mutex_unlock (data->mutex);
	return ret;
}

static gint
xmms_curl_size (xmms_transport_t *transport)
{
	xmms_curl_data_t *data;

	g_return_val_if_fail (transport, -1);

	data = xmms_transport_private_data_get (transport);
	g_return_val_if_fail (data, -1);

	if (!data->know_length)
		return -1;

	return data->length;
}

static void
xmms_curl_close (xmms_transport_t *transport)
{
	xmms_curl_data_t *data;

	g_return_if_fail (transport);

	data = xmms_transport_private_data_get (transport);
	g_return_if_fail (data);

	data->running = FALSE;

	g_mutex_lock (data->mutex);
	xmms_ringbuf_set_eos (data->ringbuf, TRUE);
	g_mutex_unlock (data->mutex);

	XMMS_DBG ("Waiting for thread...");
	g_thread_join (data->thread);
	XMMS_DBG ("Thread is joined");

	curl_multi_cleanup (data->curl_multi);
	curl_easy_cleanup (data->curl_easy);

	XMMS_DBG ("CURL cleaned up");

	xmms_ringbuf_clear (data->ringbuf);
	xmms_ringbuf_destroy (data->ringbuf);

	g_mutex_free (data->mutex);

	curl_slist_free_all (data->http_aliases);
	curl_slist_free_all (data->http_headers);

	g_free (data->url);
	g_free (data);

	XMMS_DBG ("All done!");
}

/*
 * CURL callback functions
 */

static size_t
xmms_curl_callback_write (void *ptr, size_t size, size_t nmemb, void *stream)
{
	xmms_curl_data_t *data;
	xmms_transport_t *transport = (xmms_transport_t *) stream;
	gint ret;

	g_return_val_if_fail (transport, 0);

	data = xmms_transport_private_data_get (transport);
	g_return_val_if_fail (data, 0);

	g_mutex_lock (data->mutex);
	ret = xmms_ringbuf_write_wait (data->ringbuf, ptr, size * nmemb, data->mutex);
	g_mutex_unlock (data->mutex);

	return ret;
}

static size_t
xmms_curl_callback_header (void *ptr, size_t size, size_t nmemb, void *stream)
{
	xmms_transport_t *transport = (xmms_transport_t *) stream;
	handler_func_t func;
	gchar *header;

	g_return_val_if_fail (transport, -1);

	header = g_strndup ((gchar*)ptr, size * nmemb);

	func = header_handler_find (header);
	if (func != NULL) {
		gchar *val = header + strcspn (header, ":") + 1;

		g_strstrip (val);
		func (transport, val);
	}

	g_free (header);
	return size * nmemb;
}

/*
 * Our curl thread
 */

static void
xmms_curl_thread (xmms_transport_t *transport)
{
	xmms_curl_data_t *data;
	struct timeval timeout;

	g_return_if_fail (transport);

	data = xmms_transport_private_data_get (transport);
	g_return_if_fail (data);

	FD_ZERO (&data->fdread);
	FD_ZERO (&data->fdwrite);
	FD_ZERO (&data->fdexcp);

	while (curl_multi_perform (data->curl_multi, &data->running_handles) == CURLM_CALL_MULTI_PERFORM);

	XMMS_DBG ("xmms_curl_thread is now running!");

	g_mutex_lock (data->mutex);
	while (data->running && data->running_handles) {
		CURLMsg *msg;
		CURLMcode code;
		gint msgs_in_queue = 0;
		gint ret;

		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		curl_multi_fdset (data->curl_multi, &data->fdread, &data->fdwrite, &data->fdexcp,  &data->maxfd);
		g_mutex_unlock (data->mutex);
		if (!data->first_header) {
			ret = select (data->maxfd + 1,
			              &data->fdread,
			              &data->fdwrite,
			              &data->fdexcp,
			              &timeout);
		} else {
			ret = select (data->maxfd + 1,
			              &data->fdread,
			              NULL,
			              &data->fdexcp,
			              &timeout);
		}
		g_mutex_lock (data->mutex);

		if (ret == -1) {
			goto cont;
		}

		if (ret == 0) {
			continue;
		}

		while (42) {
			g_mutex_unlock (data->mutex);
			code = curl_multi_perform (data->curl_multi, &data->running_handles);
			g_mutex_lock (data->mutex);

			if (code == CURLM_OK) {
				break;
			}

			if (code != CURLM_CALL_MULTI_PERFORM) {
				XMMS_DBG ("%s", curl_multi_strerror (code));
				goto cont;
			}
		}

		if (!data->running_handles) {
			goto cont;
		}

		for (msgs_in_queue = 1; msgs_in_queue != 0;) {
			msg = curl_multi_info_read (data->curl_multi, &msgs_in_queue);

			if (msg && msg->msg == CURLMSG_DONE) {
				XMMS_DBG ("%s", curl_easy_strerror (msg->data.result));
				xmms_error_set (&data->status, XMMS_ERROR_EOS, "End of Stream");
				goto cont;
			}
		}

	}

cont:
	XMMS_DBG ("Curl thread quitting");

	if (!data->know_mime) {
		xmms_transport_mimetype_set (transport, NULL);
	}

	data->running = FALSE;
	xmms_ringbuf_set_eos (data->ringbuf, TRUE);
	g_mutex_unlock (data->mutex);
}

static handler_func_t
header_handler_find (gchar *header)
{
	guint i;

	g_return_val_if_fail (header, NULL);

	for (i = 0; handlers[i].name != NULL; i++) {
		guint len = strlen (handlers[i].name);

		if (g_ascii_strncasecmp (handlers[i].name, header, len) == 0)
			return handlers[i].func;
	}

	return NULL;
}

static void
header_handler_contenttype (xmms_transport_t *transport, gchar *header)
{
	xmms_curl_data_t *data;

	data = xmms_transport_private_data_get (transport);

	g_mutex_lock (data->mutex);
	data->know_mime = TRUE;
	g_mutex_unlock (data->mutex);

	xmms_transport_mimetype_set (transport, header);
}

static void
header_handler_contentlength (xmms_transport_t *transport, gchar *header)
{
	xmms_curl_data_t *data;

	data = xmms_transport_private_data_get (transport);

	g_mutex_lock (data->mutex);
	data->length = strtoul (header, NULL, 10);
	g_mutex_unlock (data->mutex);
}

static void
header_handler_icy_metaint (xmms_transport_t *transport, gchar *header)
{
	xmms_curl_data_t *data;

	data = xmms_transport_private_data_get (transport);

	g_mutex_lock (data->mutex);
	data->know_meta_offset = TRUE;
	data->meta_offset = strtoul (header, NULL, 10);
	g_mutex_unlock (data->mutex);
}

static void
header_handler_icy_name (xmms_transport_t *transport, gchar *header)
{
	xmms_medialib_entry_t entry;

	entry = xmms_transport_medialib_entry_get (transport);
	xmms_medialib_entry_property_set (entry, XMMS_MEDIALIB_ENTRY_PROPERTY_CHANNEL, header);
	xmms_medialib_entry_send_update (entry);
}

static void
header_handler_icy_br (xmms_transport_t *transport, gchar *header)
{
	xmms_medialib_entry_t entry;

	entry = xmms_transport_medialib_entry_get (transport);
	xmms_medialib_entry_property_set (entry, XMMS_MEDIALIB_ENTRY_PROPERTY_BITRATE, header);
	xmms_medialib_entry_send_update (entry);
}

static void
header_handler_icy_genre (xmms_transport_t *transport, gchar *header)
{
	xmms_medialib_entry_t entry;

	entry = xmms_transport_medialib_entry_get (transport);
	xmms_medialib_entry_property_set (entry, XMMS_MEDIALIB_ENTRY_PROPERTY_GENRE, header);
	xmms_medialib_entry_send_update (entry);
}

static void
header_handler_last (xmms_transport_t *transport, gchar *header)
{
	xmms_curl_data_t *data;
	gchar *mime = NULL;

	data = xmms_transport_private_data_get (transport);

	g_mutex_lock (data->mutex);

	if (data->know_meta_offset)
		mime = "audio/mpeg";

	if (!data->know_mime)
		xmms_transport_mimetype_set (transport, mime);

	data->know_mime = TRUE;
	data->first_header = TRUE;
	g_mutex_unlock (data->mutex);
}

