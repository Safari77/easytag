/* EasyTAG - Tag editor for audio files
 * Copyright (C) 2014-2015  David King <amigadave@amigadave.com>
 * Copyright (C) 2000-2003  Jerome Couderc <easytag@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include <errno.h>
#include <fcntl.h>              /* AT_FDCWD */
#include <sys/types.h>
#include <sys/stat.h>           /* struct stat, umask(), g_mkstemp modes */
#include <unistd.h>             /* syscall() prototype */
#include <string.h>             /* strrchr() */

#ifdef __linux__
#  include <sys/syscall.h>
#  include <linux/fs.h>
   /* RENAME_NOREPLACE exists since Linux 3.15; define it for older headers. */
#  ifndef RENAME_NOREPLACE
#    define RENAME_NOREPLACE (1U << 0)
#  endif
#endif

#include <glib/gstdio.h>        /* g_lstat, g_rename, g_unlink, g_mkstemp, g_close */

#include "misc.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "easytag.h"
#include "id3_tag.h"
#include "browser.h"
#include "setting.h"
#include "preferences_dialog.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif /* G_OS_WIN32 */


/*
 * Add the 'string' passed in parameter to the list store
 * If this string already exists in the list store, it doesn't add it.
 * Returns TRUE if string was added.
 */
gboolean Add_String_To_Combo_List (GtkListStore *liststore, const gchar *str)
{
    GtkTreeIter iter;
    gchar *text;
    const gint HISTORY_MAX_LENGTH = 15;
    //gboolean found = FALSE;
    gchar *string = g_strdup(str);

    if (et_str_empty (string))
    {
        g_free (string);
        return FALSE;
    }

#if 0
    // We add the string to the beginning of the list store
    // So we will start to parse from the second line below
    gtk_list_store_prepend(liststore, &iter);
    gtk_list_store_set(liststore, &iter, MISC_COMBO_TEXT, string, -1);

    // Search in the list store if string already exists and remove other same strings in the list
    found = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(liststore), &iter);
    //gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MISC_COMBO_TEXT, &text, -1);
    while (found && gtk_tree_model_iter_next(GTK_TREE_MODEL(liststore), &iter))
    {
        gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MISC_COMBO_TEXT, &text, -1);
        //g_print(">0>%s\n>1>%s\n",string,text);
        if (g_utf8_collate(text, string) == 0)
        {
            g_free(text);
            // FIX ME : it seems that after it selects the next item for the
            // combo (changes 'string')????
            // So should select the first item?
            gtk_list_store_remove(liststore, &iter);
            // Must be rewinded?
            found = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(liststore), &iter);
            //gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MISC_COMBO_TEXT, &text, -1);
            continue;
        }
        g_free(text);
    }

    // Limit list size to HISTORY_MAX_LENGTH
    while (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(liststore),NULL) > HISTORY_MAX_LENGTH)
    {
        if ( gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore),
                                           &iter,NULL,HISTORY_MAX_LENGTH) )
        {
            gtk_list_store_remove(liststore, &iter);
        }
    }

    g_free(string);
    // Place again to the beginning of the list, to select the right value?
    //gtk_tree_model_get_iter_first(GTK_TREE_MODEL(liststore), &iter);

    return TRUE;

#else

    // Search in the list store if string already exists.
    // FIXME : insert string at the beginning of the list (if already exists),
    //         and remove other same strings in the list
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(liststore), &iter))
    {
        do
        {
            gtk_tree_model_get(GTK_TREE_MODEL(liststore), &iter, MISC_COMBO_TEXT, &text, -1);
            if (g_utf8_collate(text, string) == 0)
            {
                g_free (string);
                g_free(text);
                return FALSE;
            }

            g_free(text);
        } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(liststore), &iter));
    }

    /* We add the string to the beginning of the list store. */
    gtk_list_store_insert_with_values (liststore, &iter, 0, MISC_COMBO_TEXT,
                                       string, -1);

    // Limit list size to HISTORY_MAX_LENGTH
    while (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(liststore),NULL) > HISTORY_MAX_LENGTH)
    {
        if ( gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(liststore),
                                           &iter,NULL,HISTORY_MAX_LENGTH) )
        {
            gtk_list_store_remove(liststore, &iter);
        }
    }

    g_free(string);
    return TRUE;
#endif
}

/*
 * Run a program with a list of parameters
 *  - args_list : list of filename (with path)
 */
gboolean
et_run_program (const gchar *program_name,
                GList *args_list,
                GError **error)
{
    gchar *program_tmp;
    const gchar *program_args;
    gchar **program_args_argv = NULL;
    guint n_program_args = 0;
    gsize i;
    gchar **argv;
    GSubprocess *subprocess;
    GList *l;
    gchar *program_path;
    gboolean res = FALSE;

    g_return_val_if_fail (program_name != NULL && args_list != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Check if a name for the program has been supplied */
    if (!*program_name)
    {
        GtkWidget *msgdialog;

        msgdialog = gtk_message_dialog_new(GTK_WINDOW(MainWindow),
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_OK,
                                           "%s",
                                           _("You must type a program name"));
        gtk_window_set_title(GTK_WINDOW(msgdialog),_("Program Name Error"));

        gtk_dialog_run(GTK_DIALOG(msgdialog));
        gtk_widget_destroy(msgdialog);
        return res;
    }

    /* If user arguments are included, try to skip them. FIXME: This works
     * poorly when there are spaces in the absolute path to the binary. */
    program_tmp = g_strdup (program_name);

    /* Skip the binary name and a delimiter. */
#ifdef G_OS_WIN32
    /* FIXME: Should also consider .com, .bat, .sys. See
     * g_find_program_in_path(). */
    if ((program_args = strstr (program_tmp, ".exe")))
    {
        /* Skip ".exe". */
        program_args += 4;
    }
#else /* !G_OS_WIN32 */
    /* Remove arguments if found. */
    program_args = strchr (program_tmp, ' ');
#endif /* !G_OS_WIN32 */

    if (program_args && *program_args)
    {
        size_t len;

        len = program_args - program_tmp;
        program_path = g_strndup (program_name, len);

        /* FIXME: Splitting arguments based on a delimiting space is bogus
         * if the arguments have been quoted. */
        program_args_argv = g_strsplit (program_args, " ", 0);
        n_program_args = g_strv_length (program_args_argv);
    }
    else
    {
        n_program_args = 1;
        program_path = g_strdup (program_name);
    }

    g_free (program_tmp);

    /* +1 for NULL, program_name is already included in n_program_args. */
    argv = g_new0 (gchar *, n_program_args + g_list_length (args_list) + 1);

    argv[0] = program_path;

    if (program_args_argv)
    {
        /* Skip program_args_argv[0], which is " ". */
        for (i = 1; program_args_argv[i] != NULL; i++)
        {
            argv[i] = program_args_argv[i];
        }
    }
    else
    {
        i = 1;
    }

    /* Load arguments from 'args_list'. */
    for (l = args_list; l != NULL; l = g_list_next (l), i++)
    {
        argv[i] = (gchar *)l->data;
    }

    argv[i] = NULL;

    /* Execution ... */
    if ((subprocess = g_subprocess_newv ((const gchar * const *)argv,
                                         G_SUBPROCESS_FLAGS_NONE, error)))
    {
        res = TRUE;
        /* There is no need to watch to see if the child process exited. */
        g_object_unref (subprocess);
    }

    g_strfreev (program_args_argv);
    g_free (program_path);
    g_free (argv);

    return res;
}

gboolean
et_run_audio_player (GList *files,
                     GError **error)
{
    GFileInfo *info;
    const gchar *content_type;
    GAppInfo *app_info;
    GdkAppLaunchContext *context;

    g_return_val_if_fail (files != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    info = g_file_query_info (files->data,
                              G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                              G_FILE_QUERY_INFO_NONE, NULL, error);

    if (info == NULL)
    {
        return FALSE;
    }

    content_type = g_file_info_get_content_type (info);
    app_info = g_app_info_get_default_for_type (content_type, FALSE);
    g_object_unref (info);

    context = gdk_display_get_app_launch_context (gdk_display_get_default ());

    if (!g_app_info_launch (app_info, files, G_APP_LAUNCH_CONTEXT (context),
                            error))
    {
        g_object_unref (context);
        g_object_unref (app_info);

        return FALSE;
    }

    g_object_unref (context);
    g_object_unref (app_info);

    return TRUE;
}

/*
 * Convert a series of seconds into a readable duration
 * Remember to free the string that is returned
 */
gchar *Convert_Duration (gulong duration)
{
    guint hour=0;
    guint minute=0;
    guint second=0;
    gchar *data = NULL;

    if (duration == 0)
    {
        return g_strdup_printf ("%u:%.2u", minute, second);
    }

    hour   = duration/3600;
    minute = (duration%3600)/60;
    second = (duration%3600)%60;

    if (hour)
    {
        data = g_strdup_printf ("%u:%.2u:%.2u", hour, minute, second);
    }
    else
    {
        data = g_strdup_printf ("%u:%.2u", minute, second);
    }

    return data;
}

gchar *
et_disc_number_to_string (const guint disc_number)
{
    if (g_settings_get_boolean (MainSettings, "tag-disc-padded"))
    {
        return g_strdup_printf ("%.*u",
                                (gint)g_settings_get_uint (MainSettings,
                                                           "tag-disc-length"),
                                disc_number);
    }

    return g_strdup_printf ("%u", disc_number);
}

gchar *
et_track_number_to_string (const guint track_number)
{
    if (g_settings_get_boolean (MainSettings, "tag-number-padded"))
    {
        return g_strdup_printf ("%.*u",
                                (gint)g_settings_get_uint (MainSettings,
                                                           "tag-number-length"),
                                track_number);
    }
    else
    {
        return g_strdup_printf ("%u", track_number);
    }
}

/*
 * et_lstat:
 * g_lstat() wrapper that retries if the call is interrupted by a signal.
 */
static gint
et_lstat (const gchar *path, struct stat *st)
{
    gint ret;

    do
    {
        ret = g_lstat (path, st);
    }
    while (ret == -1 && errno == EINTR);

    return ret;
}

/*
 * et_rename_file_try_atomic:
 *
 * Fast path: renameat2(RENAME_NOREPLACE) — atomic, never overwrites.
 *
 * Returns:  1  on success
 *           0  if the target already exists (EEXIST)
 *          -2  if the atomic rename is unsupported (non-Linux, kernel or
 *              libc without renameat2, filesystem rejecting the flags:
 *              old NFS / some FUSE / exFAT / FAT) — caller must fall back
 *          -1  on any other error, with errno set
 */
static gint
et_rename_file_try_atomic (const gchar *old_path, const gchar *new_path)
{
#if defined(__linux__) && defined(SYS_renameat2)
    long ret;

    do
    {
        ret = syscall (SYS_renameat2,
                       AT_FDCWD, old_path,
                       AT_FDCWD, new_path,
                       RENAME_NOREPLACE);
    }
    while (ret == -1L && errno == EINTR);

    if (ret == 0)
        return 1;

    if (errno == EEXIST)
        return 0;

    /* ENOSYS: no renameat2 in the kernel/libc.
     * EINVAL: filesystem does not support the RENAME_* flags.
     * Both mean: fall back to a regular GIO move. */
    if (errno == ENOSYS || errno == EINVAL)
        return -2;

    return -1;
#else
    return -2;
#endif
}

/*
 * et_rename_ensure_parent_dirs:
 * Create the directory that will contain @filepath, including all missing
 * parents. Scanner rename masks (e.g. "%artist/%album/%title.mp3") may
 * point into not yet existing directories; creating them up-front also
 * keeps the renameat2 fast path usable in that case.
 */
static gboolean
et_rename_ensure_parent_dirs (const gchar *filepath, GError **error)
{
    GFile *file, *parent;
    gboolean result = TRUE;

    file = g_file_new_for_path (filepath);
    parent = g_file_get_parent (file);
    g_object_unref (file);

    if (parent == NULL)
        return TRUE; /* target is in the current directory */

    if (!g_file_make_directory_with_parents (parent, NULL, error))
    {
        if (g_error_matches (*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
            g_clear_error (error); /* already there: fine */
        else
            result = FALSE;
    }

    g_object_unref (parent);
    return result;
}

/*
 * TRUE if both paths resolve to the same file (same device + inode).
 * Catches case-only renames on case-insensitive filesystems
 * ("foo.mp3" -> "FOO.mp3"), which must not be treated as conflicts.
 */
static gboolean
et_paths_refer_to_same_file (const gchar *path_a, const gchar *path_b)
{
    struct stat st_a, st_b;

    if (et_lstat (path_a, &st_a) != 0)
        return FALSE;
    if (et_lstat (path_b, &st_b) != 0)
        return FALSE;

    return st_a.st_dev == st_b.st_dev && st_a.st_ino == st_b.st_ino;
}

/*
 * et_rename_case_only:
 * Rename a file onto itself when only the name's case differs (or the two
 * paths are links to the same file). Some filesystems refuse such a rename
 * directly, so go through a temporary name in the same directory and
 * restore the original name if the second step fails.
 */
static gboolean
et_rename_case_only (const gchar *old_path, const gchar *new_path,
                     GError **error)
{
    gchar *tmp_path;
    mode_t old_mask;
    gint fd;

    tmp_path = g_strconcat (old_path, ".XXXXXX", NULL);

    old_mask = umask (077);
    fd = g_mkstemp (tmp_path);
    umask (old_mask);

    if (fd < 0)
    {
        gint saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     _("Cannot create temporary name to rename '%s': %s"),
                     old_path, g_strerror (saved_errno));
        g_free (tmp_path);
        return FALSE;
    }

    /* We only wanted the unique name, not the file. */
    g_close (fd, NULL);
    g_unlink (tmp_path);

    if (g_rename (old_path, tmp_path) != 0)
    {
        gint saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     _("Cannot rename '%s' to '%s': %s"),
                     old_path, tmp_path, g_strerror (saved_errno));
        g_unlink (tmp_path);
        g_free (tmp_path);
        return FALSE;
    }

    if (g_rename (tmp_path, new_path) != 0)
    {
        gint saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     _("Cannot rename '%s' to '%s': %s"),
                     tmp_path, new_path, g_strerror (saved_errno));
        g_rename (tmp_path, old_path); /* best effort restore */
        g_free (tmp_path);
        return FALSE;
    }

    g_free (tmp_path);
    return TRUE;
}

/*
 * et_generate_auto_rename_path:
 * Build "<name>_<n><ext>" in the same directory, with the first n (1..99999)
 * whose path is verified not to exist via lstat.
 *
 * Returns a newly allocated free path, or NULL with @error set (fatal lstat
 * error, or the search space exhausted — in which case an *existing* path
 * is never returned).
 */
static gchar *
et_generate_auto_rename_path (const gchar *conflict_path, GError **error)
{
    gchar *dir, *basename, *extension, *name_no_ext;
    const gchar *dot;
    guint counter = 1;
    gchar *candidate = NULL;

    g_return_val_if_fail (conflict_path != NULL, NULL);

    dir = g_path_get_dirname (conflict_path);
    basename = g_path_get_basename (conflict_path);

    dot = strrchr (basename, '.');
    if (dot != NULL && dot != basename)
    {
        name_no_ext = g_strndup (basename, dot - basename);
        extension = g_strdup (dot);
    }
    else
    {
        name_no_ext = g_strdup (basename);
        extension = g_strdup ("");
    }

    while (counter <= 99999)
    {
        struct stat st;
        gint saved_errno;

        g_free (candidate);
        candidate = g_strdup_printf ("%s%s%s_%u%s",
                                     dir, G_DIR_SEPARATOR_S,
                                     name_no_ext, counter, extension);

        if (et_lstat (candidate, &st) == 0)
        {
            counter++;
            continue; /* exists, try next number */
        }

        saved_errno = errno;
        if (saved_errno == ENOENT)
            break; /* free name found */

        /* Fatal: EACCES, ENAMETOOLONG, EIO, ... */
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     _("Cannot generate auto-rename path for '%s': %s"),
                     conflict_path, g_strerror (saved_errno));
        g_clear_pointer (&candidate, g_free);
        goto out;
    }

    if (counter > 99999)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     _("Could not generate a unique filename based on "
                       "'%s' after 99999 attempts"),
                     conflict_path);
        g_clear_pointer (&candidate, g_free);
    }

out:
    g_free (dir);
    g_free (basename);
    g_free (name_no_ext);
    g_free (extension);
    return candidate;
}

/*
 * et_show_rename_conflict_dialog:
 *
 * The entry is pre-filled with the auto-rename suggestion: pressing Enter
 * accepts it, typing replaces it with a manual name.
 *
 * Returns ET_RENAME_CONFLICT_AUTO, ET_RENAME_CONFLICT_MANUAL
 * (*user_path_out set, caller frees), or GTK_RESPONSE_CANCEL.
 */
static gint
et_show_rename_conflict_dialog (GtkWindow   *parent,
                                const gchar *conflicting_path_utf8,
                                const gchar *suggested_auto_utf8,
                                gchar      **user_path_out)
{
    GtkWidget *dialog, *content_area, *entry, *auto_label;
    gchar *msg, *markup;
    gint response;

    g_return_val_if_fail (user_path_out != NULL, GTK_RESPONSE_CANCEL);
    *user_path_out = NULL;

    msg = g_strdup_printf (
        _("The file '%s' already exists.\nHow would you like to proceed?"),
        conflicting_path_utf8);

    dialog = gtk_message_dialog_new (parent,
                                     GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                     "%s", msg);
    g_free (msg);
    gtk_window_set_title (GTK_WINDOW (dialog), _("File Already Exists"));

    content_area = gtk_message_dialog_get_message_area (
        GTK_MESSAGE_DIALOG (dialog));

    auto_label = gtk_label_new (NULL);
    markup = g_markup_printf_escaped (_("Auto-rename to: <b>%s</b>"),
                                      suggested_auto_utf8);
    gtk_label_set_markup (GTK_LABEL (auto_label), markup);
    g_free (markup);
    gtk_label_set_line_wrap (GTK_LABEL (auto_label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (auto_label), 0.0);
    gtk_container_add (GTK_CONTAINER (content_area), auto_label);

    entry = gtk_entry_new ();
    gtk_entry_set_width_chars (GTK_ENTRY (entry), 60);
    gtk_entry_set_text (GTK_ENTRY (entry), suggested_auto_utf8);
    gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);
    gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
    gtk_container_add (GTK_CONTAINER (content_area), entry);

    gtk_dialog_add_button (GTK_DIALOG (dialog),
                           _("A_uto Rename"), ET_RENAME_CONFLICT_AUTO);
    gtk_dialog_add_button (GTK_DIALOG (dialog),
                           _("_Manual Name…"), ET_RENAME_CONFLICT_MANUAL);
    gtk_dialog_add_button (GTK_DIALOG (dialog),
                           _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                     ET_RENAME_CONFLICT_MANUAL);

    gtk_widget_show_all (dialog);

    response = gtk_dialog_run (GTK_DIALOG (dialog));

    if (response == ET_RENAME_CONFLICT_MANUAL)
    {
        const gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));

        if (text != NULL && *text != '\0')
            *user_path_out = g_strdup (text);
        else
            response = GTK_RESPONSE_CANCEL; /* empty name: treat as cancel */
    }

    gtk_widget_destroy (dialog);
    return response;
}

/* Explains why the conflict dialog reappeared after a manual entry. */
static void
et_show_rename_unchanged_message (GtkWindow *parent)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
        "%s", _("The filename is unchanged."));
    gtk_message_dialog_format_secondary_text (
        GTK_MESSAGE_DIALOG (dialog), "%s",
        _("Please choose a different name, or press Cancel to stop."));
    gtk_window_set_title (GTK_WINDOW (dialog), _("Rename File"));
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
}

/*
 * et_rename_attempt:
 * One rename attempt of @old_path to @new_path.
 *
 * Returns TRUE on success.
 * Returns FALSE with @target_exists TRUE if the destination exists
 * (conflict; no GError is set). Returns FALSE with @error set on real
 * failure.
 *
 * Uses the renameat2 fast path when available, and falls back to a GIO
 * move when renameat2 is unsupported or the move crosses devices (GIO
 * performs copy+delete in that case).
 */
static gboolean
et_rename_attempt (const gchar *old_path,
                   const gchar *new_path,
                   gboolean    *target_exists,
                   GError     **error)
{
    gint rc;

    *target_exists = FALSE;

    rc = et_rename_file_try_atomic (old_path, new_path);

    if (rc == 1)
        return TRUE;

    if (rc == 0)
    {
        *target_exists = TRUE;
        return FALSE;
    }

    /* Unsupported (rc == -2) or cross-device (rc == -1, errno == EXDEV). */
    if (rc == -2 || errno == EXDEV)
    {
        GFile *src, *dst;
        GError *move_error = NULL;
        gboolean ok;

        src = g_file_new_for_path (old_path);
        dst = g_file_new_for_path (new_path);

        ok = g_file_move (src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL,
                          &move_error);

        g_object_unref (src);
        g_object_unref (dst);

        if (ok)
            return TRUE;

        if (g_error_matches (move_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
            g_clear_error (&move_error);
            *target_exists = TRUE;
            return FALSE;
        }

        g_propagate_error (error, move_error);
        return FALSE;
    }

    {
        gint saved_errno = errno;
        g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                     _("Cannot rename '%s' to '%s': %s"),
                     old_path, new_path, g_strerror (saved_errno));
        return FALSE;
    }
}

/*
 * et_rename_file:
 * @old_filepath:    source path (filename encoding)
 * @new_filepath:    desired destination path (filename encoding)
 * @actual_new_path: (out) (optional): on success, the destination actually
 *                   used, which may differ from @new_filepath after
 *                   conflict resolution. Free with g_free().
 * @parent:          transient parent for the conflict dialog, or NULL for
 *                   non-interactive (batch) operation. With NULL, an
 *                   existing destination fails with G_IO_ERROR_EXISTS.
 * @error:           return location for a GError
 *
 * Behaviour:
 *  - missing destination directories are created first (all parents);
 *  - a destination naming the *same* file (e.g. differing only by case on
 *    a case-insensitive filesystem) never triggers the conflict dialog and
 *    is renamed directly via a temporary name;
 *  - an existing destination opens a conflict dialog (auto-rename, manual
 *    name, or cancel) when @parent is non-NULL;
 *  - cancelling the dialog fails with G_IO_ERROR_CANCELLED.
 *
 * Returns: TRUE on success, FALSE on failure with @error set.
 */
gboolean
et_rename_file (const gchar *old_filepath,
                const gchar *new_filepath,
                gchar      **actual_new_path,
                GtkWindow   *parent,
                GError     **error)
{
    gchar *working_path;
    gboolean result = FALSE;

    g_return_val_if_fail (old_filepath != NULL, FALSE);
    g_return_val_if_fail (new_filepath != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (actual_new_path != NULL)
        *actual_new_path = NULL;

    working_path = g_strdup (new_filepath);

    while (!result)
    {
        gboolean target_exists = FALSE;

        /* 1. Make sure the destination directory exists. */
        if (!et_rename_ensure_parent_dirs (working_path, error))
            break; /* fatal */

        /* 2. Same file (e.g. case-only change): rename directly, no
         *    NOREPLACE, no conflict dialog. */
        if (et_paths_refer_to_same_file (old_filepath, working_path))
        {
            if (!et_rename_case_only (old_filepath, working_path, error))
                break; /* fatal */
            result = TRUE;
            break;
        }

        /* 3. Attempt the rename. */
        if (et_rename_attempt (old_filepath, working_path, &target_exists,
                               error))
        {
            result = TRUE;
            break;
        }

        if (!target_exists)
            break; /* fatal: error set by et_rename_attempt() */

        if (parent == NULL)
        {
            /* Batch mode: no UI; report the conflict as a plain error. */
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                         _("Cannot rename '%s' to '%s': "
                           "target file already exists"),
                         old_filepath, working_path);
            break;
        }

        /* 4. Conflict: let the user resolve it. Every case either adopts a
         *    new target (loop retries through steps 1-3) or goes to
         *    cleanup with an error; fatal errors can never re-enter the
         *    loop with a stale GError set. */
        {
            gchar *auto_path, *auto_display, *conflict_display;
            gchar *user_path = NULL;
            gint resp;

            auto_path = et_generate_auto_rename_path (working_path, error);
            if (auto_path == NULL)
                break; /* fatal: error set by the generator */

            conflict_display = g_filename_display_name (working_path);
            auto_display = g_filename_display_name (auto_path);

            resp = et_show_rename_conflict_dialog (parent,
                                                   conflict_display,
                                                   auto_display,
                                                   &user_path);

            g_free (conflict_display);
            g_free (auto_display);

            switch (resp)
            {
                case ET_RENAME_CONFLICT_AUTO:
                    /* Adopt the generated name and retry. If it was taken
                     * in the meantime (rare race), the dialog reappears
                     * with a fresh suggestion. */
                    g_free (working_path);
                    working_path = auto_path; /* takes ownership */
                    break;

                case ET_RENAME_CONFLICT_MANUAL:
                {
                    gchar *user_path_sys = NULL;

                    if (user_path != NULL)
                        user_path_sys = g_filename_from_utf8 (user_path, -1,
                                                              NULL, NULL,
                                                              NULL);
                    if (user_path_sys == NULL && user_path != NULL)
                        user_path_sys = g_strdup (user_path);

                    g_free (user_path);

                    if (user_path_sys == NULL)
                    {
                        g_free (auto_path);
                        break; /* conversion failed: re-show the dialog */
                    }

                    /* Relative entry: resolve against the current target's
                     * directory. */
                    if (!g_path_is_absolute (user_path_sys))
                    {
                        gchar *dir = g_path_get_dirname (working_path);
                        gchar *full_path =
                            g_build_filename (dir, user_path_sys, NULL);
                        g_free (dir);
                        g_free (user_path_sys);
                        user_path_sys = full_path;
                    }

                    if (g_strcmp0 (user_path_sys, working_path) == 0)
                    {
                        /* Unchanged name: explain, then re-show. */
                        et_show_rename_unchanged_message (parent);
                        g_free (user_path_sys);
                        g_free (auto_path);
                        break;
                    }

                    g_free (working_path);
                    working_path = user_path_sys; /* takes ownership */
                    g_free (auto_path);
                    break;
                }

                case GTK_RESPONSE_CANCEL:
                case GTK_RESPONSE_DELETE_EVENT:
                default:
                    g_set_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                 _("Rename cancelled by user"));
                    g_free (user_path);
                    g_free (auto_path);
                    goto cleanup;
            }
        }
    }

cleanup:
    if (result && actual_new_path != NULL)
        *actual_new_path = g_strdup (working_path);

    g_free (working_path);

    /* GError conventions: set exactly on failure, never on success. */
    g_assert (result || error == NULL || *error != NULL);
    g_assert (!result || error == NULL || *error == NULL);

    return result;
}


/*
 * et_filename_prepare:
 * @filename_utf8: UTF8-encoded basename
 * @replace_illegal: whether to replace illegal characters in the file name
 *
 * Used to replace (in place) the illegal characters in the filename.
 */
void
et_filename_prepare (gchar *filename_utf8,
                     gboolean replace_illegal)
{
    gchar *character;

    g_return_if_fail (filename_utf8 != NULL);

    // Convert automatically the directory separator ('/' on LINUX and '\' on WIN32) to '-'.
    while ((character = strchr (filename_utf8, G_DIR_SEPARATOR)) != NULL)
    {
        *character = '-';
    }

#ifdef G_OS_WIN32
    /* Convert character '/' on WIN32 to '-'. May be converted to '\' after. */
    while ((character = strchr (filename_utf8, '/')) != NULL)
    {
        *character = '-';
    }
#endif /* G_OS_WIN32 */

    /* Convert other illegal characters on FAT32/16 filesystems and ISO9660 and
     * Joliet (CD-ROM filesystems). */
    if (replace_illegal)
    {
        size_t last;

        while ((character = strchr (filename_utf8, ':')) != NULL)
        {
            *character = '-';
        }
        while ((character = strchr (filename_utf8, '*')) != NULL)
        {
            *character = '+';
        }
        while ((character = strchr (filename_utf8, '?')) != NULL)
        {
            *character = '_';
        }
        while ((character = strchr (filename_utf8, '\"')) != NULL)
        {
            *character = '\'';
        }
        while ((character = strchr (filename_utf8, '<')) != NULL)
        {
            *character = '(';
        }
        while ((character = strchr (filename_utf8, '>')) != NULL)
        {
            *character = ')';
        }
        while ((character = strchr (filename_utf8, '|')) != NULL)
        {
            *character = '-';
        }

        /* FAT has additional restrictions on the last character of a filename.
         * https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247%28v=vs.85%29.aspx#naming_conventions */
        last = strlen (filename_utf8) - 1;

        if (filename_utf8[last] == ' ' || filename_utf8[last] == '.')
        {
            filename_utf8[last] = '_';
        }
    }
}

/* Key for Undo */
guint
et_undo_key_new (void)
{
    static guint ETUndoKey = 0;
    return ++ETUndoKey;
}

/*
 * et_normalized_strcmp0:
 * @str1: UTF-8 string, or %NULL
 * @str2: UTF-8 string to compare against, or %NULL
 *
 * Compare two UTF-8 strings, normalizing them before doing so, and return the
 * difference.
 *
 * Returns: an integer less than, equal to, or greater than zero, if str1 is <,
 * == or > than str2
 */
gint
et_normalized_strcmp0 (const gchar *str1,
                       const gchar *str2)
{
    gint result;
    gchar *normalized1;
    gchar *normalized2;

    /* Check for NULL, as it cannot be passed to g_utf8_normalize(). */
    if (!str1)
    {
        return -(str1 != str2);
    }

    if (!str2)
    {
        return str1 != str2;
    }

    normalized1 = g_utf8_normalize (str1, -1, G_NORMALIZE_DEFAULT);
    normalized2 = g_utf8_normalize (str2, -1, G_NORMALIZE_DEFAULT);

    result = g_strcmp0 (normalized1, normalized2);

    g_free (normalized1);
    g_free (normalized2);

    return result;
}

/*
 * et_normalized_strcasecmp0:
 * @str1: UTF-8 string, or %NULL
 * @str2: UTF-8 string to compare against, or %NULL
 *
 * Compare two UTF-8 strings, normalizing them before doing so, in a
 * case-insensitive manner.
 *
 * Returns: an integer less than, equal to, or greater than zero, if str1 is
 * less than, equal to or greater than str2
 */
gint
et_normalized_strcasecmp0 (const gchar *str1,
                           const gchar *str2)
{
    gint result;
    gchar *casefolded1;
    gchar *casefolded2;

    /* Check for NULL, as it cannot be passed to g_utf8_casefold(). */
    if (!str1)
    {
        return -(str1 != str2);
    }

    if (!str2)
    {
        return str1 != str2;
    }

    /* The strings are automatically normalized during casefolding. */
    casefolded1 = g_utf8_casefold (str1, -1);
    casefolded2 = g_utf8_casefold (str2, -1);

    result = g_utf8_collate (casefolded1, casefolded2);

    g_free (casefolded1);
    g_free (casefolded2);

    return result;
}

/*
 * et_str_empty:
 * @str: string to test for emptiness
 *
 * Test if @str is empty, in other words either %NULL or the empty string.
 *
 * Returns: %TRUE is @str is either %NULL or "", %FALSE otherwise
 */
gboolean
et_str_empty (const gchar *str)
{
    return !str || !str[0];
}
