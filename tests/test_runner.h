#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct TestCase {
    const char *name;
    int (*run)(void);
} TestCase;

/* Exec each case so sanitizer shutdown checks run and a crash cannot hide others. */
static int test_main(int argc, char **argv, const TestCase *cases, size_t count) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s --list | --all [--report path] | case\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--list") == 0 && argc == 2) {
        for (size_t i = 0; i < count; i++) puts(cases[i].name);
        return count == 0 ? 2 : 0;
    }
    if (strcmp(argv[1], "--all") != 0) {
        if (argc != 2) return 2;
        for (size_t i = 0; i < count; i++)
            if (strcmp(argv[1], cases[i].name) == 0) return cases[i].run();
        fprintf(stderr, "Unknown test case: %s\n", argv[1]);
        return 2;
    }
    FILE *report = NULL;
    if (argc == 4 && strcmp(argv[2], "--report") == 0) {
        report = fopen(argv[3], "w");
        if (!report) { perror(argv[3]); return 2; }
    } else if (argc != 2) return 2;
    if (report) fputs("[\n", report);
    size_t passed = 0;
    for (size_t i = 0; i < count; i++) {
        fflush(stdout);
        pid_t child = fork();
        if (child < 0) {
            perror("fork");
            if (report) fclose(report);
            return 2;
        }
        if (child == 0) {
            alarm(15);
            execlp(argv[0], argv[0], cases[i].name, (char *)NULL);
            perror("exec test case");
            _exit(127);
        }
        int status;
        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) continue;
            perror("waitpid");
            if (report) fclose(report);
            return 2;
        }
        int signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + signal_number;
        if (exit_code == 0) passed++;
        printf("%s %s", exit_code == 0 ? "PASS" : "FAIL", cases[i].name);
        if (signal_number) printf(" (signal %d)", signal_number);
        puts("");
        if (report)
            fprintf(report, "  {\"case\":\"%s\",\"exit_code\":%d,\"signal\":%d}%s\n",
                    cases[i].name, exit_code, signal_number, i + 1 < count ? "," : "");
    }
    if (report) {
        fputs("]\n", report);
        if (fclose(report) != 0) { perror("write report"); return 2; }
    }
    printf("%zu/%zu passed\n", passed, count);
    return count == 0 || passed != count;
}

#endif
