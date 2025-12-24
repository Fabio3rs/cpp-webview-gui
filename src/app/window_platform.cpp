#include "app/window_platform.h"

#if defined(__linux__)
#include <gtk/gtk.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace app {

void attach_window_to_parent(void *parent_window, void *child_window) {
#if defined(__linux__)
    if (!parent_window || !child_window) {
        return;
    }

    auto *parent = GTK_WINDOW(parent_window);
    auto *child = GTK_WINDOW(child_window);
    gtk_window_set_transient_for(child, parent);
    gtk_window_set_destroy_with_parent(child, TRUE);

#if GTK_MAJOR_VERSION < 4
    GtkWindowGroup *group = gtk_window_get_group(parent);
    if (!group) {
        group = gtk_window_group_new();
        gtk_window_group_add_window(group, parent);
        g_object_unref(group);
        group = gtk_window_get_group(parent);
    }
    if (group) {
        gtk_window_group_add_window(group, child);
    }
#endif
#else
    (void)parent_window;
    (void)child_window;
#endif
}

void move_window_to(void *window, int left, int top) {
#if defined(__linux__)
    if (!window) {
        return;
    }
#if GTK_MAJOR_VERSION < 4
    gtk_window_move(GTK_WINDOW(window), left, top);
#else
    (void)left;
    (void)top;
#endif
#elif defined(_WIN32)
    if (!window) {
        return;
    }
    auto *hwnd = static_cast<HWND>(window);
    SetWindowPos(hwnd, nullptr, left, top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    (void)window;
    (void)left;
    (void)top;
#endif
}

} // namespace app
