#include <dirent.h>
#include <linux/limits.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define LIBR_IMPLEMENTATION
#include "r.h"

#include <git2.h>
#define CHECK_GIT2(r, fmt, ...) do { \
    if((r) != 0) { \
        const git_error* e = git_error_last(); \
        if(e) { \
            failwith(fmt " (%d: %s)", ##__VA_ARGS__, e->klass, e->message); \
        } else { \
            failwith(fmt, ##__VA_ARGS__); \
        } \
    } \
} while(0)

struct {
    pid_t child;
    sigset_t sm;

    int input_fd;
    struct termios ttyprev;
    int output_fd;

    int quiet;

    int action_argc;
    char** action_argv;
} state;

struct watch {
    int wd;
    char* fn;
    struct watch* next;
};

struct watch* ws = NULL;

static int watch(int fd, const char* path)
{
    int r = inotify_add_watch(fd, path, IN_MODIFY);
    if(r < 0 && errno == ENOENT) {
        warning("ignoring missing file: %s", path);
        return 0;
    }
    CHECK_IF(r < 0, "inotify_add_watch(%s)", path);
    return r;
}

static void add_file(int fd, const char* path)
{
    int wd = watch(fd, path);

    struct watch** w = &ws;
    while(*w != NULL) {
        if(strcmp(path, (*w)->fn) == 0) {
            return;
        }
        w = &(*w)->next;
    }
    *w = calloc(1, sizeof(struct watch)); CHECK_MALLOC(w);
    (*w)->wd = wd;
    (*w)->fn = strndup(path, PATH_MAX); CHECK_MALLOC((*w)->fn);

    debug("watching file: %s", path);
}

static void walk_dir(int fd, git_repository* repo, const char* root, const char* dir)
{
    char path[PATH_MAX];
    int r = snprintf(path, sizeof(path), "%s%s", root, dir);
    if(r >= sizeof(path)) {
        failwith("truncated path (%zu): %s%s", sizeof(path), root, dir);
    }

    DIR* d = opendir(path);
    CHECK_NOT(d, NULL, "opendir(%s)", path);

    while(1) {
        errno = 0;
        struct dirent* de = readdir(d);
        if(de == NULL && errno == 0) break;
        CHECK_NOT(de, NULL, "readdir(%s)", path);

        int f = 0, d = 0;
        if(de->d_type == DT_UNKNOWN) {
            not_implemented();
        } else {
            if(de->d_type == DT_REG) {
                f = 1;
            } else if(de->d_type == DT_DIR) {
                d = 1;
            }
        }

        if(f) {
            char rel[PATH_MAX];
            int r = snprintf(rel, sizeof(rel), "%s%s", dir, de->d_name);
            if(r >= sizeof(rel)) {
                failwith("truncated path (%zu): %s%s", sizeof(rel), dir, de->d_name);
            }

            char abs[PATH_MAX];
            r = snprintf(abs, sizeof(abs), "%s%s", root, rel);
            if(r >= sizeof(abs)) {
                failwith("truncated path (%zu): %s%s", sizeof(abs), root, rel);
            }

            if(strcmp(de->d_name, ".k") == 0) {
                add_file(fd, abs);
                continue;
            }

            int i = 1;
            r = git_ignore_path_is_ignored(&i, repo, rel);
            CHECK_GIT2(r, "git_ignore_path_is_ignored(%s)", rel);
            if(i == 0) add_file(fd, abs);
        } else if(d) {
            char rel[PATH_MAX];
            int r = snprintf(rel, sizeof(rel), "%s%s/", dir, de->d_name);
            if(r >= sizeof(rel)) {
                failwith("truncated rel (%zu): %s%s", sizeof(rel), dir, de->d_name);
            }

            int i = 1;
            r = git_ignore_path_is_ignored(&i, repo, rel);
            CHECK_GIT2(r, "git_ignore_path_is_ignored(%s)", rel);
            if(i == 0) walk_dir(fd, repo, root, rel);
        }
    }
}

static void files(int fd, const char* path)
{
    int r = git_libgit2_init();
    CHECK_GIT2(r < 0, "git_libgit2_init");

    git_buf p = {0};
    r = git_repository_discover(&p, path, 0, NULL);
    CHECK_GIT2(r, "git_repository_discover(%s)", path);

    git_repository* gr;
    git_repository_open(&gr, p.ptr);
    CHECK_GIT2(r, "git_repository_open(%s)", p.ptr);

    const char* wd = git_repository_workdir(gr);

    git_index* idx;
    r = git_repository_index(&idx, gr);
    CHECK_GIT2(r, "git_repository_index");

    for(int i = 0; i < git_index_entrycount(idx); i++) {
        const git_index_entry* e = git_index_get_byindex(idx, i);

        char fn[PATH_MAX];
        r = snprintf(fn, sizeof(fn), "%s%s", wd, e->path);
        if(r >= sizeof(fn)) {
            failwith("truncated path (%zu): %s%s", sizeof(fn), wd, e->path);
        }

        add_file(fd, fn);
    }

    walk_dir(fd, gr, wd, "");
}

static void trigger_action(const char* type)
{
    if(state.child != 0) {
        info("action triggered while child is still running");
        return;
    }

    state.child = fork(); CHECK(state.child, "fork");
    if(state.child == 0) {
        if(state.input_fd >= 0) {
            int r = close(state.input_fd);
            CHECK(r, "close");

            // Some applications don't like having fd 0 unallocated
            if(state.input_fd == 0) {
                r = devnull(O_RDONLY);
                if(r != 0) {
                    failwith("unable to fill fd 0 with dummy fd (%d)", r);
                }
            }
        }

        int r = sigprocmask(SIG_UNBLOCK, &state.sm, NULL);
        CHECK_IF(r != 0, "sigprocmask");

        char** argv = calloc(1 + state.action_argc, sizeof(const char*));
        CHECK_MALLOC(argv);
        for(size_t i = 0; i < state.action_argc; i++) {
            argv[i] = strdup(state.action_argv[i]);
            CHECK_MALLOC(argv[i]);
        }

        r = setenv("CONTINUOUSLY", "1", 1);
        CHECK(r, "setenv(CONTINUOUSLY, 1, 1)");

        r = execvp(argv[0], argv);
        CHECK(r, "execv");
    }

    info("spawed action: %d", state.child);

    if(!state.quiet && state.output_fd >= 0) {
        dprintf(state.output_fd, "[%s trigger]\n", type);
    }
}

static void setup_term(int in, int out)
{
    int r = tcgetattr(in, &state.ttyprev);
    if(r == -1 && errno == ENOTTY) {
        state.input_fd = -1;
    } else {
        CHECK(r, "tcgetattr");

        state.input_fd = in;

        struct termios ta;
        memcpy(&ta, &state.ttyprev, sizeof(ta));
        ta.c_lflag &= ~(ECHO | ICANON);
        r = tcsetattr(in, TCSANOW, &ta);
        CHECK(r, "tcsetattr");
    }

    struct termios ta;
    r = tcgetattr(out, &ta);
    if(r == -1 && errno == ENOTTY) {
        state.output_fd = -1;
        state.quiet = 1;
    } else {
        CHECK(r, "tcgetattr");
        state.output_fd = out;
    }
}

static void restore_term(void)
{
    if(state.input_fd < 0) return;

    int r = tcsetattr(state.input_fd, TCSANOW, &state.ttyprev);
    CHECK(r, "tcsetattr");
}

static void quit(const char* reason, int ec, int child_sig)
{
    if(!state.quiet && state.output_fd >= 0) {
        dprintf(state.output_fd, "[quit]\n");
    }

    if(state.child != 0) {
        info("signalling running action (%d): %s",
             state.child, strsignal(child_sig));
        int r = kill(state.child, child_sig);
        CHECK(r, "kill(%d, %s)", state.child, strsignal(child_sig));
    }

    restore_term();

    if(reason) {
        info("exiting (%s): %d", reason, ec);
    } else {
        info("exiting: %d", ec);
    }

    exit(ec);
}

static void handle_inotify(int fd)
{
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    struct inotify_event* e = (struct inotify_event*)buf;

    ssize_t s = read(fd, buf, sizeof(buf));
    CHECK_IF(s < 0, "read");
    if(s < sizeof(*e) || s != sizeof(*e) + e->len) {
        failwith("unexpected partial read");
    }

    if(e->len) {
        debug("inotify_event: name=%s", e->name);
    }

    struct watch* w = ws;
    while(w != NULL) {
        if(w->wd == e->wd) {
            info("file trigger: %s", w->fn);
            trigger_action("file");
            w->wd = watch(fd, w->fn);
            return;
        }
        w = w->next;
    }

    failwith("unexpected watch descriptor");
}

static void handle_signalfd(int fd)
{
    struct signalfd_siginfo si;
    ssize_t s = read(fd, &si, sizeof(si));
    CHECK_IF(s < 0, "read");
    if(s != sizeof(si)) {
        failwith("unexpected partial read");
    }

    if(si.ssi_signo == SIGCHLD) {
        if(state.child == 0) {
            failwith("unexpected SIGCHLD signal");
        }

        int ws;
        pid_t r = waitpid(state.child, &ws, WNOHANG);
        CHECK_IF(r != state.child, "waitpid(%d)", state.child);

        if(WIFEXITED(ws)) {
            info("action (%d) exited: %d", state.child, WEXITSTATUS(ws));

            if(!state.quiet && state.output_fd >= 0) {
                if(WEXITSTATUS(ws) == 0) {
                    dprintf(state.output_fd, "[wait] ");
                } else {
                    dprintf(state.output_fd, "[%d] [wait] ", WEXITSTATUS(ws));
                }
                tcdrain(state.output_fd);
            }
        } else if(WIFSIGNALED(ws)) {
            info("action (%d) signaled: %d", state.child, WTERMSIG(ws));

            if(!state.quiet && state.output_fd >= 0) {
                dprintf(state.output_fd, "[%s] [wait] ",
                        strsignal(WTERMSIG(ws)));
                tcdrain(state.output_fd);
            }
        } else {
            failwith("unexpected waitpid (%d) status: %d", state.child, ws);
        }

        state.child = 0;
    } else if(si.ssi_signo == SIGINT
              || si.ssi_signo == SIGQUIT
              || si.ssi_signo == SIGTERM) {
        quit(strsignal(si.ssi_signo), 0, si.ssi_signo);
    }
}

static void handle_stdin(int fd)
{
    char buf[128];
    ssize_t r = read(fd, buf, sizeof(buf));
    if(r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
    }
    CHECK(r, "read");

    for(size_t i = 0; i < r; i++) {
        if(buf[i] == '\n') {
            trigger_action("manual");
        } else if(buf[i] == 'q' || buf[i] == 'Q'
                  || buf[i] == 'x' || buf[i] == 'X'
                  || buf[i] == 27) {
            quit(NULL, 0, SIGINT);
        }
    }

    handle_stdin(fd);
}

static void print_usage(int fd, const char* prog)
{
    dprintf(fd, "usage: %s [OPTION] [--] COMMAND [ARG]...\n", prog);
    dprintf(fd, "\n");
    dprintf(fd, "Run command when files change\n");
    dprintf(fd, "\n");
    dprintf(fd, "options:\n");
    dprintf(fd, "  -h  show this text\n");
    dprintf(fd, "  -q  keep quiet about event and state transitions\n");
    dprintf(fd, "  --  stop processing arguments\n");
}

int main(int argc, char* argv[])
{
    state.child = 0;
    state.quiet = 0;

    int res;
    while((res = getopt(argc, argv, "hq-")) != -1) {
        switch(res) {
        case 'q':
            state.quiet = 1;
            break;
        case '-':
            goto args_parsed;
        case 'h':
        default:
            print_usage(res == 'h' ? 1 : 2, argv[0]);
            exit(res == 'h' ? 0 : 1);
        }
    }

args_parsed:
    if(argc <= optind) {
        print_usage(2, argv[0]);
        return 1;
    }
    state.action_argc = argc - optind;
    state.action_argv = &argv[optind];

    int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    CHECK(ifd, "inotify_init1");
    files(ifd, ".");

    sigemptyset(&state.sm);
    sigaddset(&state.sm, SIGINT);
    sigaddset(&state.sm, SIGQUIT);
    sigaddset(&state.sm, SIGTERM);
    sigaddset(&state.sm, SIGCHLD);
    int sfd = signalfd(-1, &state.sm, SFD_NONBLOCK | SFD_CLOEXEC);
    CHECK(sfd, "signalfd");

    set_blocking(0, 0);
    setup_term(0, 1);

    int r = sigprocmask(SIG_BLOCK, &state.sm, NULL);
    CHECK_IF(r != 0, "sigprocmask");

    struct pollfd fds[3];
    nfds_t nfds = 2;
    fds[0].fd = ifd;
    fds[0].events = POLLIN;
    fds[1].fd = sfd;
    fds[1].events = POLLIN;

    if(state.input_fd >= 0) {
        nfds += 1;
        fds[2].fd = state.input_fd;
        fds[2].events = POLLIN;
    }

    if(!state.quiet) {
        dprintf(state.output_fd, "[wait] ");
        tcdrain(state.output_fd);
    }

    while(1) {
        int r = poll(fds, nfds, -1);
        CHECK(r, "poll");

        if(fds[0].revents & POLLIN) {
            handle_inotify(fds[0].fd);
            fds[0].revents &= ~POLLIN;
        }

        if(fds[1].revents & POLLIN) {
            handle_signalfd(fds[1].fd);
            fds[1].revents &= ~POLLIN;
        }

        if(state.input_fd >= 0) {
            if(fds[2].revents & POLLIN) {
                handle_stdin(fds[2].fd);
            }
            fds[2].revents &= ~POLLIN;
        }

        for(size_t i = 0; i < LENGTH(fds); i++) {
            if(fds[i].revents != 0) {
                failwith("unhandled poll events: "
                         "fds[%zu] = { .fd = %d, .revents = %hd }",
                         i, fds[i].fd, fds[i].revents);
            }
        }
    }

    return 0;
}
