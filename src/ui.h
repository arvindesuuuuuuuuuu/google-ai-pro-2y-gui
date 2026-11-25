#ifndef G2Y_UI_H
#define G2Y_UI_H

#include "runner.h"
#include <gtk/gtk.h>
#include <stdbool.h>

#define G2Y_WIN_W 420
#define G2Y_WIN_H 640
#define G2Y_MAX_PLANS 64

typedef struct {
    GtkWidget     *window;
    GtkWidget     *lbl_env;
    GtkWidget     *lbl_pill;

    GtkWidget     *btn_plan_menu;
    GtkWidget     *lbl_plan_btn;
    GtkWidget     *pop_plan;
    GtkWidget     *pop_plan_btns[G2Y_PLAN_COUNT];
    G2yPlan        active_plan;
    GtkWidget     *btn_show;
    GtkWidget     *entry_account;
    GtkWidget     *chk_open_app;

    GtkWidget     *btn_setup;
    GtkWidget     *btn_check;
    GtkWidget     *btn_plan;
    GtkWidget     *btn_run;

    GtkWidget     *lbl_plans_count;
    GtkWidget     *plan_list;
    GtkWidget     *plan_empty_row;
    G2yParsedPlan  plans[G2Y_MAX_PLANS];
    size_t         plan_count;
    size_t         plan_free_count;

    GtkTextBuffer *log_buf;
    GtkTextMark   *log_end_mark;
    GtkWidget     *log_view;
    GtkWidget     *lbl_status;

    GtkWidget     *btn_next;
    GtkWidget     *btn_stop;
    GtkWidget     *btn_clear;
    GtkWidget     *btn_copy;

    char           repo_dir[1024];
    char           line_acc[1024];
    size_t         line_len;
    G2yEnvStatus   env;
    G2yProcess     proc;
    guint          copy_reset_id;
} G2yUI;

/* Installs embedded GTK3 CSS; returns false if GtkCssProvider reports an error. */
bool ui_install_css(GtkCssProvider **out_provider);

/* Builds the fixed 420x640 portrait window bound to repo_dir (or default). */
G2yUI *ui_create(GtkApplication *app, const char *repo_dir);

/* Updates header badges from runner_detect_env. */
void ui_refresh_env(G2yUI *ui);

/* Cleanly terminates child process and clears timers. */
void ui_cleanup(G2yUI *ui);

#endif /* G2Y_UI_H */
