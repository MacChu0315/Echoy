#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gsp_sim_bridge.h"
#include "echoy_ui.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    int directory, socket;
    bool removed, export_available, owns_socket;
    unsigned export_counter;
    dev_t socket_device;
    ino_t socket_inode;
} pc_context_t;

static echoy_ui_handle_t state;
static pc_context_t context = {.directory = -1, .socket = -1};
static unsigned transaction;
static const char *const roles[] = {"pulse", "voice", "dust", "harmony"};
static const char *const kinds[] = {"kick", "rim", "hat", "tone", "grain", "chord"};
static const char *const layers[] = {"anchor", "echo", "ending", "support", "detail"};
static const char *const articulations[] = {"", "short", "broken", "answer"};

static FILE *begin_write(pc_context_t *pc, const char *name, char temporary[80])
{
    snprintf(temporary, 80, ".%s.%ld.%u.tmp", name, (long)getpid(), ++transaction);
    int fd = openat(pc->directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                    0600);
    if (fd < 0) {
        return NULL;
    }
    FILE *file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        unlinkat(pc->directory, temporary, 0);
    }
    return file;
}

static bool finish_write(pc_context_t *pc, FILE *file, const char *temporary, const char *name)
{
    bool ok = !ferror(file) && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) {
        ok = false;
    }
    if (ok && renameat(pc->directory, temporary, pc->directory, name) == 0) {
        return true;
    }
    unlinkat(pc->directory, temporary, 0);
    return false;
}

static bool write_status(pc_context_t *pc)
{
    if (pc->directory < 0 || pc->socket < 0) {
        return false;
    }
    char temporary[80];
    FILE *file = begin_write(pc, "status", temporary);
    if (!file) {
        return false;
    }
    fprintf(file, "{\"removed\":%s,\"export_counter\":%u,\"export_available\":%s}\n",
            pc->removed ? "true" : "false", pc->export_counter,
            pc->export_available ? "true" : "false");
    return finish_write(pc, file, temporary, "status.json");
}

static void close_context(pc_context_t *pc)
{
    if (pc->socket >= 0) {
        close(pc->socket);
    }
    if (pc->directory >= 0) {
        struct stat current;
        if (pc->owns_socket &&
            fstatat(pc->directory, "command.sock", &current, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISSOCK(current.st_mode) && current.st_dev == pc->socket_device &&
            current.st_ino == pc->socket_inode) {
            unlinkat(pc->directory, "command.sock", 0);
        }
        close(pc->directory);
    }
    *pc = (pc_context_t){.directory = -1, .socket = -1};
}

static void open_context(pc_context_t *pc)
{
    const char *path = getenv("ECHOY_SIM_CONTROL_DIR");
    if (!path || !*path) {
        return;
    }
    pc->directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat directory;
    if (pc->directory < 0 || fstat(pc->directory, &directory) != 0 ||
        directory.st_uid != getuid() || (directory.st_mode & 077) != 0 ||
        (directory.st_mode & 0700) != 0700) {
        fprintf(stderr, "Echoy PC controls disabled: CONTROL_DIR must be an existing private 0700 "
                        "directory owned by this user.\n");
        close_context(pc);
        return;
    }
    pc->socket = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    /* Resolve within the open directory, even if an ancestor is renamed. Never
     * unlink a pre-existing socket: it may belong to another running preview. */
    snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/command.sock",
             pc->directory);
    if (pc->socket < 0 || bind(pc->socket, (struct sockaddr *)&address, sizeof(address)) != 0) {
        fprintf(stderr, "Echoy PC controls disabled: cannot bind command socket (%s).\n",
                strerror(errno));
        close_context(pc);
        return;
    }
    struct stat bound;
    if (fstatat(pc->directory, "command.sock", &bound, AT_SYMLINK_NOFOLLOW) != 0) {
        close_context(pc);
        return;
    }
    pc->owns_socket = true;
    pc->socket_device = bound.st_dev;
    pc->socket_inode = bound.st_ino;
    if (fchmodat(pc->directory, "command.sock", 0600, 0) != 0 || !write_status(pc)) {
        fprintf(stderr, "Echoy PC controls disabled: cannot initialize private status.\n");
        close_context(pc);
    }
}

static echoy_command_t poll_command(void *ctx)
{
    pc_context_t *pc = ctx;
    if (pc->socket < 0) {
        return ECHOY_COMMAND_NONE;
    }
    char message[32];
    ssize_t count = recv(pc->socket, message, sizeof(message) - 1, MSG_DONTWAIT | MSG_TRUNC);
    if (count <= 0 || (size_t)count >= sizeof(message)) {
        return ECHOY_COMMAND_NONE;
    }
    message[count] = '\0';
    static const struct {
        const char *text;
        echoy_command_t command;
    } commands[] = {
        {"restore", ECHOY_COMMAND_RESTORE},     {"fault", ECHOY_COMMAND_AUDIO_FAULT},
        {"loading", ECHOY_COMMAND_LOADING},     {"recover", ECHOY_COMMAND_RECOVER},
        {"empty", ECHOY_COMMAND_BATTERY_EMPTY},
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if ((size_t)count == strlen(commands[i].text) &&
            !memcmp(message, commands[i].text, (size_t)count)) {
            return commands[i].command;
        }
    }
    return ECHOY_COMMAND_NONE;
}

static void set_removed(void *ctx, bool removed)
{
    pc_context_t *pc = ctx;
    if (pc->removed == removed) {
        return;
    }
    pc->removed = removed;
    if (pc->socket >= 0 && !write_status(pc)) {
        fprintf(stderr, "Echoy PC status write failed.\n");
    }
}

static void write_seed_note(FILE *file, const echoy_seed_note_t *note)
{
    double duration = note->kind == ECHOY_KICK ? .22 : note->kind == ECHOY_RIM ? .09 : .045;
    fprintf(file, "{\"step\":%.17g,\"kind\":\"%s\",\"vel\":%.17g,\"dur\":%.17g,\"origin\":\"you\"}",
            note->step, kinds[note->kind], note->velocity, duration);
}

static void write_pitches(FILE *file, const int *notes, unsigned count)
{
    fputc('[', file);
    for (unsigned i = 0; i < count; ++i) {
        fprintf(file, "%s%d", i ? "," : "", notes[i]);
    }
    fputc(']', file);
}

static void write_note(FILE *file, const echoy_note_t *note, echoy_role_t role)
{
    fprintf(file,
            "{\"step\":%.17g,\"kind\":\"%s\",\"vel\":%.17g,\"dur\":%.17g,"
            "\"layer\":\"%s\",\"origin\":\"%s\",\"expression\":%.17g,\"sourceStep\":",
            note->step, kinds[note->kind], note->velocity, note->duration, layers[note->layer],
            note->layer == ECHOY_ANCHOR ? "you" : "partner", note->expression);
    if (note->has_source_step) {
        fprintf(file, "%.17g", note->source_step);
    } else {
        fputs("null", file);
    }
    if (role == ECHOY_PULSE) {
        fprintf(file, ",\"role\":\"%s\"", layers[note->layer]);
    }
    if (note->kind >= ECHOY_TONE) {
        fprintf(file, ",\"note\":%d", note->note);
    }
    if (note->note_count) {
        fputs(",\"notes\":", file);
        write_pitches(file, note->notes, note->note_count);
    }
    if (note->harmony_count) {
        fputs(",\"harmony\":", file);
        write_pitches(file, note->harmony, note->harmony_count);
    }
    if (note->articulation != ECHOY_ARTICULATION_NONE) {
        fprintf(file, ",\"articulation\":\"%s\"", articulations[note->articulation]);
    }
    if (note->max_duration) {
        fprintf(file, ",\"maxDuration\":%.17g", note->max_duration);
    }
    fputc('}', file);
}

static void write_plan(FILE *file, const echoy_plan_t *plan)
{
    if (!plan) {
        fputs("null", file);
        return;
    }
    fprintf(file,
            "{\"role\":\"%s\",\"phrase\":%u,\"density\":%.17g,\"duration\":%.17g,\"source\":[",
            roles[plan->role], plan->phrase, plan->density, plan->duration);
    for (size_t i = 0; i < plan->source_count; ++i) {
        if (i) {
            fputc(',', file);
        }
        write_seed_note(file, &plan->source[i]);
    }
    fputs("],\"foreground\":[", file);
    bool first = true;
    for (size_t i = 0; i < plan->count; ++i) {
        if (plan->events[i].layer != ECHOY_ANCHOR) {
            continue;
        }
        if (!first) {
            fputc(',', file);
        }
        write_note(file, &plan->events[i], plan->role);
        first = false;
    }
    fputs("],\"full\":[", file);
    for (size_t i = 0; i < plan->count; ++i) {
        if (i) {
            fputc(',', file);
        }
        write_note(file, &plan->events[i], plan->role);
    }
    fputs("]}", file);
}

static bool valid_plan(const echoy_plan_t *plan)
{
    if (!plan) {
        return true;
    }
    if ((unsigned)plan->role > ECHOY_HARMONY || plan->phrase < 1 || plan->phrase > 3 ||
        plan->count > ECHOY_MAX_EVENTS || plan->source_count > 16 || !isfinite(plan->density) ||
        plan->density < 0 || plan->density > 1 || !isfinite(plan->duration)) {
        return false;
    }
    for (size_t i = 0; i < plan->source_count; ++i) {
        const echoy_seed_note_t *n = &plan->source[i];
        if ((unsigned)n->kind > ECHOY_HAT || !isfinite(n->step) || !isfinite(n->velocity)) {
            return false;
        }
    }
    for (size_t i = 0; i < plan->count; ++i) {
        const echoy_note_t *n = &plan->events[i];
        if ((unsigned)n->kind > ECHOY_CHORD || (unsigned)n->layer > ECHOY_DETAIL ||
            (unsigned)n->articulation > ECHOY_ANSWER || n->note_count > 3 || n->harmony_count > 3 ||
            !isfinite(n->step) || !isfinite(n->velocity) || !isfinite(n->duration) ||
            !isfinite(n->max_duration) || !isfinite(n->expression) ||
            (n->has_source_step && !isfinite(n->source_step))) {
            return false;
        }
    }
    return true;
}

static bool export_session(void *ctx, const echoy_snapshot_t *snapshot, const echoy_plan_t *plan,
                           double volume)
{
    pc_context_t *pc = ctx;
    if (pc->socket < 0 || !snapshot || !isfinite(volume) || volume < 0 || volume > 1 ||
        (unsigned)snapshot->role > ECHOY_HARMONY || !isfinite(snapshot->density) ||
        !valid_plan(plan)) {
        return false;
    }
    char temporary[80];
    FILE *file = begin_write(pc, "session", temporary);
    if (!file) {
        return false;
    }
    fprintf(file,
            "{\"study\":\"Echoy Arrange B 06\",\"simulated\":true,\"edges\":[],\"tiles\":{\"A\":{"
            "\"role\":\"%s\",\"phrase\":%u,\"density\":%.17g,\"volume\":%.17g,\"loop\":%s,"
            "\"planPending\":%s,\"revision\":%u,\"committedRevision\":%u,\"ownSeed\":[",
            roles[snapshot->role], snapshot->phrase, snapshot->density, volume,
            snapshot->continuous ? "true" : "false", snapshot->plan_pending ? "true" : "false",
            snapshot->revision, snapshot->committed_revision);
    bool first = true;
    for (unsigned step = 0; step < 16; ++step) {
        if (!(snapshot->seed & (1u << step))) {
            continue;
        }
        if (!first) {
            fputc(',', file);
        }
        echoy_seed_note_t note = {.step = step, .velocity = .74, .kind = ECHOY_KICK};
        write_seed_note(file, &note);
        first = false;
    }
    fputs("],\"current\":", file);
    write_plan(file, plan);
    fputs("}},\"limitations\":[\"audio synthesis not implemented\",\"multi-device transport not "
          "implemented\","
          "\"single PC tile; no wireless synchronization\",\"16-step rhythm quantization\"]}\n",
          file);
    if (!finish_write(pc, file, temporary, "session.json")) {
        return false;
    }
    bool previous_available = pc->export_available;
    pc->export_available = true;
    ++pc->export_counter;
    if (write_status(pc)) {
        return true;
    }
    --pc->export_counter;
    pc->export_available = previous_available;
    return false;
}

static uint16_t initial_seed(void)
{
    const char *text = getenv("ECHOY_SIM_SEED");
    if (!text || !*text) {
        return 0;
    }
    const char *digits = text;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        digits += 2;
    }
    if (!*digits) {
        return 0;
    }
    for (const char *p = digits; *p; ++p) {
        if (!isxdigit((unsigned char)*p)) {
            return 0;
        }
    }
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 16);
    return errno || *end || value > UINT16_MAX ? 0 : (uint16_t)value;
}

static echoy_condition_t initial_condition(void)
{
    const char *text = getenv("ECHOY_SIM_CONDITION");
    if (!text) {
        return ECHOY_NORMAL;
    }
    if (!strcmp(text, "fault")) {
        return ECHOY_AUDIO_FAULT;
    }
    if (!strcmp(text, "loading")) {
        return ECHOY_LOADING;
    }
    if (!strcmp(text, "empty")) {
        return ECHOY_BATTERY_EMPTY;
    }
    return ECHOY_NORMAL;
}

static int64_t now_us(void *ctx)
{
    (void)ctx;
    return (int64_t)gsp_sim_bridge_time_ms() * 1000;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    open_context(&context);
    const echoy_platform_t platform = {
        .now_us = now_us,
        .ctx = &context,
        .is_simulator = true,
        .reduced_motion = getenv("ECHOY_REDUCED_MOTION") && !strcmp(getenv("ECHOY_REDUCED_MOTION"),"1"),
        .battery_percent = 72,
        .initial_seed = initial_seed(),
        .initial_condition = initial_condition(),
        .poll_command = poll_command,
        .set_removed = set_removed,
        .export_session = export_session,
    };
    fprintf(stderr, "Echoy UI: C phrase generation; no audio synthesis or wireless.\n");
    const esp_gsp_err_t err = echoy_ui_create(ui, &platform, &state);
    if (err == ESP_GSP_OK) {
        fprintf(stderr, "ECHOY_READY\n");
    } else if (!state) {
        close_context(&context);
    }
    return err;
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    fprintf(stderr, "Echoy UI last_error=%d\n", (int)echoy_ui_get_error(state));
    const esp_gsp_err_t err = echoy_ui_delete(state);
    fprintf(stderr, "Echoy UI cleanup_error=%d\n", (int)err);
    if (err == ESP_GSP_OK) {
        state = NULL;
        close_context(&context);
    }
}
