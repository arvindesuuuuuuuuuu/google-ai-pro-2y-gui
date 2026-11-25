#include "../src/runner.h"
#include "../src/ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_ansi_strip(void) {
    const char *raw = "\x1b[1m▶ Python virtualenv\x1b[0m\r\n   ok\n";
    char clean[128] = {0};
    size_t len = runner_strip_ansi(raw, strlen(raw), clean, sizeof(clean));
    assert(len > 0);
    assert(strstr(clean, "\x1b") == NULL);
    assert(strstr(clean, "\r") == NULL);
    assert(strcmp(clean, "▶ Python virtualenv\n   ok\n") == 0);
}

static void test_plan_options_and_argv(void) {
    size_t count = 0;
    const G2yPlanOption *opts = runner_plan_options(&count);
    assert(count == G2Y_PLAN_COUNT);
    assert(opts != NULL);
    assert(runner_plan_from_id("pro12") == G2Y_PLAN_PRO12);
    assert(runner_plan_from_id("5x") == G2Y_PLAN_ULTRA_5X);
    assert(runner_plan_from_id("20x") == G2Y_PLAN_ULTRA_20X);
    assert(runner_plan_from_id("unknown") == G2Y_PLAN_ALL);

    char **setup_argv = runner_build_argv("/repo", G2Y_ACTION_SETUP, G2Y_PLAN_ALL, NULL, false);
    assert(strcmp(setup_argv[0], "/repo/scripts/setup.sh") == 0);
    assert(setup_argv[1] == NULL);
    g_strfreev(setup_argv);

    char **show_argv = runner_build_argv("/repo", G2Y_ACTION_SHOW_PLANS, G2Y_PLAN_ULTRA_5X, NULL, false);
    assert(strcmp(show_argv[0], "/repo/scripts/run.sh") == 0);
    assert(strcmp(show_argv[1], "scan") == 0);
    assert(strcmp(show_argv[2], "query") == 0);
    assert(strcmp(show_argv[3], "--timeout") == 0);
    assert(strcmp(show_argv[4], "10") == 0);
    assert(strcmp(show_argv[5], "--skus") == 0);
    assert(strcmp(show_argv[6], "g1.play.20tb") == 0);
    assert(show_argv[7] == NULL);
    g_strfreev(show_argv);

    char **plan_argv = runner_build_argv("/repo", G2Y_ACTION_PLAN, G2Y_PLAN_COMBO, "a@b.com", false);
    assert(strcmp(plan_argv[0], "/repo/scripts/run.sh") == 0);
    assert(strcmp(plan_argv[1], "combo") == 0);
    assert(strcmp(plan_argv[2], "plan") == 0);
    assert(strcmp(plan_argv[3], "--device") == 0);
    assert(strcmp(plan_argv[4], "--account") == 0);
    assert(strcmp(plan_argv[5], "a@b.com") == 0);
    assert(plan_argv[6] == NULL);
    g_strfreev(plan_argv);

    char **run_combo = runner_build_argv("/repo", G2Y_ACTION_RUN, G2Y_PLAN_COMBO, "", true);
    assert(strcmp(run_combo[1], "combo") == 0);
    assert(strcmp(run_combo[2], "run") == 0);
    assert(strcmp(run_combo[3], "--device") == 0);
    assert(strcmp(run_combo[4], "--execute") == 0);
    assert(strcmp(run_combo[5], "--yes") == 0);
    assert(strcmp(run_combo[6], "--open-app") == 0);
    assert(run_combo[7] == NULL);
    g_strfreev(run_combo);

    char **run_step = runner_build_argv("/repo", G2Y_ACTION_RUN, G2Y_PLAN_PRO6B, "", false);
    assert(strcmp(run_step[1], "step3") == 0);
    assert(run_step[2] == NULL);
    g_strfreev(run_step);

    char **run_ultra = runner_build_argv("/repo", G2Y_ACTION_RUN, G2Y_PLAN_ULTRA_20X, "", false);
    assert(strcmp(run_ultra[1], "ultra") == 0);
    assert(strcmp(run_ultra[2], "20x") == 0);
    assert(run_ultra[3] == NULL);
    g_strfreev(run_ultra);
}

static void test_plan_line_parser(void) {
    G2yParsedPlan parsed = {0};

    assert(!runner_parse_plan_line("SKU | offerId | Period | micros | Title", &parsed));
    assert(!runner_parse_plan_line("--- | --- | --- | ---: | ---", &parsed));

    assert(runner_parse_plan_line(
        "g1.2tb.ai.subs_variant | 6months-eft-2 | P6M | 0 | Google AI Pro (5 TB)", &parsed));
    assert(parsed.is_free);
    assert(strcmp(parsed.offer_id, "6months-eft-2") == 0);
    assert(strcmp(parsed.period_label, "+6m") == 0);
    assert(strcmp(parsed.price_label, "FREE") == 0);
    assert(parsed.mapped_plan == G2Y_PLAN_PRO6B);

    assert(runner_parse_plan_line(
        "g1.2tb.ai.subs_variant | null | P1M | 19,990,000 | Google AI Pro (5 TB)", &parsed));
    assert(!parsed.is_free);
    assert(strcmp(parsed.offer_id, "null") == 0);
    assert(strcmp(parsed.period_label, "+1m") == 0);
    assert(strcmp(parsed.price_label, "19.99") == 0);

    assert(runner_parse_plan_line(
        "1. [Stack] g1.2tb.ai.subs_variant / 6months-eft / +6 months -> 2028-05-25", &parsed));
    assert(parsed.is_step);
    assert(parsed.is_free);
    assert(strcmp(parsed.period_label, "+6m") == 0);
    assert(strcmp(parsed.price_label, "Stack") == 0);
    assert(parsed.mapped_plan == G2Y_PLAN_PRO6);

    assert(runner_parse_plan_line("   offer 12months-eft -> 0 ()", &parsed));
    assert(parsed.is_free);
    assert(parsed.mapped_plan == G2Y_PLAN_PRO12);
}

static void test_detect_real_repo(void) {
    G2yEnvStatus st = {0};
    runner_detect_env(G2Y_DEFAULT_REPO, &st);
    assert(st.repo_ok);
    assert(st.has_agent);

    G2yEnvStatus missing = {0};
    runner_detect_env("/nonexistent/repo/path", &missing);
    assert(!missing.repo_ok);
    assert(!missing.has_venv);
}

static void test_ui_portrait_geometry_and_css(void) {
    int argc = 0;
    char **argv = NULL;
    if (!gtk_init_check(&argc, &argv)) {
        printf("SKIP_UI_NO_DISPLAY\n");
        return;
    }

    GtkCssProvider *provider = NULL;
    assert(ui_install_css(&provider));
    g_object_unref(provider);

    G2yUI *ui = ui_create(NULL, G2Y_DEFAULT_REPO);
    assert(ui != NULL);
    assert(!gtk_window_get_resizable(GTK_WINDOW(ui->window)));
    assert(ui->env.repo_ok);
    assert(G2Y_WIN_H > G2Y_WIN_W);

    gtk_widget_show_all(ui->window);
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }

    GtkWidget *root = gtk_bin_get_child(GTK_BIN(ui->window));
    int alloc_w = gtk_widget_get_allocated_width(root);
    int alloc_h = gtk_widget_get_allocated_height(root);
    int win_w = 0, win_h = 0;
    gtk_window_get_size(GTK_WINDOW(ui->window), &win_w, &win_h);

    printf("UI_GEOM root_alloc=%dx%d win_size=%dx%d\n", alloc_w, alloc_h, win_w, win_h);
    assert(alloc_w == G2Y_WIN_W - 20);
    assert(alloc_h == G2Y_WIN_H - 20);
    assert(win_w == G2Y_WIN_W);
    assert(win_h == G2Y_WIN_H);
    assert(!gtk_text_buffer_get_has_selection(ui->log_buf));
    assert(ui->pop_plan != NULL);
    assert(gtk_popover_get_position(GTK_POPOVER(ui->pop_plan)) == GTK_POS_BOTTOM);
    assert(ui->active_plan == G2Y_PLAN_ALL);

    gtk_widget_destroy(ui->window);
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
}

int main(void) {
    test_ansi_strip();
    test_plan_options_and_argv();
    test_plan_line_parser();
    test_detect_real_repo();
    test_ui_portrait_geometry_and_css();
    printf("ALL_TESTS_OK\n");
    return 0;
}
