/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003	Peter Alm, Tobias Rundstr�m, Anders Gustafsson
 * 
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
 * 
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *                   
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */





#include <glib.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "xmms/playlist.h"
#include "xmms/playlist_entry.h"
#include "xmms/util.h"
#include "xmms/signal_xmms.h"

/*
 * Type definitions.
 */

/** @defgroup PlaylistEntry PlaylistEntry
  * @ingroup Playlist
  * @{
  */
	
static void xmms_playlist_entry_free (xmms_playlist_entry_t *entry);

struct xmms_playlist_entry_St {
	gchar *url;
	gchar *mimetype;
	
	guint id;
	
	guint ref;

	GHashTable *properties;
};

/*
 * Public functions
 */

xmms_playlist_entry_t *
xmms_playlist_entry_new (gchar *url)
{
	xmms_playlist_entry_t *ret;

	ret = g_new0 (xmms_playlist_entry_t, 1);
	ret->url = g_strdup (url);
	ret->properties = g_hash_table_new (g_str_hash, g_str_equal);
	ret->id = 0;
	ret->ref = 1;

	return ret;
}

guint
xmms_playlist_entry_id_get (xmms_playlist_entry_t *entry)
{
	g_return_val_if_fail (entry, 0);
	return entry->id;
}

void
xmms_playlist_entry_id_set (xmms_playlist_entry_t *entry, guint id)
{
	g_return_if_fail (entry);
	entry->id = id;
}

static void
xmms_playlist_entry_clone_foreach (gpointer key, gpointer value, gpointer udata)
{
	xmms_playlist_entry_t *new = (xmms_playlist_entry_t *) udata;

	xmms_playlist_entry_property_set (new, (gchar *)key, (gchar *)value);
}

/** Make a copy of all properties in entry to new
  */

void
xmms_playlist_entry_property_copy (xmms_playlist_entry_t *entry, 
		                   xmms_playlist_entry_t *newentry)
{
	g_return_if_fail (entry);
	g_return_if_fail (newentry);

	g_hash_table_foreach (entry->properties, xmms_playlist_entry_clone_foreach, (gpointer) newentry);

}

/**
 * Associates a value/key pair with a entry.
 * 
 * It copies the value/key and you'll need to call #xmms_playlist_entry_free
 * to free all strings.
 */

void
xmms_playlist_entry_property_set (xmms_playlist_entry_t *entry, gchar *key, gchar *value)
{
	g_return_if_fail (entry);
	g_return_if_fail (key);
	g_return_if_fail (value);

	g_hash_table_insert (entry->properties, g_ascii_strdown (key, strlen (key)), g_strdup (value));
	
}

void
xmms_playlist_entry_url_set (xmms_playlist_entry_t *entry, gchar *url)
{
	g_return_if_fail (entry);
	g_return_if_fail (url);

	entry->url = g_strdup (url);
}

gchar *
xmms_playlist_entry_url_get (const xmms_playlist_entry_t *entry)
{
	g_return_val_if_fail (entry, NULL);

	return entry->url;
}

void
xmms_playlist_entry_property_foreach (xmms_playlist_entry_t *entry,
				      GHFunc func,
				      gpointer userdata)
{

	g_return_if_fail (entry);
	g_return_if_fail (func);

	g_hash_table_foreach (entry->properties, func, userdata);

}

gchar *
xmms_playlist_entry_property_get (const xmms_playlist_entry_t *entry, gchar *key)
{
	g_return_val_if_fail (entry, NULL);
	g_return_val_if_fail (key, NULL);

	return g_hash_table_lookup (entry->properties, key);
}

gint
xmms_playlist_entry_property_get_int (const xmms_playlist_entry_t *entry, gchar *key)
{
	gchar *t;
	g_return_val_if_fail (entry, -1);
	g_return_val_if_fail (key, -1);

	t = g_hash_table_lookup (entry->properties, key);

	if (t)
		return strtol (t, NULL, 10);
	else
		return 0;
}


static gboolean
xmms_playlist_entry_foreach_free (gpointer key, gpointer value, gpointer udata)
{
	g_free (key);
	g_free (value);
	return TRUE;
}

static void
xmms_playlist_entry_free (xmms_playlist_entry_t *entry)
{

	if (entry->url)
		g_free (entry->url);
	
	if (entry->mimetype)
		g_free (entry->mimetype);

	g_hash_table_foreach_remove (entry->properties, xmms_playlist_entry_foreach_free, NULL);
	g_hash_table_destroy (entry->properties);

	g_free (entry);
}

static gchar *wellknowns[] = {
	XMMS_PLAYLIST_ENTRY_PROPERTY_ARTIST,
	XMMS_PLAYLIST_ENTRY_PROPERTY_ALBUM,
	XMMS_PLAYLIST_ENTRY_PROPERTY_TITLE,
	XMMS_PLAYLIST_ENTRY_PROPERTY_YEAR,
	XMMS_PLAYLIST_ENTRY_PROPERTY_TRACKNR,
	XMMS_PLAYLIST_ENTRY_PROPERTY_GENRE,
	XMMS_PLAYLIST_ENTRY_PROPERTY_BITRATE,
	XMMS_PLAYLIST_ENTRY_PROPERTY_COMMENT,
	XMMS_PLAYLIST_ENTRY_PROPERTY_DURATION,
	XMMS_PLAYLIST_ENTRY_PROPERTY_CHANNEL,
	XMMS_PLAYLIST_ENTRY_PROPERTY_SAMPLERATE,
	NULL 
};

gboolean
xmms_playlist_entry_is_wellknown (gchar *property)
{
	gint i = 0;

	while (wellknowns[i]) {
		if (g_strcasecmp (wellknowns[i], property) == 0)
			return TRUE;

		i++;
	}

	return FALSE;
}

void
xmms_playlist_entry_mimetype_set (xmms_playlist_entry_t *entry, const gchar *mimetype)
{

	g_return_if_fail (entry);
	g_return_if_fail (mimetype);

	entry->mimetype = g_strdup (mimetype);

}

const gchar *
xmms_playlist_entry_mimetype_get (xmms_playlist_entry_t *entry)
{
	g_return_val_if_fail (entry, NULL);

	return entry->mimetype;
}


void
xmms_playlist_entry_ref (xmms_playlist_entry_t *entry)
{
	g_return_if_fail (entry);
	entry->ref ++;
	XMMS_DBG ("Entry refcount is %d", entry->ref);
}

void
xmms_playlist_entry_unref (xmms_playlist_entry_t *entry)
{

	g_return_if_fail (entry);

	entry->ref --;

	XMMS_DBG ("for %s refcount is %d", entry->url, entry->ref);

	if (entry->ref < 1) {
		/* free entry */

		XMMS_DBG("Freeing %s", entry->url);
		xmms_playlist_entry_free (entry);
	}

}

/**
 * Tell all clients that this entry has been changed
 */

void
xmms_playlist_entry_changed (xmms_playlist_t *playlist, xmms_playlist_entry_t *entry)
{
	xmms_object_method_arg_t *arg;

	g_return_if_fail (playlist);
	g_return_if_fail (entry);
	
	arg = xmms_object_arg_new (XMMS_OBJECT_METHOD_ARG_UINT32,
				   GUINT_TO_POINTER (entry->id));

	xmms_object_emit (XMMS_OBJECT (playlist), 
			  XMMS_SIGNAL_PLAYLIST_MEDIAINFO_ID, arg);

	g_free (arg);
}

/** @} */
