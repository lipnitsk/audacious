/*
 * ui_preferences.c
 * Copyright 2006-2012 William Pitcock, Tomasz Moń, Michael Färber, and
 *                     John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <string.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <libaudcore/hook.h>
#include <libaudgui/libaudgui-gtk.h>

#include "debug.h"
#include "i18n.h"
#include "misc.h"
#include "output.h"
#include "playlist.h"
#include "plugin.h"
#include "plugins.h"
#include "preferences.h"
#include "ui_preferences.h"

#ifdef USE_CHARDET
#include <libguess.h>
#endif

enum CategoryViewCols {
    CATEGORY_VIEW_COL_ICON,
    CATEGORY_VIEW_COL_NAME,
    CATEGORY_VIEW_COL_ID,
    CATEGORY_VIEW_N_COLS
};

typedef struct {
    const char *icon_path;
    const char *name;
} Category;

typedef struct {
    const char *name;
    const char *tag;
} TitleFieldTag;

static /* GtkWidget * */ void * prefswin = NULL;
static GtkWidget *category_treeview = NULL;
static GtkWidget *category_notebook = NULL;

/* prefswin widgets */
GtkWidget *titlestring_entry;

static Category categories[] = {
 {"audio.png", N_("Audio")},
 {"connectivity.png", N_("Network")},
 {"playlist.png", N_("Playlist")},
 {"info.png", N_("Song Info")},
 {"plugins.png", N_("Plugins")},
};

static int n_categories = ARRAY_LEN(categories);

static TitleFieldTag title_field_tags[] = {
    { N_("Artist")     , "${artist}" },
    { N_("Album")      , "${album}" },
    { N_("Title")      , "${title}" },
    { N_("Tracknumber"), "${track-number}" },
    { N_("Genre")      , "${genre}" },
    { N_("Filename")   , "${file-name}" },
    { N_("Filepath")   , "${file-path}" },
    { N_("Date")       , "${date}" },
    { N_("Year")       , "${year}" },
    { N_("Comment")    , "${comment}" },
    { N_("Codec")      , "${codec}" },
    { N_("Quality")    , "${quality}" },
};
static const unsigned int n_title_field_tags = ARRAY_LEN(title_field_tags);

#ifdef USE_CHARDET
static ComboBoxElements chardet_detector_presets[] = {
 {"", N_("None")},
 {GUESS_REGION_AR, N_("Arabic")},
 {GUESS_REGION_BL, N_("Baltic")},
 {GUESS_REGION_CN, N_("Chinese")},
 {GUESS_REGION_GR, N_("Greek")},
 {GUESS_REGION_HW, N_("Hebrew")},
 {GUESS_REGION_JP, N_("Japanese")},
 {GUESS_REGION_KR, N_("Korean")},
 {GUESS_REGION_PL, N_("Polish")},
 {GUESS_REGION_RU, N_("Russian")},
 {GUESS_REGION_TW, N_("Taiwanese")},
 {GUESS_REGION_TR, N_("Turkish")}};
#endif

static ComboBoxElements bitdepth_elements[] = {
    { GINT_TO_POINTER(16), "16" },
    { GINT_TO_POINTER(24), "24" },
    { GINT_TO_POINTER(32), "32" },
    {GINT_TO_POINTER (0), N_("Floating point")},
};

static void * create_output_plugin_box (void);
static void output_bit_depth_changed (void);

static PreferencesWidget audio_page_widgets[] = {
 {WIDGET_LABEL, N_("<b>Output Settings</b>")},
 {WIDGET_CUSTOM, .data = {.populate = create_output_plugin_box}},
 {WIDGET_COMBO_BOX, N_("Bit depth:"),
  .cfg_type = VALUE_INT, .cname = "output_bit_depth", .callback = output_bit_depth_changed,
  .data = {.combo = {bitdepth_elements, ARRAY_LEN (bitdepth_elements)}}},
 {WIDGET_SPIN_BTN, N_("Buffer size:"),
  .cfg_type = VALUE_INT, .cname = "output_buffer_size",
  .data = {.spin_btn = {100, 10000, 1000, N_("ms")}}},
 {WIDGET_CHK_BTN, N_("Soft clipping"),
  .cfg_type = VALUE_BOOLEAN, .cname = "soft_clipping"},
 {WIDGET_CHK_BTN, N_("Use software volume control (not recommended)"),
  .cfg_type = VALUE_BOOLEAN, .cname = "software_volume_control"},
 {WIDGET_LABEL, N_("<b>Replay Gain</b>")},
 {WIDGET_CHK_BTN, N_("Enable Replay Gain"),
  .cfg_type = VALUE_BOOLEAN, .cname = "enable_replay_gain"},
 {WIDGET_CHK_BTN, N_("Album mode"), .child = TRUE,
  .cfg_type = VALUE_BOOLEAN, .cname = "replay_gain_album"},
 {WIDGET_CHK_BTN, N_("Prevent clipping (recommended)"), .child = TRUE,
  .cfg_type = VALUE_BOOLEAN, .cname = "enable_clipping_prevention"},
 {WIDGET_LABEL, N_("<b>Adjust Levels</b>"), .child = TRUE},
 {WIDGET_SPIN_BTN, N_("Amplify all files:"), .child = TRUE,
  .cfg_type = VALUE_FLOAT, .cname = "replay_gain_preamp",
  .data = {.spin_btn = {-15, 15, 0.1, N_("dB")}}},
 {WIDGET_SPIN_BTN, N_("Amplify untagged files:"), .child = TRUE,
  .cfg_type = VALUE_FLOAT, .cname = "default_gain",
  .data = {.spin_btn = {-15, 15, 0.1, N_("dB")}}}};

static PreferencesWidget proxy_host_port_elements[] = {
 {WIDGET_ENTRY, N_("Proxy hostname:"), .cfg_type = VALUE_STRING, .cname = "proxy_host"},
 {WIDGET_ENTRY, N_("Proxy port:"), .cfg_type = VALUE_STRING, .cname = "proxy_port"}};

static PreferencesWidget proxy_auth_elements[] = {
 {WIDGET_ENTRY, N_("Proxy username:"), .cfg_type = VALUE_STRING, .cname = "proxy_user"},
 {WIDGET_ENTRY, N_("Proxy password:"), .cfg_type = VALUE_STRING, .cname = "proxy_pass",
  .data = {.entry = {.password = TRUE}}}};

static PreferencesWidget connectivity_page_widgets[] = {
    {WIDGET_LABEL, N_("<b>Proxy Configuration</b>"), NULL, NULL, NULL, FALSE},
    {WIDGET_CHK_BTN, N_("Enable proxy usage"), .cfg_type = VALUE_BOOLEAN, .cname = "use_proxy"},
    {WIDGET_TABLE, .child = TRUE, .data = {.table = {proxy_host_port_elements,
     ARRAY_LEN (proxy_host_port_elements)}}},
    {WIDGET_CHK_BTN, N_("Use authentication with proxy"),
     .cfg_type = VALUE_BOOLEAN, .cname = "use_proxy_auth"},
    {WIDGET_TABLE, .child = TRUE, .data = {.table = {proxy_auth_elements,
     ARRAY_LEN (proxy_auth_elements)}}}
};

static PreferencesWidget chardet_elements[] = {
#ifdef USE_CHARDET
 {WIDGET_COMBO_BOX, N_("Auto character encoding detector for:"),
  .cfg_type = VALUE_STRING, .cname = "chardet_detector", .child = TRUE,
  .data = {.combo = {chardet_detector_presets, ARRAY_LEN (chardet_detector_presets)}}},
#endif
 {WIDGET_ENTRY, N_("Fallback character encodings:"), .cfg_type = VALUE_STRING,
  .cname = "chardet_fallback", .child = TRUE}};

static PreferencesWidget playlist_page_widgets[] = {
    {WIDGET_LABEL, N_("<b>Behavior</b>"), NULL, NULL, NULL, FALSE},
    {WIDGET_CHK_BTN, N_("Continue playback on startup"),
     .cfg_type = VALUE_BOOLEAN, .cname = "resume_playback_on_startup"},
    {WIDGET_CHK_BTN, N_("Advance when the current song is deleted"),
     .cfg_type = VALUE_BOOLEAN, .cname = "advance_on_delete"},
    {WIDGET_CHK_BTN, N_("Clear the playlist when opening files"),
     .cfg_type = VALUE_BOOLEAN, .cname = "clear_playlist"},
    {WIDGET_CHK_BTN, N_("Open files in a temporary playlist"),
     .cfg_type = VALUE_BOOLEAN, .cname = "open_to_temporary"},
    {WIDGET_CHK_BTN, N_("Do not load metadata for songs until played"),
     .cfg_type = VALUE_BOOLEAN, .cname = "metadata_on_play",
     .callback = playlist_trigger_scan},
    {WIDGET_LABEL, N_("<b>Compatibility</b>"), NULL, NULL, NULL, FALSE},
    {WIDGET_CHK_BTN, N_("Interpret \\ (backward slash) as a folder delimiter"),
     .cfg_type = VALUE_BOOLEAN, .cname = "convert_backslash"},
    {WIDGET_TABLE, .data = {.table = {chardet_elements,
     ARRAY_LEN (chardet_elements)}}}
};

static PreferencesWidget song_info_page_widgets[] = {
 {WIDGET_LABEL, N_("<b>Album Art</b>")},
 {WIDGET_LABEL, N_("Search for images matching these words (comma-separated):")},
 {WIDGET_ENTRY, .cfg_type = VALUE_STRING, .cname = "cover_name_include"},
 {WIDGET_LABEL, N_("Exclude images matching these words (comma-separated):")},
 {WIDGET_ENTRY, .cfg_type = VALUE_STRING, .cname = "cover_name_exclude"},
 {WIDGET_CHK_BTN, N_("Search for images matching song file name"),
  .cfg_type = VALUE_BOOLEAN, .cname = "use_file_cover"},
 {WIDGET_CHK_BTN, N_("Search recursively"),
  .cfg_type = VALUE_BOOLEAN, .cname = "recurse_for_cover"},
 {WIDGET_SPIN_BTN, N_("Search depth:"), .child = TRUE,
  .cfg_type = VALUE_INT, .cname = "recurse_for_cover_depth",
  .data = {.spin_btn = {0, 100, 1}}},
 {WIDGET_LABEL, N_("<b>Popup Information</b>")},
 {WIDGET_CHK_BTN, N_("Show popup information"),
  .cfg_type = VALUE_BOOLEAN, .cname = "show_filepopup_for_tuple"},
 {WIDGET_SPIN_BTN, N_("Popup delay (tenths of a second):"), .child = TRUE,
  .cfg_type = VALUE_INT, .cname = "filepopup_delay",
  .data = {.spin_btn = {0, 100, 1}}},
 {WIDGET_CHK_BTN, N_("Show time scale for current song"), .child = TRUE,
  .cfg_type = VALUE_BOOLEAN, .cname = "filepopup_showprogressbar"}};

#define TITLESTRING_NPRESETS 6

static const char * const titlestring_presets[TITLESTRING_NPRESETS] = {
 "${title}",
 "${?artist:${artist} - }${title}",
 "${?artist:${artist} - }${?album:${album} - }${title}",
 "${?artist:${artist} - }${?album:${album} - }${?track-number:${track-number}. }${title}",
 "${?artist:${artist} }${?album:[ ${album} ] }${?artist:- }${?track-number:${track-number}. }${title}",
 "${?album:${album} - }${title}"};

static const char * const titlestring_preset_names[TITLESTRING_NPRESETS] = {
 N_("TITLE"),
 N_("ARTIST - TITLE"),
 N_("ARTIST - ALBUM - TITLE"),
 N_("ARTIST - ALBUM - TRACK. TITLE"),
 N_("ARTIST [ ALBUM ] - TRACK. TITLE"),
 N_("ALBUM - TITLE")};

static void
change_category(GtkNotebook * notebook,
                GtkTreeSelection * selection)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int index;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, CATEGORY_VIEW_COL_ID, &index, -1);
    gtk_notebook_set_current_page(notebook, index);
}

static void
titlestring_tag_menu_callback(GtkMenuItem * menuitem,
                              void * data)
{
    const char *separator = " - ";
    int item = GPOINTER_TO_INT(data);
    int pos;

    pos = gtk_editable_get_position(GTK_EDITABLE(titlestring_entry));

    /* insert separator as needed */
    if (gtk_entry_get_text(GTK_ENTRY(titlestring_entry))[0])
        gtk_editable_insert_text(GTK_EDITABLE(titlestring_entry), separator, -1, &pos);

    gtk_editable_insert_text(GTK_EDITABLE(titlestring_entry), _(title_field_tags[item].tag), -1, &pos);
    gtk_editable_set_position(GTK_EDITABLE(titlestring_entry), pos);
}

static void
on_titlestring_help_button_clicked(GtkButton * button,
                                   void * data)
{
    GtkMenu * menu = data;
    gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
}

static void update_titlestring_cbox (GtkComboBox * cbox, const char * format)
{
    int preset;
    for (preset = 0; preset < TITLESTRING_NPRESETS; preset ++)
    {
        if (! strcmp (titlestring_presets[preset], format))
            break;
    }

    if (gtk_combo_box_get_active (cbox) != preset)
        gtk_combo_box_set_active (cbox, preset);
}

static void on_titlestring_entry_changed (GtkEntry * entry, GtkComboBox * cbox)
{
    const char * format = gtk_entry_get_text (entry);
    set_str (NULL, "generic_title_format", format);
    update_titlestring_cbox (cbox, format);
    playlist_reformat_titles ();
}

static void on_titlestring_cbox_changed (GtkComboBox * cbox, GtkEntry * entry)
{
    int preset = gtk_combo_box_get_active (cbox);
    if (preset < TITLESTRING_NPRESETS)
        gtk_entry_set_text (entry, titlestring_presets[preset]);
}

static void fill_category_list (GtkTreeView * treeview, GtkNotebook * notebook)
{
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GdkPixbuf *img;
    int i;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Category"));
    gtk_tree_view_append_column(treeview, column);
    gtk_tree_view_column_set_spacing(column, 2);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer, "pixbuf", 0, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer, "text", 1, NULL);

    g_object_set ((GObject *) renderer, "wrap-width", 96, "wrap-mode",
     PANGO_WRAP_WORD_CHAR, NULL);

    store = gtk_list_store_new(CATEGORY_VIEW_N_COLS,
                               GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_INT);
    gtk_tree_view_set_model(treeview, GTK_TREE_MODEL(store));

    for (i = 0; i < n_categories; i ++)
    {
        char * path = g_strdup_printf ("%s/images/%s",
         get_path (AUD_PATH_DATA_DIR), categories[i].icon_path);
        img = gdk_pixbuf_new_from_file (path, NULL);
        g_free (path);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           CATEGORY_VIEW_COL_ICON, img,
                           CATEGORY_VIEW_COL_NAME,
                           gettext(categories[i].name), CATEGORY_VIEW_COL_ID,
                           i, -1);
        g_object_unref(img);
    }

    selection = gtk_tree_view_get_selection(treeview);

    g_signal_connect_swapped(selection, "changed",
                             G_CALLBACK(change_category), notebook);
}

static GtkWidget *
create_titlestring_tag_menu(void)
{
    GtkWidget *titlestring_tag_menu, *menu_item;
    unsigned int i;

    titlestring_tag_menu = gtk_menu_new();
    for(i = 0; i < n_title_field_tags; i++) {
        menu_item = gtk_menu_item_new_with_label(_(title_field_tags[i].name));
        gtk_menu_shell_append(GTK_MENU_SHELL(titlestring_tag_menu), menu_item);
        g_signal_connect(menu_item, "activate",
                         G_CALLBACK(titlestring_tag_menu_callback),
                         GINT_TO_POINTER(i));
    };
    gtk_widget_show_all(titlestring_tag_menu);

    return titlestring_tag_menu;
}

static void show_numbers_cb (GtkToggleButton * numbers, void * unused)
{
    set_bool (NULL, "show_numbers_in_pl", gtk_toggle_button_get_active (numbers));
    playlist_reformat_titles ();
    hook_call ("title change", NULL);
}

static void leading_zero_cb (GtkToggleButton * leading)
{
    set_bool (NULL, "leading_zero", gtk_toggle_button_get_active (leading));
    playlist_reformat_titles ();
    hook_call ("title change", NULL);
}

static void create_titlestring_widgets (GtkWidget * * cbox, GtkWidget * * entry)
{
    * cbox = gtk_combo_box_text_new ();
    for (int i = 0; i < TITLESTRING_NPRESETS; i ++)
        gtk_combo_box_text_append_text ((GtkComboBoxText *) * cbox, _(titlestring_preset_names[i]));
    gtk_combo_box_text_append_text ((GtkComboBoxText *) * cbox, _("Custom"));

    * entry = gtk_entry_new ();

    char * format = get_str (NULL, "generic_title_format");
    update_titlestring_cbox ((GtkComboBox *) * cbox, format);
    gtk_entry_set_text ((GtkEntry *) * entry, format);
    str_unref (format);

    g_signal_connect (* cbox, "changed", (GCallback) on_titlestring_cbox_changed, * entry);
    g_signal_connect (* entry, "changed", (GCallback) on_titlestring_entry_changed, * cbox);
}

static void
create_playlist_category(void)
{
    GtkWidget *vbox5;
    GtkWidget *alignment55;
    GtkWidget *label60;
    GtkWidget *alignment56;
    GtkWidget *grid6;
    GtkWidget *titlestring_help_button;
    GtkWidget *image1;
    GtkWidget *label62;
    GtkWidget *label61;
    GtkWidget *titlestring_tag_menu = create_titlestring_tag_menu();
    GtkWidget * numbers_alignment, * numbers;

    vbox5 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) category_notebook, vbox5);

    create_widgets(GTK_BOX(vbox5), playlist_page_widgets, ARRAY_LEN(playlist_page_widgets));

    alignment55 = gtk_alignment_new (0.5, 0.5, 1, 1);
    gtk_box_pack_start (GTK_BOX (vbox5), alignment55, FALSE, FALSE, 0);
    gtk_alignment_set_padding ((GtkAlignment *) alignment55, 12, 3, 0, 0);

    label60 = gtk_label_new (_("<b>Song Display</b>"));
    gtk_container_add (GTK_CONTAINER (alignment55), label60);
    gtk_label_set_use_markup (GTK_LABEL (label60), TRUE);
    gtk_misc_set_alignment (GTK_MISC (label60), 0, 0.5);

    numbers_alignment = gtk_alignment_new (0, 0, 0, 0);
    gtk_alignment_set_padding ((GtkAlignment *) numbers_alignment, 0, 0, 12, 0);
    gtk_box_pack_start ((GtkBox *) vbox5, numbers_alignment, 0, 0, 3);

    numbers = gtk_check_button_new_with_label (_("Show song numbers"));
    gtk_toggle_button_set_active ((GtkToggleButton *) numbers,
     get_bool (NULL, "show_numbers_in_pl"));
    g_signal_connect ((GObject *) numbers, "toggled", (GCallback)
     show_numbers_cb, 0);
    gtk_container_add ((GtkContainer *) numbers_alignment, numbers);

    numbers_alignment = gtk_alignment_new (0, 0, 0, 0);
    gtk_alignment_set_padding ((GtkAlignment *) numbers_alignment, 0, 0, 12, 0);
    gtk_box_pack_start ((GtkBox *) vbox5, numbers_alignment, 0, 0, 3);

    numbers = gtk_check_button_new_with_label (_("Show leading zeroes (02:00 "
     "instead of 2:00)"));
    gtk_toggle_button_set_active ((GtkToggleButton *) numbers, get_bool (NULL, "leading_zero"));
    g_signal_connect ((GObject *) numbers, "toggled", (GCallback)
     leading_zero_cb, 0);
    gtk_container_add ((GtkContainer *) numbers_alignment, numbers);

    alignment56 = gtk_alignment_new (0.5, 0.5, 1, 1);
    gtk_box_pack_start (GTK_BOX (vbox5), alignment56, FALSE, FALSE, 0);
    gtk_alignment_set_padding (GTK_ALIGNMENT (alignment56), 0, 0, 12, 0);

    grid6 = gtk_grid_new ();
    gtk_container_add (GTK_CONTAINER (alignment56), grid6);
    gtk_grid_set_row_spacing (GTK_GRID (grid6), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid6), 12);

    titlestring_help_button = gtk_button_new ();
    gtk_grid_attach (GTK_GRID (grid6), titlestring_help_button, 2, 1, 1, 1);

    gtk_widget_set_can_focus (titlestring_help_button, FALSE);
    gtk_widget_set_tooltip_text (titlestring_help_button, _("Show information about titlestring format"));
    gtk_button_set_relief (GTK_BUTTON (titlestring_help_button), GTK_RELIEF_HALF);
    gtk_button_set_focus_on_click (GTK_BUTTON (titlestring_help_button), FALSE);

    image1 = gtk_image_new_from_icon_name ("list-add", GTK_ICON_SIZE_BUTTON);
    gtk_container_add (GTK_CONTAINER (titlestring_help_button), image1);

    GtkWidget * titlestring_cbox;
    create_titlestring_widgets (& titlestring_cbox, & titlestring_entry);
    gtk_widget_set_hexpand (titlestring_cbox, TRUE);
    gtk_widget_set_hexpand (titlestring_entry, TRUE);
    gtk_grid_attach (GTK_GRID (grid6), titlestring_cbox, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid6), titlestring_entry, 1, 1, 1, 1);

    label62 = gtk_label_new (_("Custom string:"));
    gtk_grid_attach (GTK_GRID (grid6), label62, 0, 1, 1, 1);
    gtk_label_set_justify (GTK_LABEL (label62), GTK_JUSTIFY_RIGHT);
    gtk_misc_set_alignment (GTK_MISC (label62), 1, 0.5);

    label61 = gtk_label_new (_("Title format:"));
    gtk_grid_attach (GTK_GRID (grid6), label61, 0, 0, 1, 1);
    gtk_label_set_justify (GTK_LABEL (label61), GTK_JUSTIFY_RIGHT);
    gtk_misc_set_alignment (GTK_MISC (label61), 1, 0.5);

    g_signal_connect(titlestring_help_button, "clicked",
                     G_CALLBACK(on_titlestring_help_button_clicked),
                     titlestring_tag_menu);
}

static void create_song_info_category (void)
{
    GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add ((GtkContainer *) category_notebook, vbox);
    create_widgets ((GtkBox *) vbox, song_info_page_widgets,
     ARRAY_LEN (song_info_page_widgets));
}

static GtkWidget * output_config_button, * output_about_button;

static bool_t output_enum_cb (PluginHandle * plugin, GList * * list)
{
    * list = g_list_prepend (* list, plugin);
    return TRUE;
}

static GList * output_get_list (void)
{
    static GList * list = NULL;

    if (list == NULL)
    {
        plugin_for_each (PLUGIN_TYPE_OUTPUT, (PluginForEachFunc) output_enum_cb,
         & list);
        list = g_list_reverse (list);
    }

    return list;
}

static void output_combo_update (GtkComboBox * combo)
{
    PluginHandle * plugin = plugin_get_current (PLUGIN_TYPE_OUTPUT);
    gtk_combo_box_set_active (combo, g_list_index (output_get_list (), plugin));
    gtk_widget_set_sensitive (output_config_button, plugin_has_configure (plugin));
    gtk_widget_set_sensitive (output_about_button, plugin_has_about (plugin));
}

static void output_combo_changed (GtkComboBox * combo)
{
    PluginHandle * plugin = g_list_nth_data (output_get_list (),
     gtk_combo_box_get_active (combo));
    g_return_if_fail (plugin != NULL);

    plugin_enable (plugin, TRUE);
    output_combo_update (combo);
}

static void output_combo_fill (GtkComboBox * combo)
{
    for (GList * node = output_get_list (); node != NULL; node = node->next)
        gtk_combo_box_text_append_text ((GtkComboBoxText *) combo,
         plugin_get_name (node->data));
}

static void output_bit_depth_changed (void)
{
    output_reset (OUTPUT_RESET_SOFT);
}

static void output_do_config (void * unused)
{
    PluginHandle * plugin = output_plugin_get_current ();
    g_return_if_fail (plugin != NULL);
    plugin_do_configure (plugin);
}

static void output_do_about (void * unused)
{
    PluginHandle * plugin = output_plugin_get_current ();
    g_return_if_fail (plugin != NULL);
    plugin_do_about (plugin);
}

static void * create_output_plugin_box (void)
{
    GtkWidget * hbox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  6);
    gtk_box_pack_start ((GtkBox *) hbox1, gtk_label_new (_("Output plugin:")), FALSE, FALSE, 0);

    GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start ((GtkBox *) hbox1, vbox, FALSE, FALSE, 0);

    GtkWidget * hbox2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  6);
    gtk_box_pack_start ((GtkBox *) vbox, hbox2, FALSE, FALSE, 0);

    GtkWidget * output_plugin_cbox = gtk_combo_box_text_new ();
    gtk_box_pack_start ((GtkBox *) hbox2, output_plugin_cbox, FALSE, FALSE, 0);

    GtkWidget * hbox3 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  6);
    gtk_box_pack_start ((GtkBox *) vbox, hbox3, FALSE, FALSE, 0);

    output_config_button = audgui_button_new (_("_Settings"),
     "preferences-system", output_do_config, NULL);
    gtk_box_pack_start ((GtkBox *) hbox3, output_config_button, FALSE, FALSE, 0);

    output_about_button = audgui_button_new (_("_About"), "help-about", output_do_about, NULL);
    gtk_box_pack_start ((GtkBox *) hbox3, output_about_button, FALSE, FALSE, 0);

    output_combo_fill ((GtkComboBox *) output_plugin_cbox);
    output_combo_update ((GtkComboBox *) output_plugin_cbox);

    g_signal_connect (output_plugin_cbox, "changed", (GCallback) output_combo_changed, NULL);
    g_signal_connect (output_config_button, "clicked", (GCallback) output_do_config, NULL);
    g_signal_connect (output_about_button, "clicked", (GCallback) output_do_about, NULL);

    return hbox1;
}

static void create_audio_category (void)
{
    GtkWidget * audio_page_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    create_widgets ((GtkBox *) audio_page_vbox, audio_page_widgets, ARRAY_LEN (audio_page_widgets));
    gtk_container_add ((GtkContainer *) category_notebook, audio_page_vbox);
}

static void
create_connectivity_category(void)
{
    GtkWidget *connectivity_page_vbox;
    GtkWidget *vbox29;

    connectivity_page_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (category_notebook), connectivity_page_vbox);

    vbox29 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start (GTK_BOX (connectivity_page_vbox), vbox29, TRUE, TRUE, 0);

    create_widgets(GTK_BOX(vbox29), connectivity_page_widgets, ARRAY_LEN(connectivity_page_widgets));
}

static void create_plugin_category (void)
{
    GtkWidget * notebook = gtk_notebook_new ();
    gtk_container_add ((GtkContainer *) category_notebook, notebook);

    int types[] = {PLUGIN_TYPE_TRANSPORT, PLUGIN_TYPE_PLAYLIST,
     PLUGIN_TYPE_INPUT, PLUGIN_TYPE_EFFECT, PLUGIN_TYPE_VIS, PLUGIN_TYPE_GENERAL};
    const char * names[] = {N_("Transport"), N_("Playlist"), N_("Input"),
     N_("Effect"), N_("Visualization"), N_("General")};

    for (int i = 0; i < ARRAY_LEN (types); i ++)
        gtk_notebook_append_page ((GtkNotebook *) notebook, plugin_view_new
         (types[i]), gtk_label_new (_(names[i])));
}

static void destroy_cb (void)
{
    prefswin = NULL;
    category_treeview = NULL;
    category_notebook = NULL;
    titlestring_entry = NULL;
}

static void create_prefs_window (void)
{
    char *aud_version_string;

    GtkWidget *vbox;
    GtkWidget *hbox1;
    GtkWidget *scrolledwindow6;
    GtkWidget *hseparator1;
    GtkWidget *hbox4;
    GtkWidget *audversionlabel;
    GtkWidget *prefswin_button_box;
    GtkWidget *close;

    prefswin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_type_hint (GTK_WINDOW (prefswin), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_container_set_border_width (GTK_CONTAINER (prefswin), 12);
    gtk_window_set_title (GTK_WINDOW (prefswin), _("Audacious Settings"));
    gtk_window_set_position (GTK_WINDOW (prefswin), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size (GTK_WINDOW (prefswin), 680, 400);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (prefswin), vbox);

    hbox1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  8);
    gtk_box_pack_start (GTK_BOX (vbox), hbox1, TRUE, TRUE, 0);

    scrolledwindow6 = gtk_scrolled_window_new (NULL, NULL);
    gtk_box_pack_start (GTK_BOX (hbox1), scrolledwindow6, FALSE, FALSE, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow6), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow6), GTK_SHADOW_IN);

    category_treeview = gtk_tree_view_new ();
    gtk_container_add (GTK_CONTAINER (scrolledwindow6), category_treeview);
    gtk_widget_set_size_request (scrolledwindow6, 168, -1);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (category_treeview), FALSE);

    category_notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (hbox1), category_notebook, TRUE, TRUE, 0);

    gtk_widget_set_can_focus (category_notebook, FALSE);
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (category_notebook), FALSE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (category_notebook), FALSE);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (category_notebook), TRUE);

    create_audio_category();
    create_connectivity_category();
    create_playlist_category();
    create_song_info_category();
    create_plugin_category();

    hseparator1 = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start (GTK_BOX (vbox), hseparator1, FALSE, FALSE, 6);

    hbox4 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox4, FALSE, FALSE, 0);

    audversionlabel = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (hbox4), audversionlabel, FALSE, FALSE, 0);
    gtk_label_set_use_markup (GTK_LABEL (audversionlabel), TRUE);

    prefswin_button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start (GTK_BOX (hbox4), prefswin_button_box, TRUE, TRUE, 0);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (prefswin_button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing (GTK_BOX (prefswin_button_box), 6);

    close = audgui_button_new (_("_Close"), "window-close",
     (AudguiCallback) gtk_widget_destroy, prefswin);
    gtk_container_add (GTK_CONTAINER (prefswin_button_box), close);
    gtk_widget_set_can_default(close, TRUE);

    /* create category view */
    fill_category_list ((GtkTreeView *) category_treeview, (GtkNotebook *) category_notebook);

    /* audacious version label */

    aud_version_string = g_strdup_printf
     ("<span size='small'>%s (%s)</span>", "Audacious " VERSION, BUILDSTAMP);

    gtk_label_set_markup( GTK_LABEL(audversionlabel) , aud_version_string );
    g_free(aud_version_string);
    gtk_widget_show_all(vbox);

    g_signal_connect (prefswin, "destroy", (GCallback) destroy_cb, NULL);

    audgui_destroy_on_escape (prefswin);
}

void show_prefs_window (void)
{
    if (! prefswin)
        create_prefs_window ();

    gtk_window_present ((GtkWindow *) prefswin);
}

void hide_prefs_window (void)
{
    if (prefswin)
        gtk_widget_destroy (prefswin);
}
