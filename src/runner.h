#ifndef G2Y_RUNNER_H
#define G2Y_RUNNER_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

#define G2Y_DEFAULT_REPO "/home/arvin/.gemini/antigravity/scratch/google-ai-pro-2y"

typedef enum {
    G2Y_ACTION_SETUP = 0,
    G2Y_ACTION_CHECK,
    G2Y_ACTION_SHOW_PLANS,
    G2Y_ACTION_PLAN,
    G2Y_ACTION_RUN
} G2yAction;

typedef enum {
    G2Y_PLAN_ALL = 0,
    G2Y_PLAN_COMBO,
    G2Y_PLAN_PRO12,
    G2Y_PLAN_PRO6,
    G2Y_PLAN_PRO6B,
    G2Y_PLAN_PRO6C,
    G2Y_PLAN_ULTRA_5X,
    G2Y_PLAN_ULTRA_20X,
    G2Y_PLAN_COUNT
} G2yPlan;

typedef struct {
    G2yPlan     plan;
    const char *id;
    const char *label;
    const char *short_name;
} G2yPlanOption;

typedef struct {
    bool is_step;
    bool is_free;
    int  duration_months;
    char sku[96];
    char offer_id[96];
    char period_label[24];
    char price_label[32];
    char detail[256];
    G2yPlan mapped_plan;
} G2yParsedPlan;

typedef struct {
    bool repo_ok;
    bool has_venv;
    bool has_agent;
    bool has_adb;
} G2yEnvStatus;

typedef void (*G2yOutputCb)(const char *text, bool waiting_enter, void *user_data);
typedef void (*G2yDoneCb)(int exit_code, bool signaled, void *user_data);

typedef struct {
    bool         running;
    GPid         pid;
    int          stdin_fd;
    int          out_fd;
    int          err_fd;
    guint        out_watch;
    guint        err_watch;
    guint        child_watch;
    G2yOutputCb  on_output;
    G2yDoneCb    on_done;
    void        *user_data;
} G2yProcess;

/* Returns static metadata table of selectable plan presets (G2Y_PLAN_COUNT entries). */
const G2yPlanOption *runner_plan_options(size_t *out_count);

/* Looks up a plan preset by string id (returns G2Y_PLAN_ALL if unknown). */
G2yPlan runner_plan_from_id(const char *id);

/* Returns metadata for one plan preset (clamps invalid values to G2Y_PLAN_ALL). */
const G2yPlanOption *runner_plan_info(G2yPlan plan);

/* Parses one stripped stdout/stderr line from scan/plan/list into out; false if not a plan row. */
bool runner_parse_plan_line(const char *line, G2yParsedPlan *out);

/* Inspects the target google-ai-pro-2y repo and host adb without spawning processes. */
void runner_detect_env(const char *repo_dir, G2yEnvStatus *out);

/* Builds argv (null-terminated heap strings via g_strdup; free with g_strfreev). */
char **runner_build_argv(const char *repo_dir, G2yAction action, G2yPlan plan,
                         const char *account, bool open_app);

/* Strips ANSI CSI sequences cleanly into out (out_cap >= 1). Returns bytes written. */
size_t runner_strip_ansi(const char *in, size_t in_len, char *out, size_t out_cap);

/* Spawns an action asynchronously with PYTHONUNBUFFERED=1 and non-blocking pipes. */
bool runner_spawn(G2yProcess *proc, const char *repo_dir, G2yAction action, G2yPlan plan,
                  const char *account, bool open_app,
                  G2yOutputCb on_output, G2yDoneCb on_done, void *user_data,
                  char *err_buf, size_t err_cap);

/* Sends a newline to the active child's stdin (unblocks combo run Enter prompt). */
bool runner_send_enter(G2yProcess *proc);

/* Sends SIGTERM to an active child if running. */
void runner_stop(G2yProcess *proc);

/* Removes GLib watches and closes descriptors on teardown. */
void runner_cleanup(G2yProcess *proc);

#endif /* G2Y_RUNNER_H */
