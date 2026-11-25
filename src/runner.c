#include "runner.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    G2yPlanOption opt;
    const char *const *scan_skus;
    size_t scan_sku_count;
    const char *run_cmd;
} PlanEntry;

static const char *const SKUS_ALL[] = {
    "g1.2tb.ai.2months_eft",
    "g1.2tb.ai.3months_eft",
    "g1.2tb.ai.4months_eft",
    "g1.2tb.ai.6months_eft",
    "g1.2tb.ai.12months_eft",
    "g1.2tb.ai.15months_eft",
    "g1.2tb.ai.18months_eft",
    "g1.2tb.ai.subs_variant",
    "g1.play.20tb",
    "g1.play.ai.pro",
    "g1.ai.pro",
    "g1.30tb"
};

static const char *const SKUS_COMBO[] = {
    "g1.2tb.ai.2months_eft",
    "g1.2tb.ai.3months_eft",
    "g1.2tb.ai.4months_eft",
    "g1.2tb.ai.6months_eft",
    "g1.2tb.ai.12months_eft",
    "g1.2tb.ai.15months_eft",
    "g1.2tb.ai.18months_eft",
    "g1.2tb.ai.subs_variant"
};

static const char *const SKUS_PRO12[] = {
    "g1.2tb.ai.subs_variant",
    "g1.2tb.ai.12months_eft"
};

static const char *const SKUS_PRO_VARIANT[] = {
    "g1.2tb.ai.subs_variant"
};

static const char *const SKUS_ULTRA_5X[] = {
    "g1.play.20tb"
};

static const char *const SKUS_ULTRA_20X[] = {
    "g1.play.ai.pro",
    "g1.ai.pro",
    "g1.30tb"
};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const PlanEntry PLAN_TABLE[G2Y_PLAN_COUNT] = {
    {
        { G2Y_PLAN_ALL, "all", "All Plans · Pro + Ultra (Scan)", "All Tiers" },
        SKUS_ALL, ARRAY_LEN(SKUS_ALL), "combo"
    },
    {
        { G2Y_PLAN_COMBO, "combo", "Combo Stack · Pro 24m+ (Plan / Run)", "Combo" },
        SKUS_COMBO, ARRAY_LEN(SKUS_COMBO), "combo"
    },
    {
        { G2Y_PLAN_PRO12, "pro12", "Pro 12m · 12months-eft (step1)", "Pro 12m" },
        SKUS_PRO12, ARRAY_LEN(SKUS_PRO12), "step1"
    },
    {
        { G2Y_PLAN_PRO6, "pro6", "Pro +6m · 6months-eft (step2)", "Pro +6m" },
        SKUS_PRO_VARIANT, ARRAY_LEN(SKUS_PRO_VARIANT), "step2"
    },
    {
        { G2Y_PLAN_PRO6B, "pro6b", "Pro +6m B · 6months-eft-2 (step3)", "Pro +6m B" },
        SKUS_PRO_VARIANT, ARRAY_LEN(SKUS_PRO_VARIANT), "step3"
    },
    {
        { G2Y_PLAN_PRO6C, "pro6c", "Pro +6m C · 6months-eft-ss-2026 (step4)", "Pro +6m C" },
        SKUS_PRO_VARIANT, ARRAY_LEN(SKUS_PRO_VARIANT), "step4"
    },
    {
        { G2Y_PLAN_ULTRA_5X, "5x", "Ultra 5x · 20TB (g1.play.20tb)", "Ultra 5x" },
        SKUS_ULTRA_5X, ARRAY_LEN(SKUS_ULTRA_5X), "5x"
    },
    {
        { G2Y_PLAN_ULTRA_20X, "20x", "Ultra 20x · 30TB (g1.play.ai.pro)", "Ultra 20x" },
        SKUS_ULTRA_20X, ARRAY_LEN(SKUS_ULTRA_20X), "20x"
    }
};

static const G2yPlanOption PLAN_PUBLIC[G2Y_PLAN_COUNT] = {
    { G2Y_PLAN_ALL,       "all",   "All Plans · Pro + Ultra (Scan)",        "All Tiers" },
    { G2Y_PLAN_COMBO,     "combo", "Combo Stack · Pro 24m+ (Plan / Run)",   "Combo"     },
    { G2Y_PLAN_PRO12,     "pro12", "Pro 12m · 12months-eft (step1)",        "Pro 12m"   },
    { G2Y_PLAN_PRO6,      "pro6",  "Pro +6m · 6months-eft (step2)",         "Pro +6m"   },
    { G2Y_PLAN_PRO6B,     "pro6b", "Pro +6m B · 6months-eft-2 (step3)",     "Pro +6m B" },
    { G2Y_PLAN_PRO6C,     "pro6c", "Pro +6m C · 6months-eft-ss-2026 (step4)", "Pro +6m C" },
    { G2Y_PLAN_ULTRA_5X,  "5x",    "Ultra 5x · 20TB (g1.play.20tb)",        "Ultra 5x"  },
    { G2Y_PLAN_ULTRA_20X, "20x",   "Ultra 20x · 30TB (g1.play.ai.pro)",     "Ultra 20x" }
};

const G2yPlanOption *runner_plan_options(size_t *out_count) {
    if (out_count) *out_count = G2Y_PLAN_COUNT;
    return PLAN_PUBLIC;
}

G2yPlan runner_plan_from_id(const char *id) {
    if (!id || !*id) return G2Y_PLAN_ALL;
    for (size_t i = 0; i < G2Y_PLAN_COUNT; i++) {
        if (strcmp(PLAN_TABLE[i].opt.id, id) == 0) return PLAN_TABLE[i].opt.plan;
    }
    return G2Y_PLAN_ALL;
}

const G2yPlanOption *runner_plan_info(G2yPlan plan) {
    if (plan < 0 || plan >= G2Y_PLAN_COUNT) plan = G2Y_PLAN_ALL;
    return &PLAN_PUBLIC[plan];
}

static const char *trim_view(const char *s, size_t *out_len) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    if (out_len) *out_len = len;
    return s;
}

static void copy_trimmed(char *dst, size_t dst_cap, const char *src, size_t len) {
    while (len > 0 && isspace((unsigned char)*src)) { src++; len--; }
    while (len > 0 && isspace((unsigned char)src[len - 1])) len--;
    if (len >= dst_cap) len = dst_cap - 1;
    if (len > 0) memcpy(dst, src, len);
    dst[len] = '\0';
}

static G2yPlan infer_mapped_plan(const char *sku, const char *offer_id) {
    if (offer_id && *offer_id) {
        if (strcmp(offer_id, "12months-eft") == 0 || strcmp(offer_id, "eft-12month") == 0) {
            return G2Y_PLAN_PRO12;
        }
        if (strcmp(offer_id, "6months-eft") == 0) return G2Y_PLAN_PRO6;
        if (strcmp(offer_id, "6months-eft-2") == 0) return G2Y_PLAN_PRO6B;
        if (strcmp(offer_id, "6months-eft-ss-2026") == 0) return G2Y_PLAN_PRO6C;
    }
    if (sku && *sku) {
        if (strcmp(sku, "g1.play.20tb") == 0) return G2Y_PLAN_ULTRA_5X;
        if (strcmp(sku, "g1.play.ai.pro") == 0 || strcmp(sku, "g1.ai.pro") == 0 ||
            strcmp(sku, "g1.30tb") == 0) {
            return G2Y_PLAN_ULTRA_20X;
        }
        if (strstr(sku, "12months_eft") != NULL) return G2Y_PLAN_PRO12;
    }
    return G2Y_PLAN_COMBO;
}

static int format_period(const char *raw, char *out, size_t out_cap) {
    if (!raw || !*raw || strcmp(raw, "-") == 0) {
        out[0] = '\0';
        return 0;
    }
    int years = 0, months = 0, days = 0;
    if (raw[0] == 'P') {
        const char *p = raw + 1;
        while (*p && isdigit((unsigned char)*p)) {
            int val = (int)strtol(p, (char **)&p, 10);
            if (*p == 'Y') { years = val; p++; }
            else if (*p == 'M') { months = val; p++; }
            else if (*p == 'D') { days = val; p++; }
            else break;
        }
        int total_m = years * 12 + months;
        if (total_m > 0) {
            snprintf(out, out_cap, "+%dm", total_m);
            return total_m;
        }
        if (days > 0) {
            snprintf(out, out_cap, "+%dd", days);
            return 0;
        }
    }
    snprintf(out, out_cap, "%s", raw);
    return 0;
}

static void format_micros(const char *raw, char *out, size_t out_cap, bool *is_free) {
    char digits[32];
    size_t j = 0;
    bool numeric = raw && *raw;
    for (size_t i = 0; raw && raw[i] && j + 1 < sizeof(digits); i++) {
        if (raw[i] == ',') continue;
        if (isdigit((unsigned char)raw[i]) || (i == 0 && raw[i] == '-')) {
            digits[j++] = raw[i];
        } else {
            numeric = false;
            break;
        }
    }
    digits[j] = '\0';

    if (numeric && j > 0) {
        long long val = strtoll(digits, NULL, 10);
        if (val == 0) {
            *is_free = true;
            snprintf(out, out_cap, "FREE");
            return;
        }
        *is_free = false;
        double unit = (double)val / 1000000.0;
        snprintf(out, out_cap, "%.2f", unit);
        return;
    }
    *is_free = false;
    snprintf(out, out_cap, "%s", (raw && *raw) ? raw : "?");
}

static bool parse_scanner_row(const char *line, G2yParsedPlan *out) {
    /* Expected: `<sku> | <offer_id> | <period> | <micros> | <title>` */
    const char *seps[4];
    const char *cur = line;
    for (int i = 0; i < 4; i++) {
        const char *hit = strstr(cur, " | ");
        if (!hit) return false;
        seps[i] = hit;
        cur = hit + 3;
    }
    char sku[96], offer[96], period[24], micros[32], title[128];
    copy_trimmed(sku, sizeof(sku), line, (size_t)(seps[0] - line));
    copy_trimmed(offer, sizeof(offer), seps[0] + 3, (size_t)(seps[1] - (seps[0] + 3)));
    copy_trimmed(period, sizeof(period), seps[1] + 3, (size_t)(seps[2] - (seps[1] + 3)));
    copy_trimmed(micros, sizeof(micros), seps[2] + 3, (size_t)(seps[3] - (seps[2] + 3)));
    copy_trimmed(title, sizeof(title), seps[3] + 3, strlen(seps[3] + 3));

    if (!*sku || strcmp(sku, "SKU") == 0 || sku[0] == '-') return false;

    memset(out, 0, sizeof(*out));
    snprintf(out->sku, sizeof(out->sku), "%s", sku);
    snprintf(out->offer_id, sizeof(out->offer_id), "%s", *offer ? offer : "-");
    out->duration_months = format_period(period, out->period_label, sizeof(out->period_label));
    format_micros(micros, out->price_label, sizeof(out->price_label), &out->is_free);
    if (*title && strcmp(title, "-") != 0) {
        snprintf(out->detail, sizeof(out->detail), "%s · %s", sku, title);
    } else {
        snprintf(out->detail, sizeof(out->detail), "%s", sku);
    }
    out->mapped_plan = infer_mapped_plan(out->sku, out->offer_id);
    return true;
}

static bool parse_combo_step_row(const char *line, G2yParsedPlan *out) {
    /* Expected: `1. [New] g1... / offer_id / +12 months -> 2027-11-25` */
    const char *p = line;
    if (!isdigit((unsigned char)*p)) return false;
    while (*p && isdigit((unsigned char)*p)) p++;
    if (p[0] != '.' || p[1] != ' ' || p[2] != '[') return false;

    const char *rbr = strchr(p + 3, ']');
    if (!rbr || rbr[1] != ' ') return false;

    char kind[24];
    copy_trimmed(kind, sizeof(kind), p + 3, (size_t)(rbr - (p + 3)));
    const char *rest = rbr + 2;

    const char *s1 = strstr(rest, " / ");
    if (!s1) return false;
    const char *s2 = strstr(s1 + 3, " / ");
    if (!s2) return false;

    char sku[96], offer[96], tail[96];
    copy_trimmed(sku, sizeof(sku), rest, (size_t)(s1 - rest));
    copy_trimmed(offer, sizeof(offer), s1 + 3, (size_t)(s2 - (s1 + 3)));
    copy_trimmed(tail, sizeof(tail), s2 + 3, strlen(s2 + 3));

    memset(out, 0, sizeof(*out));
    out->is_step = true;
    out->is_free = true;
    snprintf(out->sku, sizeof(out->sku), "%s", sku);
    snprintf(out->offer_id, sizeof(out->offer_id), "%s", offer);
    snprintf(out->price_label, sizeof(out->price_label), "%s", *kind ? kind : "PLAN");

    const char *arrow = strstr(tail, " -> ");
    if (arrow) {
        char dur[24], date[32];
        copy_trimmed(dur, sizeof(dur), tail, (size_t)(arrow - tail));
        copy_trimmed(date, sizeof(date), arrow + 4, strlen(arrow + 4));
        int mo = (int)strtol(dur, NULL, 10);
        out->duration_months = mo > 0 ? mo : 0;
        if (mo > 0) snprintf(out->period_label, sizeof(out->period_label), "+%dm", mo);
        snprintf(out->detail, sizeof(out->detail), "%s · until %s", sku, date);
    } else {
        snprintf(out->detail, sizeof(out->detail), "%s · %s", sku, tail);
    }
    out->mapped_plan = infer_mapped_plan(out->sku, out->offer_id);
    return true;
}

static bool parse_legacy_offer_row(const char *line, G2yParsedPlan *out) {
    /* Expected: `offer <id> -> <micros> (<note>)` */
    if (strncmp(line, "offer ", 6) != 0) return false;
    const char *arrow = strstr(line + 6, " -> ");
    if (!arrow) return false;

    char offer[96], rhs[64];
    copy_trimmed(offer, sizeof(offer), line + 6, (size_t)(arrow - (line + 6)));
    copy_trimmed(rhs, sizeof(rhs), arrow + 4, strlen(arrow + 4));

    char micros[32], note[48] = {0};
    const char *lp = strchr(rhs, '(');
    if (lp) {
        copy_trimmed(micros, sizeof(micros), rhs, (size_t)(lp - rhs));
        const char *rp = strchr(lp + 1, ')');
        if (rp && rp > lp + 1) {
            copy_trimmed(note, sizeof(note), lp + 1, (size_t)(rp - (lp + 1)));
        }
    } else {
        copy_trimmed(micros, sizeof(micros), rhs, strlen(rhs));
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->offer_id, sizeof(out->offer_id), "%s", offer);
    snprintf(out->sku, sizeof(out->sku), "%s", "g1.2tb.ai.subs_variant");
    format_micros(micros, out->price_label, sizeof(out->price_label), &out->is_free);
    if (*note) {
        snprintf(out->detail, sizeof(out->detail), "%s · %s", offer, note);
    } else {
        snprintf(out->detail, sizeof(out->detail), "%s", offer);
    }
    out->mapped_plan = infer_mapped_plan(out->sku, out->offer_id);
    return true;
}

bool runner_parse_plan_line(const char *line, G2yParsedPlan *out) {
    if (!line || !out) return false;
    size_t len = 0;
    const char *trimmed = trim_view(line, &len);
    if (len == 0 || len >= 512) return false;

    char copy[512];
    memcpy(copy, trimmed, len);
    copy[len] = '\0';

    if (parse_scanner_row(copy, out)) return true;
    if (parse_combo_step_row(copy, out)) return true;
    if (parse_legacy_offer_row(copy, out)) return true;
    return false;
}

static bool path_check(const char *dir, const char *rel, int mode) {
    char full[PATH_MAX];
    int n = snprintf(full, sizeof(full), "%s/%s", dir, rel);
    if (n < 0 || (size_t)n >= sizeof(full)) return false;
    return access(full, mode) == 0;
}

void runner_detect_env(const char *repo_dir, G2yEnvStatus *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!repo_dir || !*repo_dir) return;

    out->repo_ok = path_check(repo_dir, "scripts/run.sh", X_OK) &&
                   path_check(repo_dir, "scripts/setup.sh", X_OK);
    out->has_venv = path_check(repo_dir, ".venv/bin/python", X_OK);
    out->has_agent = path_check(repo_dir, "agent/agent.iife.js", R_OK);

    if (path_check(repo_dir, "tools/platform-tools/adb", X_OK)) {
        out->has_adb = true;
    } else {
        const char *home = getenv("HOME");
        if (home && path_check(home, "Android/Sdk/platform-tools/adb", X_OK)) {
            out->has_adb = true;
        } else {
            gchar *sys_adb = g_find_program_in_path("adb");
            if (sys_adb) {
                out->has_adb = true;
                g_free(sys_adb);
            }
        }
    }
}

char **runner_build_argv(const char *repo_dir, G2yAction action, G2yPlan plan,
                         const char *account, bool open_app) {
    if (!repo_dir || !*repo_dir) return NULL;
    if (plan < 0 || plan >= G2Y_PLAN_COUNT) plan = G2Y_PLAN_ALL;
    const PlanEntry *spec = &PLAN_TABLE[plan];

    GPtrArray *args = g_ptr_array_new();
    char script_path[PATH_MAX + 32];

    if (action == G2Y_ACTION_SETUP) {
        snprintf(script_path, sizeof(script_path), "%s/scripts/setup.sh", repo_dir);
        g_ptr_array_add(args, g_strdup(script_path));
    } else {
        snprintf(script_path, sizeof(script_path), "%s/scripts/run.sh", repo_dir);
        g_ptr_array_add(args, g_strdup(script_path));

        switch (action) {
            case G2Y_ACTION_CHECK:
                g_ptr_array_add(args, g_strdup("check"));
                break;
            case G2Y_ACTION_SHOW_PLANS:
                g_ptr_array_add(args, g_strdup("scan"));
                g_ptr_array_add(args, g_strdup("query"));
                g_ptr_array_add(args, g_strdup("--timeout"));
                g_ptr_array_add(args, g_strdup("10"));
                g_ptr_array_add(args, g_strdup("--skus"));
                for (size_t i = 0; i < spec->scan_sku_count; i++) {
                    g_ptr_array_add(args, g_strdup(spec->scan_skus[i]));
                }
                break;
            case G2Y_ACTION_PLAN:
                g_ptr_array_add(args, g_strdup("combo"));
                g_ptr_array_add(args, g_strdup("plan"));
                g_ptr_array_add(args, g_strdup("--device"));
                if (account && *account) {
                    g_ptr_array_add(args, g_strdup("--account"));
                    g_ptr_array_add(args, g_strdup(account));
                }
                break;
            case G2Y_ACTION_RUN:
                if (strcmp(spec->run_cmd, "combo") == 0) {
                    g_ptr_array_add(args, g_strdup("combo"));
                    g_ptr_array_add(args, g_strdup("run"));
                    g_ptr_array_add(args, g_strdup("--device"));
                    g_ptr_array_add(args, g_strdup("--execute"));
                    g_ptr_array_add(args, g_strdup("--yes"));
                    if (account && *account) {
                        g_ptr_array_add(args, g_strdup("--account"));
                        g_ptr_array_add(args, g_strdup(account));
                    }
                    if (open_app) {
                        g_ptr_array_add(args, g_strdup("--open-app"));
                    }
                } else if (strncmp(spec->run_cmd, "step", 4) == 0) {
                    g_ptr_array_add(args, g_strdup(spec->run_cmd));
                } else {
                    g_ptr_array_add(args, g_strdup("ultra"));
                    g_ptr_array_add(args, g_strdup(spec->run_cmd));
                }
                break;
            default:
                break;
        }
    }
    g_ptr_array_add(args, NULL);
    return (char **)g_ptr_array_free(args, FALSE);
}

size_t runner_strip_ansi(const char *in, size_t in_len, char *out, size_t out_cap) {
    if (!out || out_cap == 0) return 0;
    if (!in || in_len == 0) {
        out[0] = '\0';
        return 0;
    }

    size_t w = 0;
    size_t i = 0;
    while (i < in_len && w + 1 < out_cap) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0x1B && i + 1 < in_len && in[i + 1] == '[') {
            i += 2;
            while (i < in_len) {
                unsigned char seq = (unsigned char)in[i++];
                if (seq >= 0x40 && seq <= 0x7E) break;
            }
            continue;
        }
        if (c == '\r') {
            i++;
            continue;
        }
        out[w++] = (char)c;
        i++;
    }
    out[w] = '\0';
    return w;
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool contains_wait_prompt(const char *text) {
    return strstr(text, "Press Enter after completing payment") != NULL ||
           strstr(text, "Press Enter") != NULL;
}

static void drain_fd(G2yProcess *proc, int fd) {
    if (fd < 0) return;
    char raw[2048];
    char clean[2048];

    for (;;) {
        ssize_t n = read(fd, raw, sizeof(raw) - 1);
        if (n > 0) {
            raw[n] = '\0';
            size_t len = runner_strip_ansi(raw, (size_t)n, clean, sizeof(clean));
            if (len > 0 && proc->on_output) {
                proc->on_output(clean, contains_wait_prompt(clean), proc->user_data);
            }
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static void close_fd_slot(int *fd_ptr, guint *watch_ptr) {
    if (watch_ptr && *watch_ptr > 0) {
        g_source_remove(*watch_ptr);
        *watch_ptr = 0;
    }
    if (fd_ptr && *fd_ptr >= 0) {
        close(*fd_ptr);
        *fd_ptr = -1;
    }
}

static gboolean on_pipe_ready(gint fd, GIOCondition cond, gpointer user_data) {
    G2yProcess *proc = (G2yProcess *)user_data;
    if (!proc) return G_SOURCE_REMOVE;

    drain_fd(proc, fd);

    if ((cond & (G_IO_HUP | G_IO_ERR)) != 0) {
        if (fd == proc->out_fd) {
            proc->out_watch = 0;
            close(proc->out_fd);
            proc->out_fd = -1;
        } else if (fd == proc->err_fd) {
            proc->err_watch = 0;
            close(proc->err_fd);
            proc->err_fd = -1;
        }
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void on_child_reaped(GPid pid, gint wait_status, gpointer user_data) {
    G2yProcess *proc = (G2yProcess *)user_data;
    if (!proc) return;

    drain_fd(proc, proc->out_fd);
    drain_fd(proc, proc->err_fd);

    close_fd_slot(&proc->out_fd, &proc->out_watch);
    close_fd_slot(&proc->err_fd, &proc->err_watch);
    if (proc->stdin_fd >= 0) {
        close(proc->stdin_fd);
        proc->stdin_fd = -1;
    }

    g_spawn_close_pid(pid);
    proc->pid = 0;
    proc->child_watch = 0;
    proc->running = false;

    int exit_code = 1;
    bool signaled = false;
    if (WIFEXITED(wait_status)) {
        exit_code = WEXITSTATUS(wait_status);
    } else if (WIFSIGNALED(wait_status)) {
        signaled = true;
        exit_code = 128 + WTERMSIG(wait_status);
    }

    if (proc->on_done) {
        proc->on_done(exit_code, signaled, proc->user_data);
    }
}

bool runner_spawn(G2yProcess *proc, const char *repo_dir, G2yAction action, G2yPlan plan,
                  const char *account, bool open_app,
                  G2yOutputCb on_output, G2yDoneCb on_done, void *user_data,
                  char *err_buf, size_t err_cap) {
    if (!proc || proc->running) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "A task is already running");
        return false;
    }

    char **argv = runner_build_argv(repo_dir, action, plan, account, open_app);
    if (!argv) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "Invalid repository path");
        return false;
    }

    gchar **envp = g_get_environ();
    envp = g_environ_setenv(envp, "PYTHONUNBUFFERED", "1", TRUE);
    envp = g_environ_setenv(envp, "PYTHONIOENCODING", "utf-8", TRUE);

    GPid pid = 0;
    int in_fd = -1, out_fd = -1, err_fd = -1;
    GError *gerr = NULL;

    gboolean ok = g_spawn_async_with_pipes(
        repo_dir,
        argv,
        envp,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL,
        NULL,
        &pid,
        &in_fd,
        &out_fd,
        &err_fd,
        &gerr
    );

    g_strfreev(argv);
    g_strfreev(envp);

    if (!ok) {
        if (err_buf && err_cap) {
            snprintf(err_buf, err_cap, "%s", gerr ? gerr->message : "spawn failed");
        }
        if (gerr) g_error_free(gerr);
        return false;
    }

    set_nonblocking(out_fd);
    set_nonblocking(err_fd);

    proc->running = true;
    proc->pid = pid;
    proc->stdin_fd = in_fd;
    proc->out_fd = out_fd;
    proc->err_fd = err_fd;
    proc->on_output = on_output;
    proc->on_done = on_done;
    proc->user_data = user_data;

    proc->out_watch = g_unix_fd_add(out_fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_pipe_ready, proc);
    proc->err_watch = g_unix_fd_add(err_fd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_pipe_ready, proc);
    proc->child_watch = g_child_watch_add(pid, on_child_reaped, proc);
    return true;
}

bool runner_send_enter(G2yProcess *proc) {
    if (!proc || !proc->running || proc->stdin_fd < 0) return false;
    ssize_t wrote = write(proc->stdin_fd, "\n", 1);
    return wrote == 1;
}

void runner_stop(G2yProcess *proc) {
    if (!proc || !proc->running || proc->pid <= 0) return;
    kill((pid_t)proc->pid, SIGTERM);
}

void runner_cleanup(G2yProcess *proc) {
    if (!proc) return;
    if (proc->running && proc->pid > 0) {
        kill((pid_t)proc->pid, SIGTERM);
    }
    if (proc->child_watch > 0) {
        g_source_remove(proc->child_watch);
        proc->child_watch = 0;
    }
    close_fd_slot(&proc->out_fd, &proc->out_watch);
    close_fd_slot(&proc->err_fd, &proc->err_watch);
    if (proc->stdin_fd >= 0) {
        close(proc->stdin_fd);
        proc->stdin_fd = -1;
    }
    if (proc->pid > 0) {
        waitpid((pid_t)proc->pid, NULL, WNOHANG);
        g_spawn_close_pid(proc->pid);
        proc->pid = 0;
    }
    proc->running = false;
}
