/*
 * Initial main.c file generated by Glade. Edit as required.
 * Glade will not overwrite this file.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>

#include "interface.h"
#include "support.h"

#include <xmmsclient.h>

enum status_codes {
	STOP,
	PLAY
};

GtkWidget *playlistwin=NULL;
GtkWidget *mainwindow;
GHashTable *idtable;
xmmsc_connection_t *conn;
gint lasttime;
gint state;

static GdkPixbuf *
get_icon ()
{
	GtkWidget *image;
	image = gtk_image_new_from_file ("cdaudio_mount.png");
	return gtk_image_get_pixbuf(GTK_IMAGE(image));
}

void
fill_playlist ()
{
	GtkTreeModel *store;
	GtkTreeIter iter1;
	GList *node;
	GtkWidget *tree = lookup_widget (playlistwin, "treeview1");
	gint id = xmmsc_get_playing_id (conn);
	GList *list = xmmsc_playlist_list (conn);

	store = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

	if (!idtable)
		idtable = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (node = list; node; node = g_list_next (node)) {
		gchar *file;
		xmmsc_playlist_entry_t *entry = node->data;
	
		file = strrchr (entry->url, '/');
		
		gtk_list_store_append (GTK_LIST_STORE (store), &iter1);
		
		if (id == entry->id) {
			gtk_list_store_set (GTK_LIST_STORE (store), &iter1, 
					0, get_icon (), 
					1, file+1, 
					2, entry->id, 
					-1);
		} else {
			gtk_list_store_set (GTK_LIST_STORE (store), &iter1, 
					1, file+1, 
					2, entry->id, 
					-1);
		}
		g_hash_table_insert (idtable, GUINT_TO_POINTER (entry->id), 
				(gpointer) gtk_tree_row_reference_new (store, gtk_tree_model_get_path (GTK_TREE_MODEL (store), 
				&iter1)));
	}


}

void
setup_playlist ()
{
	GtkListStore *store;
	GtkTreeIter iter1;
	GtkCellRenderer *renderer, *renderer_img;
	GtkTreeViewColumn *column;
	GtkWidget *tree = lookup_widget (playlistwin, "treeview1");
	

	store = gtk_list_store_new (3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_UINT);

	gtk_tree_view_set_model (GTK_TREE_VIEW (tree), GTK_TREE_MODEL (store));
	g_object_unref (G_OBJECT (store));

	renderer = gtk_cell_renderer_text_new ();
	renderer_img = gtk_cell_renderer_pixbuf_new ();

	column = gtk_tree_view_column_new_with_attributes ("icon", renderer_img,
				"pixbuf", 0,
				NULL);
	
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	column = gtk_tree_view_column_new_with_attributes ("File", renderer,
				"text", 1,
				NULL);

	gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

	fill_playlist ();

}


static void
mediainfo (gint id)
{
	GHashTable *entry;
	GtkWidget *title = lookup_widget (mainwindow, "title");
	GtkWidget *range = lookup_widget (mainwindow, "hscale1");

	if (id > 0) {
		gchar *tmp;

		lasttime = 0;

		entry = xmmsc_playlist_get_mediainfo (conn, id);
		if (!entry)
			return;

		tmp = (gchar *)g_hash_table_lookup (entry, "duration");
		if (tmp)
			gtk_range_set_range (GTK_RANGE (range), 0, (gdouble) atoi (tmp));
		else
			gtk_range_set_range (GTK_RANGE (range), 0, 0);

		tmp = (gchar *)g_hash_table_lookup (entry, "title");
		if (tmp) {
			gtk_label_set_text (GTK_LABEL (title), tmp);
		} else {
			tmp = (gchar *)g_hash_table_lookup (entry, "uri");
			tmp = strrchr (tmp, '/');
			gtk_label_set_text (GTK_LABEL (title), tmp+1);
		}

		state = PLAY;

		xmmsc_playlist_entry_free (entry);
		
	} else {
		gtk_label_set_text (GTK_LABEL (title), "xmms2 - it really whips the GNUs ass");
		gtk_range_set_range (GTK_RANGE (range), 0, 0);
	}
}

static void
handle_playtime (void *userdata, void *arg)
{
	guint tme = GPOINTER_TO_UINT (arg) / 1000;
	GtkWidget *range = lookup_widget (mainwindow, "hscale1");

	if (state == STOP) {
		gtk_range_set_value (GTK_RANGE (range), 0);
		return;
	}

	if (tme > lasttime) {
		gtk_range_set_value (GTK_RANGE (range), (gdouble)(tme));
		lasttime = tme;
	}
}

static void
handle_information (void *userdata, void *arg)
{
}

static void
handle_disconnected (void *userdata, void *arg)
{
}

static void
handle_mediainfo (void *userdata, void *arg)
{
	gint id = GPOINTER_TO_UINT (arg);

	mediainfo (id);
}

static void
handle_playback_stopped (void *userdata, void *arg)
{
	state = STOP;
	mediainfo (0);
}

static void
handle_playlist_added (void *userdata, void *arg)
{
	GtkTreeModel *store;
	GtkWidget *tree;
	GHashTable *entry;
	GtkTreeIter iter1;
	gchar *file;
	guint * foo = arg;
	guint id, option;


	if (!playlistwin)
		return;

	id = foo[0];
	option = foo[1];

	tree = lookup_widget (playlistwin, "treeview1");
	if (!tree)
		return;
	store = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

	gtk_list_store_append (GTK_LIST_STORE (store), &iter1);

	entry = xmmsc_playlist_get_mediainfo (conn, id);

	file = g_hash_table_lookup (entry, "uri");
	file = g_strdup (strrchr (file, '/'));

	gtk_list_store_set (GTK_LIST_STORE (store), &iter1, 
			0, NULL,
			1, file+1,
			2, id,
			-1);

	if (!idtable)
		idtable = g_hash_table_new (g_direct_hash, g_direct_equal);

	g_hash_table_insert (idtable, GUINT_TO_POINTER (id), 
		(gpointer) gtk_tree_row_reference_new (GTK_TREE_MODEL (store), gtk_tree_model_get_path (GTK_TREE_MODEL (store), 
		&iter1)));
}

static void
handle_playlist_removed (void *userdata, void *arg)
{
	GtkTreeModel *store;
	GtkTreeIter itr;
	GtkTreePath *path;
	GtkTreeRowReference *ref;
	
	GtkWidget *tree;
	guint id = (guint)arg;

	if (!playlistwin)
		return;

	ref = g_hash_table_lookup (idtable, GUINT_TO_POINTER (id));

	if (!ref)
		return;
	
	path = gtk_tree_row_reference_get_path (ref);

	tree = lookup_widget (playlistwin, "treeview1");
	store = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));
	
	gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &itr, path);
	
	if (gtk_list_store_remove (GTK_LIST_STORE (store), &itr))
		g_hash_table_remove (idtable, GUINT_TO_POINTER (arg));


}

static void
handle_playlist_moved (void *userdata, void *arg)
{
}

static void
handle_playlist_jumped (void *userdata, void *arg)
{
}

static void
handle_playlist_shuffled (void *userdata, void *arg)
{
	GtkTreeModel *store;
	GtkWidget *tree;

	if (!playlistwin)
		return;

	tree = lookup_widget (playlistwin, "treeview1");
	store = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

	gtk_list_store_clear (GTK_LIST_STORE (store));
	g_hash_table_destroy (idtable);
	idtable = NULL;

	fill_playlist ();
		
}

static void
handle_playlist_cleared (void *userdata, void *arg)
{
	GtkTreeModel *store;
	GtkWidget *tree;


	if (!playlistwin)
		return;

	tree = lookup_widget (playlistwin, "treeview1");
	store = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));

	gtk_list_store_clear (GTK_LIST_STORE (store));

	g_hash_table_destroy (idtable);
	idtable = NULL;

}

static void
buttonset_sensitive (gboolean sens)
{
	GtkWidget *tmp;

	tmp = lookup_widget (mainwindow, "play");
	gtk_widget_set_sensitive (tmp, sens);
	tmp = lookup_widget (mainwindow, "stop");
	gtk_widget_set_sensitive (tmp, sens);
	tmp = lookup_widget (mainwindow, "next");
	gtk_widget_set_sensitive (tmp, sens);
	tmp = lookup_widget (mainwindow, "prev");
	gtk_widget_set_sensitive (tmp, sens);
	tmp = lookup_widget (mainwindow, "playlist");
	gtk_widget_set_sensitive (tmp, sens);
}

int
main (int argc, char *argv[])
{
	gtk_set_locale ();
	gtk_init (&argc, &argv);
	GtkWidget *title;

	add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");

	mainwindow = create_mainwindow ();

	title = lookup_widget (mainwindow, "title");
	
	gtk_widget_show (mainwindow);

	/* connect */
	conn = xmmsc_init ();
	state = STOP;

	if (!conn) {
		return 1;
	}

	if (!xmmsc_connect (conn)) {
		gtk_label_set_text (GTK_LABEL (title), "failed to connect!");
		buttonset_sensitive (FALSE);
	} else {
		/* set up xmmsclient callbacks */
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYTIME_CHANGED, handle_playtime, NULL);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_INFORMATION, handle_information, NULL);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_MEDIAINFO_CHANGED, handle_mediainfo, conn);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYBACK_STOPPED, handle_playback_stopped, conn);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_DISCONNECTED, handle_disconnected, conn);

		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_ADDED, handle_playlist_added, NULL);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_REMOVED, handle_playlist_removed, NULL);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_MOVED, handle_playlist_moved, conn);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_JUMPED, handle_playlist_jumped, conn);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_SHUFFLED, handle_playlist_shuffled, conn);
		xmmsc_set_callback (conn, XMMSC_CALLBACK_PLAYLIST_CLEARED, handle_playlist_cleared, conn);

		xmmsc_glib_setup_mainloop (conn, NULL);

		mediainfo (xmmsc_get_playing_id (conn));
	}

	gtk_main ();
	return 0;
}

