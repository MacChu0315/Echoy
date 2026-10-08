#define _GNU_SOURCE
#include "gsp_sim_bridge.h"
#include "echoy_ui.h"
#include "echoy_band.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static volatile sig_atomic_t interrupted;
struct desk;
struct tile_ctx {
    struct desk *desk;
    int id;
    const char *endpoint;
};
struct desk {
    pthread_mutex_t lock, io_lock;
    echoy_band_t *band;
    struct tile_ctx tiles[3];
    atomic_bool stopping, failed, audio_connected, audio_failed;
    atomic_int ready;
    int directory, socket;
    bool owns_socket, silent;
    dev_t socket_dev;
    ino_t socket_ino;
    const char *capture;
    unsigned export_counter;
    bool export_available;
    atomic_ullong samples_written, frames_rendered;
    atomic_llong audio_origin_us;
    atomic_uint late_blocks;
};
static const char *roles[] = {"pulse", "voice", "dust", "harmony"};
static const char *conditions[] = {"normal", "fault", "loading", "empty"};
static int64_t now_us(void *unused)
{
    (void)unused;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static void stop_signal(int sig)
{
    (void)sig;
    interrupted = 1;
}
static bool read_control(void *ctx, echoy_control_view_t *out)
{
    struct tile_ctx *t = ctx;
    pthread_mutex_lock(&t->desk->lock);
    bool ok = echoy_band_read(t->desk->band, t->id, out);
    pthread_mutex_unlock(&t->desk->lock);
    return ok;
}
static bool action(void *ctx, echoy_action_t act, double value, int64_t now)
{
    struct tile_ctx *t = ctx;
    pthread_mutex_lock(&t->desk->lock);
    bool ok = echoy_band_action(t->desk->band, t->id, act, value, now);
    pthread_mutex_unlock(&t->desk->lock);
    return ok;
}
static void removed(void *ctx, bool value)
{
    action(ctx, ECHOY_ACT_PRESENT, !value, now_us(NULL));
}
static bool export_snapshot(struct desk *d);
static bool export_from_ui(void *ctx, const echoy_snapshot_t *s, const echoy_plan_t *p,
                           double volume)
{
    (void)s;
    (void)p;
    (void)volume;
    struct tile_ctx *t = ctx;
    return export_snapshot(t->desk);
}
static void *ui_thread(void *ctx)
{
    struct tile_ctx *t = ctx;
    struct desk *d = t->desk;
    esp_gsp_handle_t gsp = NULL;
    echoy_ui_handle_t ui = NULL;
    esp_gsp_err_t e = gsp_sim_bridge_open(t->endpoint, &gsp);
    if (e == ESP_GSP_OK) {
        const echoy_platform_t p = {.ctx = t,
                                    .now_us = now_us,
                                    .is_simulator = true,
                                    .battery_percent = 72,
                                    .read_control = read_control,
                                    .perform_action = action,
                                    .set_removed = removed,
                                    .export_session = export_from_ui};
        e = echoy_ui_create(gsp, &p, &ui);
    }
    if (e == ESP_GSP_OK) {
        atomic_fetch_add(&d->ready, 1);
        fprintf(stderr, "ECHOY_DESK_UI tile=%c ready=1\n", 'A' + t->id);
    }
    while (e == ESP_GSP_OK && !atomic_load(&d->stopping)) {
        e = gsp_sim_bridge_poll(gsp, 2);
        if (e == ESP_GSP_OK) {
            e = echoy_ui_get_error(ui);
        }
    }
    if (e != ESP_GSP_OK) {
        fprintf(stderr, "ECHOY_DESK_UI_ERROR tile=%c error=%d\n", 'A' + t->id, (int)e);
        atomic_store(&d->failed, true);
        atomic_store(&d->stopping, true);
    }
    /* Stop producers first, then let accepted images publish/cancel on their
     * owner thread. close() also drains releases; retain UI context until then. */
    int64_t end = now_us(NULL) + 2000000;
    while (ui) {
        esp_gsp_err_t cleanup = echoy_ui_delete(ui);
        if (cleanup == ESP_GSP_OK) {
            ui = NULL;
            break;
        }
        if (!gsp || now_us(NULL) >= end || gsp_sim_bridge_poll(gsp, 2) != ESP_GSP_OK) {
            break;
        }
    }
    if (gsp) {
        gsp_sim_bridge_close(gsp);
    }
    if (ui) {
        echoy_ui_release_closed(ui);
    }
    fprintf(stderr, "ECHOY_DESK_UI tile=%c closed=1\n", 'A' + t->id);
    return NULL;
}
static void *prepare_thread(void *ctx)
{
    struct desk *d = ctx;
    while (!atomic_load(&d->stopping)) {
        pthread_mutex_lock(&d->lock);
        echoy_band_job_t *j = echoy_band_take_job(d->band);
        pthread_mutex_unlock(&d->lock);
        if (!j) {
            usleep(2000);
            continue;
        }
        bool ok = echoy_band_prepare_job(j);
        pthread_mutex_lock(&d->lock);
        bool accepted = echoy_band_finish_job(d->band, j, now_us(NULL));
        pthread_mutex_unlock(&d->lock);
        if (!ok) {
            fprintf(stderr, "ECHOY_AUDIO_PREPARE_FAILED\n");
        } else {
            fprintf(stderr, "ECHOY_AUDIO_PREPARED accepted=%d\n", accepted);
        }
    }
    return NULL;
}
static int audio_pipe(pid_t *pid)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC)) {
        return -1;
    }
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    char *const args[] = {"/usr/bin/paplay",
                          "--raw",
                          "--format=s16le",
                          "--rate=24000",
                          "--channels=1",
                          "--latency-msec=30",
                          "--process-time-msec=10",
                          "--client-name=Echoy",
                          "--stream-name=Echoy A+B+C",
                          NULL};
    error = posix_spawn(pid, args[0], &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[0]);
    if (error) {
        close(fds[1]);
        return -1;
    }
    if (fcntl(fds[1], F_SETFL, O_NONBLOCK) != 0) {
        close(fds[1]);
        kill(*pid, SIGTERM);
        waitpid(*pid, NULL, 0);
        return -1;
    }
    return fds[1];
}
static bool write_all(struct desk *d, int fd, const void *p, size_t n)
{
    const char *data = p;
    int64_t deadline = now_us(NULL) + 1000000;
    while (n && !atomic_load(&d->stopping)) {
        ssize_t w = write(fd, data, n);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (now_us(NULL) >= deadline) {
                return false;
            }
            struct pollfd ready = {.fd = fd, .events = POLLOUT};
            int result = poll(&ready, 1, 20);
            if (result < 0 && errno != EINTR) {
                return false;
            }
            continue;
        }
        if (w <= 0) {
            return false;
        }
        data += w;
        n -= (size_t)w;
    }
    return n == 0;
}
static void le32(unsigned char *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++) {
        p[i] = (unsigned char)(n >> (8 * i));
    }
}
static bool wave_header(FILE *f, uint32_t frames)
{
    unsigned char h[44] = {0};
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + frames * 2);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    h[20] = 1;
    h[22] = 1;
    le32(h + 24, 24000);
    le32(h + 28, 48000);
    h[32] = 2;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    le32(h + 40, frames * 2);
    return fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, 44, f) == 44 && fflush(f) == 0;
}
static bool wait_audio_process(pid_t child, int64_t deadline)
{
    for (;;) {
        int status;
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
        if (now_us(NULL) >= deadline) {
            return false;
        }
        usleep(10000);
    }
}
static bool stop_audio_process(pid_t child)
{
    if (wait_audio_process(child, now_us(NULL) + 1000000)) {
        return true;
    }
    if (kill(child, SIGTERM) != 0 && errno != ESRCH) {
        return false;
    }
    if (wait_audio_process(child, now_us(NULL) + 500000)) {
        return true;
    }
    if (kill(child, SIGKILL) != 0 && errno != ESRCH) {
        return false;
    }
    return wait_audio_process(child, now_us(NULL) + 500000);
}
static void *audio_thread(void *ctx)
{
    struct desk *d = ctx;
    pid_t child = -1;
    int fd = d->silent ? -1 : audio_pipe(&child);
    FILE *capture = d->capture ? fopen(d->capture, "wb") : NULL;
    if (d->capture && !capture) {
        perror("audio capture");
        atomic_store(&d->failed, true);
        atomic_store(&d->stopping, true);
    }
    if (capture && !wave_header(capture, 0)) {
        fprintf(stderr, "Cannot initialize WAV capture\n");
        atomic_store(&d->failed, true);
        atomic_store(&d->stopping, true);
    }
    if (!d->silent && fd < 0) {
        atomic_store(&d->audio_failed, true);
    }
    float buffer[240];
    int16_t pcm[240];
    int64_t at = now_us(NULL);
    atomic_store(&d->audio_origin_us, at);
    uint32_t captured = 0;
    while (!atomic_load(&d->stopping)) {
        pthread_mutex_lock(&d->lock);
        echoy_band_tick(d->band, at);
        echoy_band_render(d->band, at, buffer, 240);
        pthread_mutex_unlock(&d->lock);
        for (unsigned i = 0; i < 240; i++) {
            pcm[i] = (int16_t)lrint(fmax(-1, fmin(1, buffer[i])) * 32767);
        }
        if (fd >= 0) {
            if (!write_all(d, fd, pcm, sizeof(pcm))) {
                close(fd);
                fd = -1;
                atomic_store(&d->audio_connected, false);
                atomic_store(&d->audio_failed, true);
            } else {
                atomic_fetch_add(&d->samples_written, 240);
                atomic_store(&d->audio_connected, true);
            }
        }
        if (capture && (d->silent || fd >= 0)) {
            if (fwrite(pcm, sizeof(*pcm), 240, capture) != 240) {
                atomic_store(&d->failed, true);
                atomic_store(&d->stopping, true);
            }
            captured += 240;
        }
        atomic_fetch_add(&d->frames_rendered, 240);
        at += 10000;
        int64_t now = now_us(NULL);
        if (now > at + 30000) {
            atomic_fetch_add(&d->late_blocks, 1);
            at = now;
        }
        struct timespec deadline = {.tv_sec = at / 1000000, .tv_nsec = (at % 1000000) * 1000};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == EINTR &&
               !interrupted) {
        }
    }
    if (fd >= 0) {
        close(fd);
    }
    if (child > 0 && !stop_audio_process(child)) {
        fprintf(stderr, "Cannot reap audio output process\n");
        atomic_store(&d->failed, true);
    }
    atomic_store(&d->audio_connected, false);
    if (capture) {
        bool valid = wave_header(capture, captured);
        if (fclose(capture) != 0) {
            valid = false;
        }
        if (!valid) {
            fprintf(stderr, "Cannot finish WAV capture\n");
            atomic_store(&d->failed, true);
        }
    }
    return NULL;
}
static bool open_control(struct desk *d, const char *path)
{
    d->directory = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat s;
    if (d->directory < 0 || fstat(d->directory, &s) || s.st_uid != getuid() ||
        (s.st_mode & 0777) != 0700) {
        return false;
    }
    d->socket = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (d->socket < 0) {
        return false;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/command.sock",
             d->directory);
    if (bind(d->socket, (struct sockaddr *)&address, sizeof(address))) {
        return false;
    }
    if (fstatat(d->directory, "command.sock", &s, AT_SYMLINK_NOFOLLOW)) {
        return false;
    }
    d->owns_socket = true;
    d->socket_dev = s.st_dev;
    d->socket_ino = s.st_ino;
    return fchmodat(d->directory, "command.sock", 0600, 0) == 0;
}
static FILE *begin_json(struct desk *d, const char *name, char path[80])
{
    static unsigned serial;
    snprintf(path, 80, ".%s.%ld.%u.tmp", name, (long)getpid(), ++serial);
    int fd = openat(d->directory, path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        return NULL;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        unlinkat(d->directory, path, 0);
    }
    return f;
}
static bool end_json(struct desk *d, FILE *f, const char *temp, const char *name)
{
    bool ok = !ferror(f) && fflush(f) == 0;
    if (fclose(f)) {
        ok = false;
    }
    if (ok && renameat(d->directory, temp, d->directory, name) == 0) {
        return true;
    }
    unlinkat(d->directory, temp, 0);
    return false;
}
struct desk_snapshot {
    echoy_control_view_t views[3];
    echoy_arrange_tile_t tiles[3];
    echoy_arrange_edge_t edges[2];
    size_t edge_count;
    uint64_t revision;
    bool pending;
};
static struct desk_snapshot *snapshot(struct desk *d)
{
    struct desk_snapshot *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    pthread_mutex_lock(&d->lock);
    const echoy_arrange_t *m = echoy_band_arrangement(d->band);
    for (int i = 0; i < 3; i++) {
        echoy_band_read(d->band, i, &s->views[i]);
        s->tiles[i] = *echoy_arrange_get_tile(m, i);
    }
    const echoy_arrange_edge_t *e = echoy_arrange_get_edges(m, false, &s->edge_count);
    if (s->edge_count) {
        memcpy(s->edges, e, s->edge_count * sizeof(*e));
    }
    s->revision = echoy_arrange_revision(m);
    s->pending = echoy_arrange_get_pending(m) != NULL;
    pthread_mutex_unlock(&d->lock);
    return s;
}
static void edges_json(FILE *f, const struct desk_snapshot *s)
{
    fputc('[', f);
    for (size_t i = 0; i < s->edge_count; i++) {
        fprintf(f, "%s{\"left\":\"%c\",\"right\":\"%c\"}", i ? "," : "", 'A' + s->edges[i].left,
                'A' + s->edges[i].right);
    }
    fputc(']', f);
}
static bool status_locked(struct desk *d, const struct desk_snapshot *s)
{
    char temp[80];
    FILE *f = begin_json(d, "status", temp);
    if (!f) {
        return false;
    }
    fprintf(f,
            "{\"ready\":%s,\"audio_connected\":%s,\"silent\":%s,\"revision\":%llu,\"pending\":%s,"
            "\"export_counter\":%u,\"export_available\":%s,\"samples_written\":%llu,\"late_"
            "blocks\":%u,\"tiles\":{",
            atomic_load(&d->ready) == 3 ? "true" : "false",
            atomic_load(&d->audio_connected) ? "true" : "false", d->silent ? "true" : "false",
            (unsigned long long)s->revision, s->pending ? "true" : "false", d->export_counter,
            d->export_available ? "true" : "false", atomic_load(&d->samples_written),
            atomic_load(&d->late_blocks));
    for (int i = 0; i < 3; i++) {
        echoy_control_view_t v = s->views[i];
        fprintf(f,
                "%s\"%c\":{\"present\":%s,\"role\":\"%s\",\"phrase\":%u,\"volume\":%.17g,"
                "\"battery\":%u,\"condition\":\"%s\",\"phase\":%d,\"seed\":%u,\"source\":",
                i ? "," : "", 'A' + i, v.present ? "true" : "false", roles[v.state.role],
                v.state.phrase, v.volume, v.battery, conditions[v.condition], v.state.phase,
                v.state.seed);
        if (v.source >= 0) {
            fprintf(f, "\"%c\"", 'A' + v.source);
        } else {
            fputs("null", f);
        }
        fprintf(f,
                ",\"clock\":\"%c\",\"clock_origin_us\":%lld,\"start_us\":%lld,\"loop\":%s,"
                "\"events\":%zu,\"preparing\":%s,\"plan_pending\":%s,\"prepare_failed\":%s,"
                "\"density\":%.17g,\"apply_at_us\":%lld,\"english\":%s}",
                'A' + v.clock, (long long)v.state.clock_origin_us, (long long)v.state.start_us,
                v.state.continuous ? "true" : "false", v.plan.count, v.preparing ? "true" : "false",
                v.state.plan_pending ? "true" : "false", v.preparation_failed ? "true" : "false",
                v.state.density, (long long)v.state.apply_at_us, v.english ? "true" : "false");
    }
    fprintf(f,
            "},\"frames_rendered\":%llu,\"audio_origin_us\":%lld,\"status_at_us\":%lld,\"edges\":",
            atomic_load(&d->frames_rendered), atomic_load(&d->audio_origin_us),
            (long long)now_us(NULL));
    edges_json(f, s);
    fputs("}\n", f);
    return end_json(d, f, temp, "status.json");
}
static const char *note_kinds[] = {"kick", "rim", "hat", "tone", "grain", "chord"};
static void seed_json(FILE *f, const echoy_seed_note_t *seed, size_t count)
{
    fputc('[', f);
    for (size_t i = 0; i < count; i++) {
        fprintf(f,
                "%s{\"step\":%.17g,\"kind\":\"%s\",\"vel\":%.17g,\"dur\":%.17g,\"origin\":\"you\"}",
                i ? "," : "", seed[i].step, note_kinds[seed[i].kind], seed[i].velocity,
                seed[i].kind == ECHOY_KICK  ? .22
                : seed[i].kind == ECHOY_RIM ? .09
                                            : .045);
    }
    fputc(']', f);
}
static void note_json(FILE *f, const echoy_note_t *n)
{
    static const char *layers[] = {"anchor", "echo", "ending", "support", "detail"};
    static const char *articulations[] = {"", "short", "broken", "answer"};
    fprintf(f,
            "{\"step\":%.17g,\"kind\":\"%s\",\"layer\":\"%s\",\"origin\":\"%s\",\"vel\":%.17g,"
            "\"dur\":%.17g,\"expression\":%.17g,\"sourceStep\":",
            n->step, note_kinds[n->kind], layers[n->layer],
            n->layer == ECHOY_ANCHOR ? "you" : "partner", n->velocity, n->duration, n->expression);
    if (n->has_source_step) {
        fprintf(f, "%.17g", n->source_step);
    } else {
        fputs("null", f);
    }
    if (n->kind >= ECHOY_TONE) {
        fprintf(f, ",\"note\":%d", n->note);
    }
    if (n->note_count) {
        fputs(",\"notes\":[", f);
        for (unsigned j = 0; j < n->note_count; j++) {
            fprintf(f, "%s%d", j ? "," : "", n->notes[j]);
        }
        fputc(']', f);
    }
    if (n->harmony_count) {
        fputs(",\"harmony\":[", f);
        for (unsigned j = 0; j < n->harmony_count; j++) {
            fprintf(f, "%s%d", j ? "," : "", n->harmony[j]);
        }
        fputc(']', f);
    }
    if (n->articulation != ECHOY_ARTICULATION_NONE) {
        fprintf(f, ",\"articulation\":\"%s\"", articulations[n->articulation]);
    }
    if (n->max_duration) {
        fprintf(f, ",\"maxDuration\":%.17g", n->max_duration);
    }
    fputc('}', f);
}
static void plan_json(FILE *f, const echoy_arrange_plan_t *plan)
{
    if (!plan->valid) {
        fputs("null", f);
        return;
    }
    const echoy_plan_t *p = &plan->music;
    fprintf(f, "{\"role\":\"%s\",\"phrase\":%u,\"density\":%.17g,\"duration\":%.17g,\"source\":",
            roles[p->role], p->phrase, p->density, p->duration);
    if (plan->source < 0) {
        fputs("null", f);
    } else {
        fprintf(f, "\"%c\"", 'A' + plan->source);
    }
    fprintf(f, ",\"detached\":%s,\"rootSeed\":", plan->detached ? "true" : "false");
    seed_json(f, p->source, p->source_count);
    fputs(",\"foreground\":[", f);
    bool first = true;
    for (size_t i = 0; i < p->count; i++) {
        if (p->events[i].layer == ECHOY_ANCHOR) {
            if (!first) {
                fputc(',', f);
            }
            note_json(f, &p->events[i]);
            first = false;
        }
    }
    fputs("],\"full\":[", f);
    for (size_t i = 0; i < p->count; i++) {
        if (i) {
            fputc(',', f);
        }
        note_json(f, &p->events[i]);
    }
    fputs("]}", f);
}
static bool export_locked(struct desk *d, const struct desk_snapshot *s)
{
    char temp[80];
    FILE *f = begin_json(d, "session", temp);
    if (!f) {
        return false;
    }
    fputs("{\"study\":\"Echoy native linked desk\",\"simulated\":true,\"audio\":\"shared portable "
          "C at 24000 Hz\",\"edges\":",
          f);
    edges_json(f, s);
    fputs(",\"tiles\":{", f);
    for (int i = 0; i < 3; i++) {
        echoy_control_view_t v = s->views[i];
        fprintf(
            f,
            "%s\"%c\":{\"present\":%s,\"loop\":%s,\"volume\":%.17g,\"clock\":\"%c\",\"current\":",
            i ? "," : "", 'A' + i, v.present ? "true" : "false",
            v.state.continuous ? "true" : "false", v.volume, 'A' + v.clock);
        const echoy_arrange_tile_t *tile = &s->tiles[i];
        plan_json(f, &tile->current);
        fputs(",\"ownSeed\":", f);
        seed_json(f, tile->own_seed, tile->own_count);
        fprintf(f, ",\"clockOriginUs\":%lld,\"role\":\"%s\",\"phrase\":%u,\"density\":%.17g",
                (long long)v.state.clock_origin_us, roles[tile->role], tile->phrase, tile->density);
        fputc('}', f);
    }
    fputs("},\"limitations\":[\"PC shared clock, not wireless/acoustic hardware verification\"]}\n",
          f);
    if (!end_json(d, f, temp, "session.json")) {
        return false;
    }
    d->export_available = true;
    ++d->export_counter;
    return status_locked(d, s);
}
static bool write_snapshot(struct desk *d, bool export)
{
    pthread_mutex_lock(&d->io_lock);
    struct desk_snapshot *s = snapshot(d);
    bool ok = s && (export ? export_locked(d, s) : status_locked(d, s));
    free(s);
    pthread_mutex_unlock(&d->io_lock);
    return ok;
}
static bool export_snapshot(struct desk *d)
{
    return write_snapshot(d, true);
}
static int tile_id(const char *s)
{
    return s && s[0] >= 'A' && s[0] <= 'C' && !s[1] ? s[0] - 'A' : -1;
}
static bool command_locked(struct desk *d, char *message)
{
    char *words[8], *save = NULL;
    size_t n = 0;
    for (char *p = strtok_r(message, " \r\n", &save); p; p = strtok_r(NULL, " \r\n", &save)) {
        if (n == 8) {
            return false;
        }
        words[n++] = p;
    }
    if (!n) {
        return false;
    }
    int64_t now = now_us(NULL);
    if (!strcmp(words[0], "edges")) {
        if (n != 1 && n != 3 && n != 5) {
            return false;
        }
        echoy_arrange_edge_t edges[2];
        for (size_t i = 1; i < n; i += 2) {
            int a = tile_id(words[i]), b = tile_id(words[i + 1]);
            if (a < 0 || b < 0) {
                return false;
            }
            edges[(i - 1) / 2] = (echoy_arrange_edge_t){a, b};
        }
        return echoy_band_set_edges(d->band, edges, (n - 1) / 2, now);
    }
    if (!strcmp(words[0], "pause_all") && n == 1) {
        for (int i = 0; i < 3; i++) {
            echoy_band_action(d->band, i, ECHOY_ACT_PAUSE, 0, now);
        }
        return true;
    }

    if (n < 2 || tile_id(words[1]) < 0) {
        return false;
    }
    int id = tile_id(words[1]);
    if (!strcmp(words[0], "example") && n == 2) {
        return echoy_band_action(d->band, id, ECHOY_ACT_EXAMPLE, 0, now);
    }
    if (!strcmp(words[0], "condition") && n == 3) {
        for (int i = 0; i < 4; i++) {
            if (!strcmp(words[2], conditions[i])) {
                return echoy_band_action(d->band, id, ECHOY_ACT_CONDITION, i, now);
            }
        }
        return false;
    }
    if (n == 3 && (!strcmp(words[0], "present") || !strcmp(words[0], "volume"))) {
        char *end;
        errno = 0;
        double value = strtod(words[2], &end);
        if (errno || *end || !isfinite(value) || value < 0 || value > 1) {
            return false;
        }
        return echoy_band_action(
            d->band, id, !strcmp(words[0], "present") ? ECHOY_ACT_PRESENT : ECHOY_ACT_VOLUME, value,
            now);
    }
    return false;
}
int main(int argc, char **argv)
{
    struct desk d = {.lock = PTHREAD_MUTEX_INITIALIZER,
                     .io_lock = PTHREAD_MUTEX_INITIALIZER,
                     .directory = -1,
                     .socket = -1};
    const char *dir = NULL;
    uint64_t duration = 0;
    for (int i = 0; i < 3; i++) {
        d.tiles[i].desk = &d;
        d.tiles[i].id = i;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--silent")) {
            d.silent = true;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "Missing option value\n");
            return 2;
        }
        if (!strcmp(argv[i], "--endpoint-A")) {
            d.tiles[0].endpoint = argv[++i];
        } else if (!strcmp(argv[i], "--endpoint-B")) {
            d.tiles[1].endpoint = argv[++i];
        } else if (!strcmp(argv[i], "--endpoint-C")) {
            d.tiles[2].endpoint = argv[++i];
        } else if (!strcmp(argv[i], "--control-dir")) {
            dir = argv[++i];
        } else if (!strcmp(argv[i], "--capture")) {
            d.capture = argv[++i];
        } else if (!strcmp(argv[i], "--duration-ms")) {
            char *end;
            errno = 0;
            duration = strtoull(argv[++i], &end, 10);
            if (errno || *end || duration > 86400000) {
                return 2;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (!dir || !d.tiles[0].endpoint || !d.tiles[1].endpoint || !d.tiles[2].endpoint) {
        fprintf(stderr, "Three endpoints and a private --control-dir are required\n");
        return 2;
    }
    signal(SIGINT, stop_signal);
    signal(SIGTERM, stop_signal);
    signal(SIGPIPE, SIG_IGN);
    bool opened = open_control(&d, dir);
    if (!opened) {
        fprintf(stderr, "Cannot open private desk controls: %s\n", strerror(errno));
        atomic_store(&d.failed, true);
        goto cleanup;
    }
    d.band = echoy_band_create();
    if (!d.band) {
        fprintf(stderr, "Cannot initialize C instruments\n");
        atomic_store(&d.failed, true);
        goto cleanup;
    }
    pthread_t ui[3], prepare, audio;
    unsigned created = 0;
    bool prepared = false, sounded = false;
    for (int i = 0; i < 3; i++) {
        int e = pthread_create(&ui[i], NULL, ui_thread, &d.tiles[i]);
        if (e) {
            atomic_store(&d.failed, true);
            goto threads_done;
        }
        ++created;
    }
    if (pthread_create(&prepare, NULL, prepare_thread, &d)) {
        atomic_store(&d.failed, true);
        goto threads_done;
    }
    prepared = true;
    if (pthread_create(&audio, NULL, audio_thread, &d)) {
        atomic_store(&d.failed, true);
        goto threads_done;
    }
    sounded = true;
    int64_t start = now_us(NULL), last_status = 0;
    bool faulted = false;
    while (!interrupted && !atomic_load(&d.stopping) &&
           (!duration || now_us(NULL) - start < (int64_t)duration * 1000)) {
        int64_t now = now_us(NULL);
        pthread_mutex_lock(&d.lock);
        echoy_band_tick(d.band, now);
        pthread_mutex_unlock(&d.lock);
        char msg[256];
        for (unsigned i = 0; i < 32; i++) {
            ssize_t n = recv(d.socket, msg, sizeof(msg) - 1, MSG_DONTWAIT | MSG_TRUNC);
            if (n <= 0) {
                break;
            }
            if ((size_t)n >= sizeof(msg)) {
                continue;
            }
            msg[n] = 0;
            bool ok;
            if (!strcmp(msg, "export")) {
                ok = export_snapshot(&d);
            } else {
                pthread_mutex_lock(&d.lock);
                ok = command_locked(&d, msg);
                pthread_mutex_unlock(&d.lock);
            }
            if (!ok) {
                fprintf(stderr, "ECHOY_DESK_COMMAND_REJECTED\n");
            }
        }
        pthread_mutex_lock(&d.lock);
        if (atomic_load(&d.audio_failed) && !faulted) {
            for (int i = 0; i < 3; i++) {
                echoy_band_action(d.band, i, ECHOY_ACT_CONDITION, ECHOY_AUDIO_FAULT, now);
            }
            faulted = true;
        }
        pthread_mutex_unlock(&d.lock);
        if (now - last_status >= 100000) {
            if (!write_snapshot(&d, false)) {
                fprintf(stderr, "Cannot write desk status\n");
                atomic_store(&d.failed, true);
                atomic_store(&d.stopping, true);
            }
            last_status = now;
        }
        usleep(2000);
    }
threads_done:
    atomic_store(&d.stopping, true);
    if (prepared) {
        pthread_join(prepare, NULL);
    }
    if (sounded) {
        pthread_join(audio, NULL);
    }
    for (unsigned i = 0; i < created; i++) {
        pthread_join(ui[i], NULL);
    }
cleanup:
    echoy_band_delete(d.band);
    if (d.socket >= 0) {
        close(d.socket);
    }
    if (d.directory >= 0) {
        struct stat st;
        if (d.owns_socket && !fstatat(d.directory, "command.sock", &st, AT_SYMLINK_NOFOLLOW) &&
            S_ISSOCK(st.st_mode) && st.st_dev == d.socket_dev && st.st_ino == d.socket_ino) {
            unlinkat(d.directory, "command.sock", 0);
        }
        close(d.directory);
    }
    pthread_mutex_destroy(&d.lock);
    pthread_mutex_destroy(&d.io_lock);
    fprintf(stderr, "ECHOY_DESK_CLOSED error=%d samples=%llu late_blocks=%u\n",
            atomic_load(&d.failed), atomic_load(&d.samples_written), atomic_load(&d.late_blocks));
    return atomic_load(&d.failed) ? 1 : 0;
}
