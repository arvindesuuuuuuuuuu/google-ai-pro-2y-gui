#include "ui.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *repo_dir;
} AppConfig;

static void on_activate(GtkApplication *app, gpointer user_data) {
    const AppConfig *cfg = (const AppConfig *)user_data;
    GList *windows = gtk_application_get_windows(app);
    if (windows) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }
    G2yUI *ui = ui_create(app, cfg ? cfg->repo_dir : NULL);
    if (ui && ui->window) {
        gtk_widget_show_all(ui->window);
    }
}

int main(int argc, char **argv) {
    const char *repo_dir = getenv("G2Y_REPO");
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--repo") == 0) {
            repo_dir = argv[i + 1];
            break;
        }
    }
    if (!repo_dir || !*repo_dir) {
        repo_dir = G2Y_DEFAULT_REPO;
    }

    AppConfig cfg = { .repo_dir = repo_dir };
    GtkApplication *app = gtk_application_new(
        "local.bench.g2y_gui",
        G_APPLICATION_DEFAULT_FLAGS
    );
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &cfg);
    /* Pass argc=1 so custom --repo flag is handled cleanly before GtkApplication. */
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
