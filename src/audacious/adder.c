/*
 * adder.c
 * Copyright 2011 John Lindgren
 *
 * This file is part of Audacious.
 *
 * Audacious is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2 or version 3 of the License.
 *
 * Audacious is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Audacious. If not, see <http://www.gnu.org/licenses/>.
 *
 * The Audacious team does not consider modular code linking to Audacious or
 * using our public API to be a derived work.
 */

#include <dirent.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>

#include <gtk/gtk.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/hook.h>

#include "config.h"
#include "i18n.h"
#include "playback.h"
#include "playlist.h"
#include "plugins.h"
#include "main.h"
#include "misc.h"

typedef struct {
    gint playlist_id, at;
    gboolean play;
    struct index * filenames, * tuples;
    PlaylistFilterFunc filter;
    void * user;
} AddTask;

typedef struct {
    gint playlist_id, at;
    gboolean play;
    struct index * filenames, * tuples, * decoders;
} AddResult;

static GList * add_tasks = NULL;
static GList * add_results = NULL;
static gint current_playlist_id = -1;

static GMutex * mutex;
static GCond * cond;
static gboolean add_quit;
static pthread_t add_thread;
static gint add_source = 0;

static gint status_source = 0;
static gchar status_path[512];
static gint status_count;
static GtkWidget * status_window = NULL, * status_path_label,
 * status_count_label;

static gboolean status_cb (void * unused)
{
    if (! headless && ! status_window)
    {
        status_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_type_hint ((GtkWindow *) status_window,
         GDK_WINDOW_TYPE_HINT_DIALOG);
        gtk_window_set_title ((GtkWindow *) status_window, _("Searching ..."));
        gtk_window_set_resizable ((GtkWindow *) status_window, FALSE);
        gtk_container_set_border_width ((GtkContainer *) status_window, 6);

        GtkWidget * vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add ((GtkContainer *) status_window, vbox);

        status_path_label = gtk_label_new (NULL);
        gtk_widget_set_size_request (status_path_label, 320, -1);
        gtk_label_set_ellipsize ((GtkLabel *) status_path_label,
         PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_pack_start ((GtkBox *) vbox, status_path_label, FALSE, FALSE, 0);

        status_count_label = gtk_label_new (NULL);
        gtk_widget_set_size_request (status_count_label, 320, -1);
        gtk_box_pack_start ((GtkBox *) vbox, status_count_label, FALSE, FALSE, 0);

        gtk_widget_show_all (status_window);

        g_signal_connect (status_window, "destroy", (GCallback)
         gtk_widget_destroyed, & status_window);
    }

    g_mutex_lock (mutex);

    gchar scratch[128];
    snprintf (scratch, sizeof scratch, dngettext (PACKAGE, "%d file found",
     "%d files found", status_count), status_count);

    if (headless)
    {
        printf ("Searching, %s ...\r", scratch);
        fflush (stdout);
    }
    else
    {
        gtk_label_set_text ((GtkLabel *) status_path_label, status_path);
        gtk_label_set_text ((GtkLabel *) status_count_label, scratch);
    }

    g_mutex_unlock (mutex);
    return TRUE;
}

static void status_update (const gchar * filename, gint found)
{
    g_mutex_lock (mutex);

    snprintf (status_path, sizeof status_path, "%s", filename);
    status_count = found;

    if (! status_source)
        status_source = g_timeout_add (250, status_cb, NULL);

    g_mutex_unlock (mutex);
}

static void status_done_locked (void)
{
    if (status_source)
    {
        g_source_remove (status_source);
        status_source = 0;
    }

    if (headless)
        printf ("\n");
    else if (status_window)
        gtk_widget_destroy (status_window);
}

static void index_free_filenames (struct index * filenames)
{
    gint count = index_count (filenames);
    for (gint i = 0; i < count; i ++)
        str_unref (index_get (filenames, i));

    index_free (filenames);
}

static void index_free_tuples (struct index * tuples)
{
    gint count = index_count (tuples);
    for (gint i = 0; i < count; i ++)
    {
        Tuple * tuple = index_get (tuples, i);
        if (tuple)
            tuple_unref (tuple);
    }

    index_free (tuples);
}

static AddTask * add_task_new (gint playlist_id, gint at, gboolean play,
 struct index * filenames, struct index * tuples, PlaylistFilterFunc filter,
 void * user)
{
    AddTask * task = g_slice_new (AddTask);
    task->playlist_id = playlist_id;
    task->at = at;
    task->play = play;
    task->filenames = filenames;
    task->tuples = tuples;
    task->filter = filter;
    task->user = user;
    return task;
}

static void add_task_free (AddTask * task)
{
    if (task->filenames)
        index_free_filenames (task->filenames);
    if (task->tuples)
        index_free_tuples (task->tuples);

    g_slice_free (AddTask, task);
}

static AddResult * add_result_new (gint playlist_id, gint at, gboolean play)
{
    AddResult * result = g_slice_new (AddResult);
    result->playlist_id = playlist_id;
    result->at = at;
    result->play = play;
    result->filenames = index_new ();
    result->tuples = index_new ();
    result->decoders = index_new ();
    return result;
}

static void add_result_free (AddResult * result)
{
    if (result->filenames)
        index_free_filenames (result->filenames);
    if (result->tuples)
        index_free_tuples (result->tuples);
    if (result->decoders)
        index_free (result->decoders);

    g_slice_free (AddResult, result);
}

static void add_file (gchar * filename, Tuple * tuple, PluginHandle * decoder,
 PlaylistFilterFunc filter, void * user, AddResult * result, gboolean validate)
{
    g_return_if_fail (filename);
    if (filter && ! filter (filename, user))
    {
        str_unref (filename);
        return;
    }

    status_update (filename, index_count (result->filenames));

    if (! tuple && ! decoder)
    {
        decoder = file_find_decoder (filename, TRUE);
        if (validate && ! decoder)
        {
            str_unref (filename);
            return;
        }
    }

    if (! tuple && decoder && input_plugin_has_subtunes (decoder) && ! strchr
     (filename, '?'))
        tuple = file_read_tuple (filename, decoder);

    gint n_subtunes = tuple ? tuple_get_n_subtunes (tuple) : 0;

    if (n_subtunes)
    {
        for (gint sub = 0; sub < n_subtunes; sub ++)
        {
            gchar * subname = str_printf ("%s?%d", filename,
             tuple_get_nth_subtune (tuple, sub));
            add_file (subname, NULL, decoder, filter, user, result, FALSE);
        }

        str_unref (filename);
        tuple_unref (tuple);
        return;
    }

    index_append (result->filenames, filename);
    index_append (result->tuples, tuple);
    index_append (result->decoders, decoder);
}

static void add_folder (gchar * filename, PlaylistFilterFunc filter,
 void * user, AddResult * result)
{
    g_return_if_fail (filename);
    if (filter && ! filter (filename, user))
    {
        str_unref (filename);
        return;
    }

    status_update (filename, index_count (result->filenames));

    gchar * unix_name = uri_to_filename (filename);
    g_return_if_fail (unix_name);
    if (unix_name[strlen (unix_name) - 1] == '/')
        unix_name[strlen (unix_name) - 1] = 0;

    GList * files = NULL;
    DIR * folder = opendir (unix_name);
    if (! folder)
        goto FREE;

    struct dirent * entry;
    while ((entry = readdir (folder)))
    {
        if (entry->d_name[0] != '.')
            files = g_list_prepend (files, g_strdup_printf ("%s"
             G_DIR_SEPARATOR_S "%s", unix_name, entry->d_name));
    }

    closedir (folder);
    files = g_list_sort (files, (GCompareFunc) string_compare);

    while (files)
    {
        struct stat info;
        if (stat (files->data, & info) < 0)
            goto NEXT;

        if (S_ISREG (info.st_mode))
        {
            gchar * item_name = filename_to_uri (files->data);
            add_file (str_get (item_name), NULL, NULL, filter, user, result, TRUE);
            g_free (item_name);
        }
        else if (S_ISDIR (info.st_mode))
        {
            gchar * item_name = filename_to_uri (files->data);
            add_folder (str_get (item_name), filter, user, result);
            g_free (item_name);
        }

    NEXT:
        g_free (files->data);
        files = g_list_delete_link (files, files);
    }

FREE:
    str_unref (filename);
    g_free (unix_name);
}

static void add_playlist (gchar * filename, PlaylistFilterFunc filter,
 void * user, AddResult * result)
{
    g_return_if_fail (filename);
    if (filter && ! filter (filename, user))
    {
        str_unref (filename);
        return;
    }

    status_update (filename, index_count (result->filenames));

    gchar * title = NULL;
    struct index * filenames, * tuples;
    if (! playlist_load (filename, & title, & filenames, & tuples))
    {
        str_unref (filename);
        return;
    }

    gint count = index_count (filenames);
    for (gint i = 0; i < count; i ++)
        add_file (index_get (filenames, i), tuples ? index_get (tuples, i) :
         NULL, NULL, filter, user, result, FALSE);

    str_unref (filename);
    str_unref (title);
    index_free (filenames);
    if (tuples)
        index_free (tuples);
}

static void add_generic (gchar * filename, Tuple * tuple,
 PlaylistFilterFunc filter, void * user, AddResult * result)
{
    g_return_if_fail (filename);

    if (tuple)
        add_file (filename, tuple, NULL, filter, user, result, FALSE);
    else if (vfs_file_test (filename, G_FILE_TEST_IS_DIR))
        add_folder (filename, filter, user, result);
    else if (filename_is_playlist (filename))
        add_playlist (filename, filter, user, result);
    else
        add_file (filename, NULL, NULL, filter, user, result, FALSE);
}

static gboolean add_finish (void * unused)
{
    g_mutex_lock (mutex);

    while (add_results)
    {
        AddResult * result = add_results->data;
        add_results = g_list_delete_link (add_results, add_results);

        gint playlist = playlist_by_unique_id (result->playlist_id);
        if (playlist < 0) /* playlist deleted */
            goto FREE;

        gint count = playlist_entry_count (playlist);
        if (result->at < 0 || result->at > count)
            result->at = count;

        playlist_entry_insert_batch_raw (playlist, result->at,
         result->filenames, result->tuples, result->decoders);
        result->filenames = NULL;
        result->tuples = NULL;
        result->decoders = NULL;

        if (result->play && playlist_entry_count (playlist) > count)
        {
            playlist_set_playing (playlist);
            if (! get_bool (NULL, "shuffle"))
                playlist_set_position (playlist, result->at);

            playback_play (0, FALSE);
        }

    FREE:
        add_result_free (result);
    }

    if (add_source)
    {
        g_source_remove (add_source);
        add_source = 0;
    }

    if (! add_tasks)
        status_done_locked ();

    g_mutex_unlock (mutex);

    hook_call ("playlist add complete", NULL);
    return FALSE;
}

static void * add_worker (void * unused)
{
    g_mutex_lock (mutex);
    g_cond_broadcast (cond);

    while (! add_quit)
    {
        if (! add_tasks)
        {
            g_cond_wait (cond, mutex);
            continue;
        }

        AddTask * task = add_tasks->data;
        add_tasks = g_list_delete_link (add_tasks, add_tasks);

        current_playlist_id = task->playlist_id;
        g_mutex_unlock (mutex);

        AddResult * result = add_result_new (task->playlist_id, task->at,
         task->play);

        gint count = index_count (task->filenames);
        if (task->tuples)
            count = MIN (count, index_count (task->tuples));

        for (gint i = 0; i < count; i ++)
        {
            add_generic (index_get (task->filenames, i), task->tuples ?
             index_get (task->tuples, i) : NULL, task->filter, task->user,
             result);

            index_set (task->filenames, i, NULL);
            if (task->tuples)
                index_set (task->tuples, i, NULL);
        }

        add_task_free (task);

        g_mutex_lock (mutex);
        current_playlist_id = -1;

        add_results = g_list_append (add_results, result);

        if (! add_source)
            add_source = g_timeout_add (0, add_finish, NULL);
    }

    g_mutex_unlock (mutex);
    return NULL;
}

void adder_init (void)
{
    mutex = g_mutex_new ();
    cond = g_cond_new ();
    g_mutex_lock (mutex);
    add_quit = FALSE;
    pthread_create (& add_thread, NULL, add_worker, NULL);
    g_cond_wait (cond, mutex);
    g_mutex_unlock (mutex);
}

void adder_cleanup (void)
{
    g_mutex_lock (mutex);
    add_quit = TRUE;
    g_cond_broadcast (cond);
    g_mutex_unlock (mutex);
    pthread_join (add_thread, NULL);
    g_mutex_free (mutex);
    g_cond_free (cond);

    if (add_source)
    {
        g_source_remove (add_source);
        add_source = 0;
    }

    status_done_locked ();
}

void playlist_entry_insert (gint playlist, gint at, const gchar * filename,
 Tuple * tuple, gboolean play)
{
    struct index * filenames = index_new ();
    struct index * tuples = index_new ();
    index_append (filenames, str_get (filename));
    index_append (tuples, tuple);

    playlist_entry_insert_batch (playlist, at, filenames, tuples, play);
}

void playlist_entry_insert_batch (gint playlist, gint at,
 struct index * filenames, struct index * tuples, gboolean play)
{
    playlist_entry_insert_filtered (playlist, at, filenames, tuples, NULL, NULL, play);
}

void playlist_entry_insert_filtered (gint playlist, gint at,
 struct index * filenames, struct index * tuples, PlaylistFilterFunc filter,
 void * user, gboolean play)
{
    gint playlist_id = playlist_get_unique_id (playlist);
    g_return_if_fail (playlist_id >= 0);

    AddTask * task = add_task_new (playlist_id, at, play, filenames, tuples, filter, user);

    g_mutex_lock (mutex);
    add_tasks = g_list_append (add_tasks, task);
    g_cond_broadcast (cond);
    g_mutex_unlock (mutex);
}

gboolean playlist_add_in_progress (gint playlist)
{
    gint playlist_id = playlist_get_unique_id (playlist);
    g_return_val_if_fail (playlist_id >= 0, FALSE);

    g_mutex_lock (mutex);

    for (GList * node = add_tasks; node; node = node->next)
    {
        if (((AddTask *) node->data)->playlist_id == playlist_id)
            goto YES;
    }

    if (current_playlist_id == playlist_id)
        goto YES;

    for (GList * node = add_results; node; node = node->next)
    {
        if (((AddResult *) node->data)->playlist_id == playlist_id)
            goto YES;
    }

    g_mutex_unlock (mutex);
    return FALSE;

YES:
    g_mutex_unlock (mutex);
    return TRUE;
}
