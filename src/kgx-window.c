/* kgx-window.c
 *
 * Copyright 2019 Zander Brown
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:kgx-window
 * @title: KgxWindow
 * @short_description: Window
 * 
 * The main #GtkApplicationWindow that acts as the terminal
 */

#include <glib/gi18n.h>
#include <vte/vte.h>
#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include "rgba.h"
#include "fp-vte-util.h"

#include "kgx-config.h"
#include "kgx-window.h"
#include "kgx-application.h"
#include "kgx-search-box.h"
#include "kgx-process.h"

#define KGX_WINDOW_STYLE_ROOT "root"
#define KGX_WINDOW_STYLE_REMOTE "remote"

G_DEFINE_TYPE (KgxWindow, kgx_window, GTK_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_THEME,
  PROP_INITIAL_WORK_DIR,
  PROP_CLOSE_ON_ZERO,
  LAST_PROP
};

static GParamSpec *pspecs[LAST_PROP] = { NULL, };


void
kgx_window_set_theme (KgxWindow *self,
                      KgxTheme   theme)
{
  GdkRGBA fg;
  GdkRGBA bg = (GdkRGBA) { 0.1, 0.1, 0.1, 0.96};

  // Workings of GDK_RGBA prevent this being static
  GdkRGBA palette[16] = {
    GDK_RGBA ("241f31"), // Black
    GDK_RGBA ("c01c28"), // Red
    GDK_RGBA ("2ec27e"), // Green
    GDK_RGBA ("f5c211"), // Yellow
    GDK_RGBA ("1e78e4"), // Blue
    GDK_RGBA ("9841bb"), // Magenta
    GDK_RGBA ("0ab9dc"), // Cyan
    GDK_RGBA ("c0bfbc"), // White
    GDK_RGBA ("5e5c64"), // Bright Black
    GDK_RGBA ("ed333b"), // Bright Red
    GDK_RGBA ("57e389"), // Bright Green
    GDK_RGBA ("f8e45c"), // Bright Yellow
    GDK_RGBA ("51a1ff"), // Bright Blue
    GDK_RGBA ("c061cb"), // Bright Magenta
    GDK_RGBA ("4fd2fd"), // Bright Cyan
    GDK_RGBA ("f6f5f4"), // Bright White
  };

  self->theme = theme;

  switch (theme) {
    case KGX_THEME_HACKER:
      fg = (GdkRGBA) { 0.1, 1.0, 0.1, 1.0};
      break;
    case KGX_THEME_NIGHT:
    default:
      fg = (GdkRGBA) { 1.0, 1.0, 1.0, 1.0};
      break;
  }

  vte_terminal_set_colors (VTE_TERMINAL (self->terminal), &fg, &bg, palette, 16);

  g_object_notify_by_pspec (G_OBJECT (self), pspecs[PROP_THEME]);
}

static void
kgx_window_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  KgxWindow *self = KGX_WINDOW (object);

  switch (property_id) {
    case PROP_THEME:
      kgx_window_set_theme (self, g_value_get_enum (value));
      break;
    case PROP_INITIAL_WORK_DIR:
      self->working_dir = g_value_get_string (value);
      break;
    case PROP_CLOSE_ON_ZERO:
      self->close_on_zero = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
kgx_window_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  KgxWindow *self = KGX_WINDOW (object);

  switch (property_id) {
    case PROP_THEME:
      g_value_set_enum (value, self->theme);
      break;
    case PROP_CLOSE_ON_ZERO:
      g_value_set_boolean (value, self->close_on_zero);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
application_set (GObject *object, GParamSpec *pspec, gpointer data)
{
  GtkApplication *app;

  app = gtk_window_get_application (GTK_WINDOW (object));

  g_object_bind_property (app,
                          "theme",
                          object,
                          "theme",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  g_object_bind_property (app,
                          "font",
                          KGX_WINDOW (object)->terminal,
                          "font-desc",
                          G_BINDING_SYNC_CREATE);
}

static void
search_changed (KgxSearchBox *box,
                const gchar  *search,
                KgxWindow    *self)
{
  VteRegex *regex;
  GError *error = NULL;

  regex = vte_regex_new_for_search (g_regex_escape_string (search, -1),
                                    -1, PCRE2_MULTILINE, &error);

  if (error) {
    g_warning (_("Search error: %s"), error->message);
    return;
  }

  vte_terminal_search_set_regex (VTE_TERMINAL (self->terminal),
                                 regex, 0);
}

static void
search_next (KgxSearchBox *box,
             KgxWindow    *self)
{
  vte_terminal_search_find_next (VTE_TERMINAL (self->terminal));
}

static void
search_prev (KgxSearchBox *box,
             KgxWindow    *self)
{
  vte_terminal_search_find_previous (VTE_TERMINAL (self->terminal));
}

static void
font_increase (VteTerminal *term,
               KgxWindow   *self)
{
  GAction *action = NULL;

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "zoom-in");
  g_action_activate (action, NULL);
}

static void
font_decrease (VteTerminal *term,
               KgxWindow   *self)
{
  GAction *action = NULL;

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "zoom-out");
  g_action_activate (action, NULL);
}

static gboolean
size_timeout (KgxWindow *self)
{
  self->timeout = 0;

  gtk_widget_hide (self->dims);

  return G_SOURCE_REMOVE;
}

static void
size_changed (GtkWidget    *widget,
              GdkRectangle *allocation,
              KgxWindow    *self)
{
  char        *label;
  int          cols;
  int          rows;
  VteTerminal *term = VTE_TERMINAL (widget);

  cols = vte_terminal_get_column_count (term);
  rows = vte_terminal_get_row_count (term);

  if (self->timeout != 0) {
    g_source_remove (self->timeout);
  }
  self->timeout = g_timeout_add (800, G_SOURCE_FUNC (size_timeout), self);

  if (cols == self->last_cols && rows == self->last_rows)
    return;

  self->last_cols = cols;
  self->last_rows = rows;

  label = g_strdup_printf ("%i × %i", cols, rows);

  gtk_label_set_label (GTK_LABEL (self->dims), label);
  gtk_widget_show (self->dims);

  g_free (label);
}

static void
kgx_window_class_init (KgxWindowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = kgx_window_set_property;
  object_class->get_property = kgx_window_get_property;

  pspecs[PROP_THEME] =
    g_param_spec_enum ("theme", "Theme", "Terminal theme",
                       KGX_TYPE_THEME, KGX_THEME_HACKER,
                       G_PARAM_READWRITE);

  pspecs[PROP_INITIAL_WORK_DIR] =
    g_param_spec_string ("initial-work-dir", "Initial directory",
                         "Initial working directory",
                         NULL,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  pspecs[PROP_CLOSE_ON_ZERO] =
    g_param_spec_boolean ("close-on-zero", "Close on zero",
                          "Should close when child exits with 0",
                          TRUE,
                          G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, LAST_PROP, pspecs);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/zbrown/KingsCross/kgx-window.ui");

  gtk_widget_class_bind_template_child (widget_class, KgxWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, KgxWindow, terminal);
  gtk_widget_class_bind_template_child (widget_class, KgxWindow, dims);
  gtk_widget_class_bind_template_child (widget_class, KgxWindow, search_bar);
  gtk_widget_class_bind_template_child (widget_class, KgxWindow, exit_info);
  gtk_widget_class_bind_template_child (widget_class, KgxWindow, exit_message);

  gtk_widget_class_bind_template_callback (widget_class, application_set);

  gtk_widget_class_bind_template_callback (widget_class, search_changed);
  gtk_widget_class_bind_template_callback (widget_class, search_next);
  gtk_widget_class_bind_template_callback (widget_class, search_prev);

  gtk_widget_class_bind_template_callback (widget_class, font_increase);
  gtk_widget_class_bind_template_callback (widget_class, font_decrease);

  gtk_widget_class_bind_template_callback (widget_class, size_changed);
}

static void
new_activated (GSimpleAction *action,
               GVariant      *parameter,
               gpointer       data)
{
  GtkWindow       *window;
  GtkApplication  *app;
  guint32          timestamp;
  const char      *dir;

  /* Slightly "wrong" but hopefully by taking the time before
   * we spend non-zero time initing the window it's far enough in the
   * past for shell to do-the-right-thing
   */
  timestamp = GDK_CURRENT_TIME;

  app = gtk_window_get_application (GTK_WINDOW (data));

  dir = kgx_window_get_working_dir (KGX_WINDOW (data));

  window = g_object_new (KGX_TYPE_WINDOW,
                        "application", app,
                        "initial-work-dir", dir,
                        "close-on-zero", TRUE,
                        NULL);

  gtk_window_present_with_time (window, timestamp);
}

static void
about_activated (GSimpleAction *action,
                 GVariant      *parameter,
                 gpointer       data)
{
  const char *authors[] = { "Zander Brown<zbrown@gnome.org>", NULL };
  const char *artists[] = { "Tobias Bernard", NULL };
  g_autofree char *copyright = g_strdup_printf (_("Copyright © %s Zander Brown"),
                                                "2019");

  gtk_show_about_dialog (GTK_WINDOW (data),
                         "authors", authors,
                         "artists", artists,
                         "comments", _("Terminal Emulator"),
                         "copyright", copyright,
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "logo-icon-name", "org.gnome.zbrown.KingsCross",
                         "program-name", _("King's Cross"),
                         "version", PACKAGE_VERSION,
                         NULL);
}

static void
update_actions (KgxWindow *self)
{
  VteTerminal *term = VTE_TERMINAL (self->terminal);
  gdouble current = vte_terminal_get_font_scale (term);
  GAction *action;

  action = g_action_map_lookup_action (G_ACTION_MAP (self), "zoom-out");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), current > 0.5);
  action = g_action_map_lookup_action (G_ACTION_MAP (self), "zoom-normal");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), current != 1.0);
  action = g_action_map_lookup_action (G_ACTION_MAP (self), "zoom-in");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), current < 2.0);
}

static void
zoom_out_activated (GSimpleAction *action,
                    GVariant      *parameter,
                    gpointer       data)
{
  KgxWindow *self = KGX_WINDOW (data);
  VteTerminal *term = VTE_TERMINAL (self->terminal);
  gdouble current = vte_terminal_get_font_scale (term);

  vte_terminal_set_font_scale (term, current - 0.1);

  update_actions (self);
}

static void
zoom_normal_activated (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       data)
{
  KgxWindow *self = KGX_WINDOW (data);
  VteTerminal *term = VTE_TERMINAL (self->terminal);

  vte_terminal_set_font_scale (term, 1.0);

  update_actions (self);
}

static void
zoom_in_activated (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer       data)
{
  KgxWindow *self = KGX_WINDOW (data);
  VteTerminal *term = VTE_TERMINAL (self->terminal);
  gdouble current = vte_terminal_get_font_scale (term);

  vte_terminal_set_font_scale (term, current + 0.1);

  update_actions (self);
}

static GActionEntry win_entries[] =
{
  { "new-window", new_activated, NULL, NULL, NULL },
  { "about", about_activated, NULL, NULL, NULL },
  { "zoom-out", zoom_out_activated, NULL, NULL, NULL },
  { "zoom-normal", zoom_normal_activated, NULL, NULL, NULL },
  { "zoom-in", zoom_in_activated, NULL, NULL, NULL },
};

static gboolean
update_subtitle (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      data)
{
  g_autoptr (GFile) file = NULL;
  g_autofree char *path = NULL;
  const char *uri;
  const char *home;

  uri = g_value_get_string (from_value);
  if (uri == NULL) {
    g_value_set_string (to_value, NULL);
    return TRUE;
  }

  file = g_file_new_for_uri (uri);

  path = g_file_get_path (file);
  if (path == NULL) {
    g_value_set_string (to_value, NULL);

    return TRUE;
  }

  home = g_get_home_dir ();
  if (g_str_has_prefix (path, home)) {
    home = g_strdup_printf ("~%s", path + strlen (home));

    g_value_set_string (to_value, home);

    return TRUE;
  }

  g_value_set_string (to_value, path);

  return TRUE;
}

static void
wait_cb (G_GNUC_UNUSED GPid pid,
         gint               status,
         gpointer           user_data)

{
  KgxWindow *self = KGX_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  GtkStyleContext *context = NULL;

  context = gtk_widget_get_style_context (GTK_WIDGET (self->exit_info));

  /* wait_check will set @error if it got a signal/non-zero exit */
  if (!g_spawn_check_exit_status (status, &error)) {
    g_autofree char *message = NULL;

    // translators: <b> </b> marks the text as bold, ensure they are matched please!
    message = g_strdup_printf (_("<b>Read Only</b> — Command exited with code %i"), status);

    gtk_label_set_markup (GTK_LABEL (self->exit_message), message);
    gtk_style_context_add_class (context, "error");
  } else if (self->close_on_zero) {
    gtk_widget_destroy (GTK_WIDGET (self));

    return;
  } else {
    gtk_label_set_markup (GTK_LABEL (self->exit_message),
    // translators: <b> </b> marks the text as bold, ensure they are matched please!
                         _("<b>Read Only</b> — Command exited"));
    gtk_style_context_remove_class (context, "error");
  }

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->exit_info), TRUE);
}

static void
spawned (VtePty       *pty,
         GAsyncResult *res,
         gpointer      data)

{
  g_autoptr(GError) error = NULL;
  KgxWindow *self = KGX_WINDOW (data);
  GPid pid;

  g_return_if_fail (VTE_IS_PTY (pty));
  g_return_if_fail (G_IS_ASYNC_RESULT (res));

  fp_vte_pty_spawn_finish (pty, res, &pid, &error);

  if (error) {
    g_autofree char *message = NULL;

    g_critical (_("Failed to spawn shell: %s"), error->message);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self->exit_info)),
                                 "error");

    // translators: <b> </b> marks the text as bold, ensure they are matched please!
    message = g_strdup_printf (_("<b>Failed to start</b> — %s"), error->message);
   
    gtk_label_set_markup (GTK_LABEL (self->exit_message), message);

    gtk_revealer_set_reveal_child (GTK_REVEALER (self->exit_info), TRUE);

    return;
  }

  #if HAS_GTOP
  kgx_application_add_watch (KGX_APPLICATION (gtk_window_get_application (GTK_WINDOW (self))),
                             kgx_process_new (pid),
                             KGX_WINDOW (self));
  #endif

  g_child_watch_add (pid, wait_cb, self);
}

static void
kgx_window_init (KgxWindow *self)
{
  GPropertyAction *pact;
  gchar           *shell[2] = {NULL, NULL};
  const char      *initial = NULL;
  VtePty          *pty;
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) env = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_entries,
                                   G_N_ELEMENTS (win_entries),
                                   self);

  update_actions (self);

  self->theme = KGX_THEME_NIGHT;
  self->close_on_zero = TRUE;

  pact = g_property_action_new ("theme", G_OBJECT (self), "theme");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (pact));

  pact = g_property_action_new ("find", G_OBJECT (self->search_bar), "search-mode-enabled");
  g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (pact));

  g_object_bind_property_full (self->terminal, "current-directory-uri",
                               self->header_bar, "subtitle",
                               G_BINDING_SYNC_CREATE,
                               update_subtitle,
                               NULL, NULL, NULL);

  g_object_bind_property (self, "theme",
                          self->terminal, "theme",
                          G_BINDING_SYNC_CREATE);


  pty = vte_pty_new_sync (fp_vte_pty_default_flags (), NULL, &error);

  shell[0] = vte_get_user_shell ();
  if (shell[0] == NULL) {
    shell[0] = "/bin/sh";
    g_warning ("Defaulting to /bin/sh");
  }
  shell[0] = fp_vte_guess_shell (NULL, &error);

  if (self->working_dir) {
    initial = self->working_dir;
  } else {
    initial = g_get_home_dir ();
  }

  env = g_environ_setenv (env, "TERM", "xterm-256color", TRUE);

  vte_terminal_set_pty (VTE_TERMINAL (self->terminal), pty);
  fp_vte_pty_spawn_async (pty,
                          initial,
                          (const gchar * const *) shell,
                          (const gchar * const *) env,
                          -1,
                          NULL,
                          (GAsyncReadyCallback) spawned,
                          self);
}

/**
 * kgx_window_get_working_dir:
 * @self: the #KgxWindow
 * 
 * Get the working directory path of this window, used to open new windows
 * in the same directory
 */
const char *
kgx_window_get_working_dir (KgxWindow *self)
{
  const char *uri;
  g_autoptr (GFile) file = NULL;

  g_return_val_if_fail (KGX_IS_WINDOW (self), NULL);

  uri = vte_terminal_get_current_directory_uri (VTE_TERMINAL (self->terminal));
  file = g_file_new_for_uri (uri);

  return g_file_get_path (file);
}

/**
 * kgx_window_push_root:
 * @self: the #KgxWindow
 * 
 * Increase the count of processes running as root and applying the
 * appropriate styles
 */
void
kgx_window_push_root (KgxWindow *self)
{
  g_return_if_fail (KGX_IS_WINDOW (self));

  self->root++;

  g_debug ("root push now at %i", self->root);

  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               KGX_WINDOW_STYLE_ROOT);
}

/**
 * kgx_window_pop_root:
 * @self: the #KgxWindow
 * 
 * Reduce the count of root children, removing styles if we hit 0
 */
void
kgx_window_pop_root (KgxWindow *self)
{
  g_return_if_fail (KGX_IS_WINDOW (self));

  self->root--;

  g_debug ("root pop now at %i", self->root);

  if (self->root <= 0) {
    gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                    KGX_WINDOW_STYLE_ROOT);
  }
}

/**
 * kgx_window_push_remote:
 * @self: the #KgxWindow
 * 
 * Same as kgx_window_push_root() but for ssh
 */
void
kgx_window_push_remote (KgxWindow *self)
{
  g_return_if_fail (KGX_IS_WINDOW (self));

  self->remote++;

  g_debug ("remote push now at %i", self->remote);

  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                               KGX_WINDOW_STYLE_REMOTE);
}

/**
 * kgx_window_pop_remote:
 * @self: the #KgxWindow
 * 
 * Same as kgx_window_pop_root() but for ssh
 */
void
kgx_window_pop_remote (KgxWindow *self)
{
  g_return_if_fail (KGX_IS_WINDOW (self));

  self->remote--;

  g_debug ("remote pop now at %i", self->remote);

  if (self->remote <= 0) {
    gtk_style_context_remove_class (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                    KGX_WINDOW_STYLE_REMOTE);
  }
}
