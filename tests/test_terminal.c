#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1000000000.0;
}

/* Exercise the real curses game in a terminal of a reproducible size. */
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: %s executable\n", argv[0]); return 2; }
    int master, slave;
    struct winsize size = {.ws_row = 40, .ws_col = 120};
    if (openpty(&master, &slave, NULL, NULL, &size) < 0) { perror("openpty"); return 1; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        close(master);
        if (setsid() < 0 || ioctl(slave, TIOCSCTTY, 0) < 0) _exit(126);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) close(slave);
        setenv("TERM", "xterm-256color", 1);
        execl(argv[1], argv[1], (char *)NULL);
        _exit(127);
    }
    close(slave);
    fcntl(master, F_SETFL, O_NONBLOCK);
    size_t bytes = 0;
    int escapes = 0, sent_keys = 0, exited = 0, status = 0;
    double started = seconds();
    while (seconds() - started < 3.0) {
        struct pollfd fd = {.fd = master, .events = POLLIN};
        poll(&fd, 1, 50);
        char buffer[8192];
        ssize_t amount;
        while ((amount = read(master, buffer, sizeof buffer)) > 0) {
            bytes += amount;
            if (memchr(buffer, '\033', amount)) escapes = 1;
        }
        if (!sent_keys && seconds() - started > 1.0) {
            if (write(master, "hl3s0", 5) != 5) { perror("terminal input"); break; }
            sent_keys = 1;
        }
        pid_t ended = waitpid(child, &status, WNOHANG);
        if (ended == child) { exited = 1; break; }
        if (ended < 0 && errno != EINTR) { perror("waitpid"); break; }
    }
    if (!exited) {
        kill(child, SIGINT);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    close(master);
    int stopped = (WIFSIGNALED(status) && WTERMSIG(status) == SIGINT) ||
                  (WIFEXITED(status) && WEXITSTATUS(status) == 1);
    if (exited || !sent_keys || !escapes || bytes < 100 || !stopped) {
        fprintf(stderr, "Terminal smoke failed: early_exit=%d keys=%d bytes=%zu status=%d\n",
                exited, sent_keys, bytes, status);
        return 1;
    }
    printf("PASS terminal smoke: %zu bytes rendered; survived scripted input; stopped with SIGINT\n", bytes);
    return 0;
}
