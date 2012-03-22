#define _GNU_SOURCE
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "tracy.h"
#include "ll.h"

/* For __NR_<SYSCALL> */
#include <sys/syscall.h>

/* For MAX_PATH */
#include <limits.h>

/* For O_RDONLY */
#include <fcntl.h>

FILE *logs[1024];

int fc;

int log_open(struct tracy_event *e) {
    int fd;
    char *s;

    if (e->child->pre_syscall) {

        if (e->args.a1 == O_RDONLY) {
            printf("Opened read only\n");
            return 0;
        }

        s = malloc(sizeof(char) * PATH_MAX);
        tracy_read_mem(e->child, (void*)s, (void*)e->args.a0, sizeof(char) * PATH_MAX);

        printf("Opening %s.\n", s);
        free(s);
        return 0;
    }

    fd = e->args.return_code;
    if (fd < 1)
        return 0;

    if (e->args.a1 == O_RDONLY) {
        return 0;
    }

    s = malloc(sizeof(char) * 15);
    snprintf(s, 15, "%04d%06d.txt", fd, fc++);

    logs[fd] = fopen(s, "w+");
    printf("Opening for fd %d\n", fd);
    free(s);

    return 0;
}

int log_socket(struct tracy_event *e) {
    int fd;
    char *s;

    if (e->child->pre_syscall)
        return 0;

    fd = e->args.return_code;
    if (fd < 1)
        return 0;

    s = malloc(sizeof(char) * 15);
    snprintf(s, 15, "%04d%06d.txt", fd, fc++);

    logs[fd] = fopen(s, "w+");
    printf("Opening for fd (socket) %d\n", fd);
    free(s);

    return 0;
}

int log_close(struct tracy_event *e) {
    int fd;
    if (!e->child->pre_syscall)
        return 0;

    if (e->args.return_code != 0) {
        printf("Close returned != 0\n");
        return 0;
    }

    fd = e->args.a0;

    printf("close call for fd: %d\n", fd);

    if (!logs[fd]) {
        printf("Application bug: closing already closed fd\n");
        return 0;
    }

    fprintf(logs[fd], "Closing.\n");
    fclose(logs[fd]);
    logs[fd] = NULL;

    return 0;
}

int log_write(struct tracy_event *e) {
    int len, fd;
    char* str;

    if (!e->child->pre_syscall)
        return 0;

    fd = e->args.a0;

    len = e->args.a2;
    str = malloc(sizeof(char) * len);

    tracy_read_mem(e->child, (void*)str, (void*)e->args.a1, sizeof(char) * len);

    /* printf("write call for fd: %ld, str: %s, len: %ld\n",
            e->args.a0, str, e->args.a2); */

    /* printf("Going to write: %s\n", str); */
    fwrite(str, sizeof(char), len, logs[fd]);

    free(str);

    return 0;
}

int main(int argc, char** argv) {
    struct tracy *tracy;

    fc = 0;
    logs[1] = fopen("stdout.txt", "w+");
    logs[2] = fopen("stderr.txt", "w+");

    tracy = tracy_init(TRACY_TRACE_CHILDREN);

    if (argc < 2) {
        printf("Usage: loggy <program name> <program arguments>\n");
        return EXIT_FAILURE;
    }

    if (tracy_set_hook(tracy, "write", log_write)) {
        printf("failed to hook write syscall.\n");
        return EXIT_FAILURE;
    }

    if (tracy_set_hook(tracy, "open", log_open)) {
        printf("failed to hook open syscall.\n");
        return EXIT_FAILURE;
    }

    if (tracy_set_hook(tracy, "socket", log_socket)) {
        printf("failed to hook open syscall.\n");
        return EXIT_FAILURE;
    }

    if (tracy_set_hook(tracy, "close", log_close)) {
        printf("failed to hook close syscall.\n");
        return EXIT_FAILURE;
    }

    argv++; argc--;
    if (!fork_trace_exec(tracy, argc, argv)) {
        perror("fork_trace_exec returned NULL");
        return EXIT_FAILURE;
    }

    tracy_main(tracy);

    tracy_free(tracy);

    return 0;
}
