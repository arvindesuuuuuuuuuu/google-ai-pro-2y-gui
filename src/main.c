#include "ui.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char   *repo_dir;
    G2yRemoteCfg  remote;
} AppConfig;

static void on_activate(GtkApplication *app, gpointer user_data) {
    const AppConfig *cfg = (const AppConfig *)user_data;
    GList *windows = gtk_application_get_windows(app);
    if (windows) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }
    G2yUI *ui = ui_create_ex(app, cfg ? cfg->repo_dir : NULL, cfg ? &cfg->remote : NULL);
    if (ui && ui->window) {
        gtk_widget_show_all(ui->window);
    }
}

int main(int argc, char **argv) {
    const char *repo_dir = getenv("G2Y_REPO");
    const char *ssh_target = getenv("G2Y_SSH");
    const char *ssh_sock = getenv("G2Y_SSH_SOCK");
    const char *remote_repo = getenv("G2Y_REMOTE_REPO");

    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--repo") == 0) {
            repo_dir = argv[++i];
        } else if (strcmp(argv[i], "--ssh") == 0) {
            ssh_target = argv[++i];
        } else if (strcmp(argv[i], "--ssh-sock") == 0) {
            ssh_sock = argv[++i];
        } else if (strcmp(argv[i], "--remote-repo") == 0) {
            remote_repo = argv[++i];
        }
    }
    if (!repo_dir || !*repo_dir) {
        repo_dir = G2Y_DEFAULT_REPO;
    }

    AppConfig cfg = {
        .repo_dir = repo_dir,
        .remote = {
            .ssh_target = ssh_target,
            .ssh_sock = ssh_sock,
            .remote_repo = remote_repo
        }
    };
    GtkApplication *app = gtk_application_new(
        "local.bench.g2y_gui",
        G_APPLICATION_DEFAULT_FLAGS
    );
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &cfg);
    /* Pass argc=1 so custom CLI flags are handled cleanly before GtkApplication. */
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    return status;
}
