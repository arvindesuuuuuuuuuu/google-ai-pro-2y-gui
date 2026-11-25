#include "ui.h"
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_class(GtkWidget *w, const char *cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

static void remove_class(GtkWidget *w, const char *cls) {
    gtk_style_context_remove_class(gtk_widget_get_style_context(w), cls);
}

static void on_css_error(GtkCssProvider *provider, GtkCssSection *section,
                         const GError *error, gpointer user_data) {
    (void)provider;
    (void)section;
    bool *failed = (bool *)user_data;
    if (failed) *failed = true;
    if (error) g_warning("CSS error: %s", error->message);
}

bool ui_install_css(GtkCssProvider **out_provider) {
    GtkCssProvider *provider = gtk_css_provider_new();
    bool failed = false;
    g_signal_connect(provider, "parsing-error", G_CALLBACK(on_css_error), &failed);
    gchar *full_css = g_strconcat(G2Y_CSS_BASE, G2Y_CSS_VIEWS, NULL);
    gtk_css_provider_load_from_data(provider, full_css, -1, NULL);
    g_free(full_css);

    GdkScreen *screen = gdk_screen_get_default();
    if (screen) {
        gtk_style_context_add_provider_for_screen(
            screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
        );
    }
    if (out_provider) {
        *out_provider = provider;
    } else {
        g_object_unref(provider);
    }
    return !failed;
}

static G2yPlan active_plan(G2yUI *ui) {
    if (!ui) return G2Y_PLAN_ALL;
    return ui->active_plan;
}

static void set_active_plan(G2yUI *ui, G2yPlan plan, bool announce) {
    if (!ui) return;
    if (plan < 0 || plan >= G2Y_PLAN_COUNT) plan = G2Y_PLAN_ALL;
    ui->active_plan = plan;
    const G2yPlanOption *opt = runner_plan_info(plan);
    if (ui->lbl_plan_btn) {
        gtk_label_set_text(GTK_LABEL(ui->lbl_plan_btn), opt->label);
    }
    for (size_t i = 0; i < G2Y_PLAN_COUNT; i++) {
        GtkWidget *item_btn = ui->pop_plan_btns[i];
        if (!item_btn) continue;
        if ((G2yPlan)i == plan) {
            add_class(item_btn, "g2y-pop-item-active");
        } else {
            remove_class(item_btn, "g2y-pop-item-active");
        }
    }
    if (ui->pop_plan) {
        gtk_popover_popdown(GTK_POPOVER(ui->pop_plan));
    }
    if (announce && ui->lbl_status && !ui->proc.running) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Plan preset: %s (Show to scan, Run to execute)", opt->short_name);
        gtk_label_set_text(GTK_LABEL(ui->lbl_status), msg);
    }
}

static void update_plans_counter(G2yUI *ui) {
    char msg[64];
    if (ui->plan_count == 0) {
        snprintf(msg, sizeof(msg), ui->proc.running ? "scanning..." : "none");
    } else if (ui->plan_free_count > 0) {
        snprintf(msg, sizeof(msg), "%zu found · %zu free", ui->plan_count, ui->plan_free_count);
    } else {
        snprintf(msg, sizeof(msg), "%zu found", ui->plan_count);
    }
    gtk_label_set_text(GTK_LABEL(ui->lbl_plans_count), msg);
}

static void add_empty_plan_hint(G2yUI *ui, const char *text) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    add_class(row, "g2y-plan-row");

    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    add_class(lbl, "g2y-sub");
    gtk_container_add(GTK_CONTAINER(row), lbl);
    gtk_container_add(GTK_CONTAINER(ui->plan_list), row);
    gtk_widget_show_all(row);
    ui->plan_empty_row = row;
}

static void clear_detected_plans(G2yUI *ui, const char *hint) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(ui->plan_list));
    for (GList *it = children; it != NULL; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);

    ui->plan_empty_row = NULL;
    ui->plan_count = 0;
    ui->plan_free_count = 0;
    if (hint && *hint) {
        add_empty_plan_hint(ui, hint);
    }
    update_plans_counter(ui);
}

static size_t find_insert_pos(const G2yUI *ui, const G2yParsedPlan *item) {
    if (item->is_step) return ui->plan_count;
    for (size_t i = 0; i < ui->plan_count; i++) {
        const G2yParsedPlan *cur = &ui->plans[i];
        if (cur->is_step) continue;
        if (item->is_free && !cur->is_free) return i;
        if (item->is_free == cur->is_free &&
            item->duration_months > cur->duration_months) {
            return i;
        }
    }
    return ui->plan_count;
}

static void add_detected_plan_row(G2yUI *ui, const G2yParsedPlan *item) {
    if (!ui || !item || ui->plan_count >= G2Y_MAX_PLANS) return;

    for (size_t i = 0; i < ui->plan_count; i++) {
        if (ui->plans[i].is_step == item->is_step &&
            strcmp(ui->plans[i].sku, item->sku) == 0 &&
            strcmp(ui->plans[i].offer_id, item->offer_id) == 0) {
            return;
        }
    }

    if (ui->plan_empty_row) {
        gtk_widget_destroy(ui->plan_empty_row);
        ui->plan_empty_row = NULL;
    }

    size_t pos = find_insert_pos(ui, item);
    if (pos < ui->plan_count) {
        memmove(&ui->plans[pos + 1], &ui->plans[pos],
                (ui->plan_count - pos) * sizeof(G2yParsedPlan));
    }
    ui->plans[pos] = *item;
    ui->plan_count++;
    if (item->is_free) ui->plan_free_count++;

    G2yParsedPlan *owned = malloc(sizeof(G2yParsedPlan));
    if (!owned) return;
    *owned = *item;

    GtkWidget *row = gtk_list_box_row_new();
    add_class(row, "g2y-plan-row");
    g_object_set_data_full(G_OBJECT(row), "g2y-plan-item", owned, free);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);

    GtkWidget *lbl_id = gtk_label_new(item->offer_id);
    gtk_label_set_xalign(GTK_LABEL(lbl_id), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_id), PANGO_ELLIPSIZE_END);
    add_class(lbl_id, "g2y-plan-id");

    GtkWidget *lbl_sku = gtk_label_new(item->detail);
    gtk_label_set_xalign(GTK_LABEL(lbl_sku), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_sku), PANGO_ELLIPSIZE_END);
    add_class(lbl_sku, "g2y-plan-sku");

    gtk_box_pack_start(GTK_BOX(left), lbl_id, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), lbl_sku, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), left, TRUE, TRUE, 0);

    GtkWidget *badges = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(badges, GTK_ALIGN_CENTER);

    if (item->period_label[0]) {
        GtkWidget *dur = gtk_label_new(item->period_label);
        add_class(dur, "g2y-badge-dur");
        gtk_box_pack_start(GTK_BOX(badges), dur, FALSE, FALSE, 0);
    }

    GtkWidget *price = gtk_label_new(item->price_label);
    add_class(price, item->is_free ? "g2y-badge-free" : "g2y-badge-paid");
    gtk_box_pack_start(GTK_BOX(badges), price, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(hbox), badges, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), hbox);
    gtk_list_box_insert(GTK_LIST_BOX(ui->plan_list), row, (gint)pos);
    gtk_widget_show_all(row);
    update_plans_counter(ui);
}

static void feed_plan_parser(G2yUI *ui, const char *chunk, bool flush) {
    if (chunk) {
        for (const char *p = chunk; *p; p++) {
            if (*p == '\n') {
                ui->line_acc[ui->line_len] = '\0';
                G2yParsedPlan parsed;
                if (runner_parse_plan_line(ui->line_acc, &parsed)) {
                    add_detected_plan_row(ui, &parsed);
                }
                ui->line_len = 0;
            } else if (ui->line_len + 1 < sizeof(ui->line_acc)) {
                ui->line_acc[ui->line_len++] = *p;
            }
        }
    }
    if (flush && ui->line_len > 0) {
        ui->line_acc[ui->line_len] = '\0';
        G2yParsedPlan parsed;
        if (runner_parse_plan_line(ui->line_acc, &parsed)) {
            add_detected_plan_row(ui, &parsed);
        }
        ui->line_len = 0;
    }
}

static void append_log(G2yUI *ui, const char *text) {
    if (!ui || !ui->log_buf || !text || !*text) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui->log_buf, &end);
    gtk_text_buffer_insert(ui->log_buf, &end, text, -1);
    if (ui->log_end_mark) {
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(ui->log_view),
                                     ui->log_end_mark, 0.0, FALSE, 0.0, 1.0);
    }
}

static void set_pill(G2yUI *ui, const char *label, const char *variant_cls) {
    gtk_label_set_text(GTK_LABEL(ui->lbl_pill), label);
    remove_class(ui->lbl_pill, "g2y-pill-warn");
    remove_class(ui->lbl_pill, "g2y-pill-busy");
    if (variant_cls) add_class(ui->lbl_pill, variant_cls);
}

static void sync_controls(G2yUI *ui, bool waiting_enter) {
    bool busy = ui->proc.running;
    bool ready = ui->env.repo_ok && ui->env.has_venv;

    gtk_widget_set_sensitive(ui->btn_plan_menu, !busy);
    gtk_widget_set_sensitive(ui->btn_show,      !busy && ready);
    gtk_widget_set_sensitive(ui->btn_setup,     !busy && ui->env.repo_ok);
    gtk_widget_set_sensitive(ui->btn_check,     !busy && ready);
    gtk_widget_set_sensitive(ui->btn_plan,      !busy && ready);
    gtk_widget_set_sensitive(ui->btn_run,       !busy && ready);
    gtk_widget_set_sensitive(ui->entry_account, !busy);
    gtk_widget_set_sensitive(ui->chk_open_app,  !busy);

    gtk_widget_set_sensitive(ui->btn_stop, busy);
    gtk_widget_set_sensitive(ui->btn_next, busy && waiting_enter);
}

void ui_refresh_env(G2yUI *ui) {
    if (!ui) return;
    runner_detect_env(ui->repo_dir, &ui->env);

    char msg[1100];
    if (!ui->env.repo_ok) {
        snprintf(msg, sizeof(msg), "Repo scripts missing under %s", ui->repo_dir);
        set_pill(ui, "NO REPO", "g2y-pill-warn");
    } else if (!ui->env.has_venv) {
        snprintf(msg, sizeof(msg), "venv missing · adb %s · click Setup first",
                 ui->env.has_adb ? "ok" : "none");
        set_pill(ui, "NO VENV", "g2y-pill-warn");
    } else {
        snprintf(msg, sizeof(msg), "venv ok · agent %s · adb %s",
                 ui->env.has_agent ? "ok" : "missing",
                 ui->env.has_adb ? "ok" : "none");
        if (!ui->proc.running) set_pill(ui, "READY", NULL);
    }
    gtk_label_set_text(GTK_LABEL(ui->lbl_env), msg);
    sync_controls(ui, false);
}

static void on_proc_output(const char *text, bool waiting_enter, void *user_data) {
    G2yUI *ui = (G2yUI *)user_data;
    append_log(ui, text);
    feed_plan_parser(ui, text, false);
    if (waiting_enter) {
        set_pill(ui, "WAIT ENTER", "g2y-pill-warn");
        gtk_label_set_text(GTK_LABEL(ui->lbl_status),
                           "Confirm Play checkout on device, then click Next Step");
        sync_controls(ui, true);
    }
}

static void on_proc_done(int exit_code, bool signaled, void *user_data) {
    G2yUI *ui = (G2yUI *)user_data;
    feed_plan_parser(ui, NULL, true);
    if (ui->plan_count == 0 && !ui->plan_empty_row) {
        add_empty_plan_hint(ui, "No plans parsed yet — pick a Plan and click Show");
    }

    char status[96];
    if (signaled) {
        snprintf(status, sizeof(status), "Stopped (signal %d)", exit_code - 128);
    } else {
        snprintf(status, sizeof(status), "Finished (exit %d)", exit_code);
    }
    gtk_label_set_text(GTK_LABEL(ui->lbl_status), status);
    append_log(ui, "\n");
    ui_refresh_env(ui);
    update_plans_counter(ui);
}

static void launch_action(G2yUI *ui, G2yAction action, const char *title) {
    if (ui->proc.running) return;

    G2yPlan plan = active_plan(ui);
    const char *acct = gtk_entry_get_text(GTK_ENTRY(ui->entry_account));
    bool open_app = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->chk_open_app));

    if (action == G2Y_ACTION_SHOW_PLANS || action == G2Y_ACTION_PLAN) {
        ui->line_len = 0;
        clear_detected_plans(ui, "Querying Play offers from device...");
    }

    char banner[160];
    snprintf(banner, sizeof(banner), "=== %s ===\n", title);
    append_log(ui, banner);

    char err[192] = {0};
    if (!runner_spawn(&ui->proc, ui->repo_dir, action, plan, acct, open_app,
                      on_proc_output, on_proc_done, ui, err, sizeof(err))) {
        append_log(ui, "ERR: ");
        append_log(ui, err);
        append_log(ui, "\n");
        gtk_label_set_text(GTK_LABEL(ui->lbl_status), err);
        if (action == G2Y_ACTION_SHOW_PLANS || action == G2Y_ACTION_PLAN) {
            clear_detected_plans(ui, "Scan failed — verify adb and Google One open");
        }
        return;
    }

    set_pill(ui, "RUNNING", "g2y-pill-busy");
    snprintf(banner, sizeof(banner), "Running: %s", title);
    gtk_label_set_text(GTK_LABEL(ui->lbl_status), banner);
    sync_controls(ui, false);
    update_plans_counter(ui);
}

static void on_setup_clicked(GtkButton *b, gpointer d) {
    (void)b;
    launch_action((G2yUI *)d, G2Y_ACTION_SETUP, "setup.sh");
}

static void on_check_clicked(GtkButton *b, gpointer d) {
    (void)b;
    launch_action((G2yUI *)d, G2Y_ACTION_CHECK, "run.sh check");
}

static void on_show_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    const G2yPlanOption *opt = runner_plan_info(active_plan(ui));
    char title[96];
    snprintf(title, sizeof(title), "scan query (%s)", opt->short_name);
    launch_action(ui, G2Y_ACTION_SHOW_PLANS, title);
}

static void on_plan_clicked(GtkButton *b, gpointer d) {
    (void)b;
    launch_action((G2yUI *)d, G2Y_ACTION_PLAN, "combo plan --device");
}

static void on_run_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    const G2yPlanOption *opt = runner_plan_info(active_plan(ui));
    char title[96];
    snprintf(title, sizeof(title), "run (%s)", opt->short_name);
    launch_action(ui, G2Y_ACTION_RUN, title);
}

static void on_plan_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer d) {
    (void)box;
    if (!row) return;
    G2yUI *ui = (G2yUI *)d;
    if (ui->proc.running) return;
    if (!gtk_list_box_row_get_selectable(row)) return;

    const G2yParsedPlan *item = g_object_get_data(G_OBJECT(row), "g2y-plan-item");
    if (!item) return;
    const G2yPlanOption *target = runner_plan_info(item->mapped_plan);
    set_active_plan(ui, target->plan, false);

    char msg[192];
    snprintf(msg, sizeof(msg), "Selected %s (%s) -> %s",
             item->offer_id, item->price_label, target->short_name);
    gtk_label_set_text(GTK_LABEL(ui->lbl_status), msg);
}

static void on_pop_plan_clicked(GtkButton *btn, gpointer d) {
    G2yUI *ui = (G2yUI *)d;
    G2yPlan plan = (G2yPlan)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "g2y-plan-id"));
    set_active_plan(ui, plan, true);
}

static void on_next_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    if (runner_send_enter(&ui->proc)) {
        append_log(ui, "[Enter sent]\n");
        set_pill(ui, "RUNNING", "g2y-pill-busy");
        gtk_label_set_text(GTK_LABEL(ui->lbl_status), "Continuing combo step...");
        sync_controls(ui, false);
    }
}

static void on_stop_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    runner_stop(&ui->proc);
    gtk_label_set_text(GTK_LABEL(ui->lbl_status), "Stopping...");
}

static void on_clear_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    gtk_text_buffer_set_text(ui->log_buf, "", 0);
    if (!ui->proc.running) {
        clear_detected_plans(ui, "Pick a Plan above and click Show to list offers");
        gtk_label_set_text(GTK_LABEL(ui->lbl_status), "Cleared");
    }
}

static gboolean reset_copy_lbl(gpointer d) {
    G2yUI *ui = (G2yUI *)d;
    ui->copy_reset_id = 0;
    gtk_button_set_label(GTK_BUTTON(ui->btn_copy), "Copy");
    return G_SOURCE_REMOVE;
}

static void on_copy_clicked(GtkButton *b, gpointer d) {
    (void)b;
    G2yUI *ui = (G2yUI *)d;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->log_buf, &start, &end);
    gchar *text = gtk_text_buffer_get_text(ui->log_buf, &start, &end, FALSE);

    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    if (cb && text) {
        gtk_clipboard_set_text(cb, text, -1);
    }
    g_free(text);

    gtk_button_set_label(GTK_BUTTON(ui->btn_copy), "Copied");
    if (ui->copy_reset_id > 0) g_source_remove(ui->copy_reset_id);
    ui->copy_reset_id = g_timeout_add(1400, reset_copy_lbl, ui);
}

static void on_window_destroy(GtkWidget *w, gpointer d) {
    (void)w;
    G2yUI *ui = (G2yUI *)d;
    ui_cleanup(ui);
    free(ui);
}

static GtkWidget *make_btn(const char *label, const char *extra_cls,
                           GCallback cb, G2yUI *ui) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    add_class(btn, "g2y-btn");
    if (extra_cls) add_class(btn, extra_cls);
    g_signal_connect(btn, "clicked", cb, ui);
    return btn;
}

G2yUI *ui_create(GtkApplication *app, const char *repo_dir) {
    G2yUI *ui = calloc(1, sizeof(G2yUI));
    if (!ui) return NULL;
    ui->proc.stdin_fd = -1;
    ui->proc.out_fd = -1;
    ui->proc.err_fd = -1;

    const char *src_repo = (repo_dir && *repo_dir) ? repo_dir : G2Y_DEFAULT_REPO;
    snprintf(ui->repo_dir, sizeof(ui->repo_dir), "%s", src_repo);

    ui_install_css(NULL);

    ui->window = app ? gtk_application_window_new(app)
                     : gtk_window_new(GTK_WINDOW_TOPLEVEL);
    add_class(ui->window, "g2y-win");
    gtk_window_set_title(GTK_WINDOW(ui->window), "Google AI Pro 2Y");
    gtk_window_set_default_size(GTK_WINDOW(ui->window), G2Y_WIN_W, G2Y_WIN_H);
    gtk_window_set_resizable(GTK_WINDOW(ui->window), FALSE);
    gtk_window_set_position(GTK_WINDOW(ui->window), GTK_WIN_POS_CENTER);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(on_window_destroy), ui);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(root, G2Y_WIN_W, G2Y_WIN_H);
    gtk_container_set_border_width(GTK_CONTAINER(root), 10);
    gtk_container_add(GTK_CONTAINER(ui->window), root);

    /* Header */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *hdr_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget *title = gtk_label_new("Google AI Pro 2Y");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    add_class(title, "g2y-title");

    ui->lbl_env = gtk_label_new("checking...");
    gtk_label_set_xalign(GTK_LABEL(ui->lbl_env), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ui->lbl_env), PANGO_ELLIPSIZE_END);
    add_class(ui->lbl_env, "g2y-sub");

    gtk_box_pack_start(GTK_BOX(hdr_left), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hdr_left), ui->lbl_env, FALSE, FALSE, 0);

    ui->lbl_pill = gtk_label_new("READY");
    add_class(ui->lbl_pill, "g2y-pill");
    gtk_widget_set_valign(ui->lbl_pill, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(hdr), hdr_left, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hdr), ui->lbl_pill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), hdr, FALSE, FALSE, 0);

    /* Config card: Plan selector + Show button + Account input */
    GtkWidget *cfg_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    add_class(cfg_card, "g2y-card");

    GtkWidget *plan_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *lbl_plan = gtk_label_new("Plan");
    add_class(lbl_plan, "g2y-field-lbl");
    gtk_box_pack_start(GTK_BOX(plan_row), lbl_plan, FALSE, FALSE, 0);

    ui->btn_plan_menu = gtk_menu_button_new();
    add_class(ui->btn_plan_menu, "g2y-combo");
    GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui->lbl_plan_btn = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ui->lbl_plan_btn), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ui->lbl_plan_btn), PANGO_ELLIPSIZE_END);
    GtkWidget *arrow = gtk_label_new("▾");
    gtk_box_pack_start(GTK_BOX(menu_box), ui->lbl_plan_btn, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(menu_box), arrow, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(ui->btn_plan_menu), menu_box);

    ui->pop_plan = gtk_popover_new(ui->btn_plan_menu);
    add_class(ui->pop_plan, "g2y-popover");
    gtk_popover_set_position(GTK_POPOVER(ui->pop_plan), GTK_POS_BOTTOM);
    gtk_popover_set_constrain_to(GTK_POPOVER(ui->pop_plan), GTK_POPOVER_CONSTRAINT_WINDOW);

    GtkWidget *pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_size_request(pop_box, 310, -1);
    size_t n_plans = 0;
    const G2yPlanOption *opts = runner_plan_options(&n_plans);
    for (size_t i = 0; i < n_plans; i++) {
        GtkWidget *item_btn = gtk_button_new_with_label(opts[i].label);
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(item_btn));
        if (GTK_IS_LABEL(child)) {
            gtk_label_set_xalign(GTK_LABEL(child), 0.0f);
        }
        add_class(item_btn, "g2y-pop-item");
        g_object_set_data(G_OBJECT(item_btn), "g2y-plan-id", GINT_TO_POINTER((gint)opts[i].plan));
        g_signal_connect(item_btn, "clicked", G_CALLBACK(on_pop_plan_clicked), ui);
        gtk_box_pack_start(GTK_BOX(pop_box), item_btn, FALSE, FALSE, 0);
        ui->pop_plan_btns[i] = item_btn;
    }
    gtk_widget_show_all(pop_box);
    gtk_container_add(GTK_CONTAINER(ui->pop_plan), pop_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(ui->btn_plan_menu), ui->pop_plan);
    set_active_plan(ui, G2Y_PLAN_ALL, false);
    gtk_box_pack_start(GTK_BOX(plan_row), ui->btn_plan_menu, TRUE, TRUE, 0);

    ui->btn_show = make_btn("Show", "g2y-btn-accent", G_CALLBACK(on_show_clicked), ui);
    gtk_box_pack_end(GTK_BOX(plan_row), ui->btn_show, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cfg_card), plan_row, FALSE, FALSE, 0);

    GtkWidget *input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui->entry_account = gtk_entry_new();
    add_class(ui->entry_account, "g2y-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->entry_account),
                                   "Account email (optional, e.g. you@example.com)");
    ui->chk_open_app = gtk_check_button_new_with_label("Open App");
    add_class(ui->chk_open_app, "g2y-check");
    gtk_box_pack_start(GTK_BOX(input_row), ui->entry_account, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(input_row), ui->chk_open_app, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cfg_card), input_row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), cfg_card, FALSE, FALSE, 0);

    /* Primary action bar */
    GtkWidget *act_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(act_row), TRUE);
    ui->btn_setup = make_btn("Setup", NULL,          G_CALLBACK(on_setup_clicked), ui);
    ui->btn_check = make_btn("Check", NULL,          G_CALLBACK(on_check_clicked), ui);
    ui->btn_plan  = make_btn("Plan",  NULL,          G_CALLBACK(on_plan_clicked),  ui);
    ui->btn_run   = make_btn("Run",   "g2y-btn-run", G_CALLBACK(on_run_clicked),   ui);

    gtk_box_pack_start(GTK_BOX(act_row), ui->btn_setup, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), ui->btn_check, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), ui->btn_plan,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(act_row), ui->btn_run,   TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), act_row, FALSE, FALSE, 0);

    /* Detected Plans card (structured list for easy viewing & selection) */
    GtkWidget *plans_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    add_class(plans_card, "g2y-card");

    GtkWidget *plans_hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *plans_title = gtk_label_new("DETECTED PLANS");
    gtk_label_set_xalign(GTK_LABEL(plans_title), 0.0f);
    add_class(plans_title, "g2y-sect");
    ui->lbl_plans_count = gtk_label_new("none");
    add_class(ui->lbl_plans_count, "g2y-sub");
    gtk_box_pack_start(GTK_BOX(plans_hdr), plans_title, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(plans_hdr), ui->lbl_plans_count, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(plans_card), plans_hdr, FALSE, FALSE, 0);

    GtkWidget *plans_scroll = gtk_scrolled_window_new(NULL, NULL);
    add_class(plans_scroll, "g2y-list-wrap");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(plans_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(plans_scroll, -1, 146);

    ui->plan_list = gtk_list_box_new();
    add_class(ui->plan_list, "g2y-plan-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->plan_list), GTK_SELECTION_SINGLE);
    g_signal_connect(ui->plan_list, "row-selected", G_CALLBACK(on_plan_row_selected), ui);
    gtk_container_add(GTK_CONTAINER(plans_scroll), ui->plan_list);
    gtk_box_pack_start(GTK_BOX(plans_card), plans_scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), plans_card, FALSE, FALSE, 0);

    /* Output console */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    add_class(scroll, "g2y-console-wrap");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    ui->log_view = gtk_text_view_new();
    add_class(ui->log_view, "g2y-console");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ui->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ui->log_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui->log_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(ui->log_view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(ui->log_view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(ui->log_view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(ui->log_view), 6);
    ui->log_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui->log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui->log_buf, &end);
    ui->log_end_mark = gtk_text_buffer_create_mark(ui->log_buf, "log_end", &end, FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), ui->log_view);
    gtk_box_pack_start(GTK_BOX(root), scroll, TRUE, TRUE, 0);

    /* Two-line portrait footer: full-width status + 4 controls */
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    ui->lbl_status = gtk_label_new("Idle");
    gtk_label_set_xalign(GTK_LABEL(ui->lbl_status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(ui->lbl_status), PANGO_ELLIPSIZE_END);
    add_class(ui->lbl_status, "g2y-sub");

    GtkWidget *fbtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(fbtns), TRUE);
    ui->btn_next  = make_btn("Next Step", "g2y-btn-step", G_CALLBACK(on_next_clicked), ui);
    ui->btn_stop  = make_btn("Stop",      "g2y-btn-stop", G_CALLBACK(on_stop_clicked), ui);
    ui->btn_clear = make_btn("Clear",     NULL,           G_CALLBACK(on_clear_clicked), ui);
    ui->btn_copy  = make_btn("Copy",      NULL,           G_CALLBACK(on_copy_clicked), ui);

    gtk_box_pack_start(GTK_BOX(fbtns), ui->btn_next,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fbtns), ui->btn_stop,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fbtns), ui->btn_clear, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(fbtns), ui->btn_copy,  TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(footer), ui->lbl_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer), fbtns, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(root), footer, FALSE, FALSE, 0);

    clear_detected_plans(ui, "Pick a Plan above and click Show to list offers");
    ui_refresh_env(ui);
    if (!ui->env.has_venv) {
        append_log(ui, "Environment not initialized yet.\nClick Setup to create .venv and install frida.\n");
    } else {
        append_log(ui, "Ready. Pick a Plan -> Show to inspect offers, Plan for combo steps, or Run.\n");
    }
    return ui;
}

void ui_cleanup(G2yUI *ui) {
    if (!ui) return;
    if (ui->copy_reset_id > 0) {
        g_source_remove(ui->copy_reset_id);
        ui->copy_reset_id = 0;
    }
    runner_cleanup(&ui->proc);
}
