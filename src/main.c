#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#define MAX_INPUT   128
#define MAX_ARGS    16
#define MAX_PATH    PATH_MAX

// Check if a command name is one of our built-ins
int is_builtin(const char *cmd) {
    return strcmp(cmd, "echo") == 0
        || strcmp(cmd, "exit") == 0
        || strcmp(cmd, "pwd")  == 0
        || strcmp(cmd, "cd")   == 0
        || strcmp(cmd, "type") == 0;
}

// Free a NULL-terminated args array
void free_args(char **args) {
    for (int i = 0; args[i]; i++) {
        free(args[i]);
    }
}

// Parse input line into args[], handling quotes and backslashes
void parse_input(char *input, char **args) {
    int argi = 0, i = 0, len = strlen(input);

    while (i < len) {
        // skip whitespace
        while (i < len && (input[i] == ' ' || input[i] == '\t'))
            i++;
        if (i >= len) break;

        // start new arg
        char *buf = calloc(MAX_INPUT, 1);
        int bi = 0;
        int in_squote = 0, in_dquote = 0;

        while (i < len) {
            char c = input[i];
            // backslash escape
            if (c == '\\') {
                if (i + 1 < len) {
                    buf[bi++] = input[i+1];
                    i += 2;
                } else {
                    // stray backslash: just skip
                    i++;
                }
            }
            // single-quote toggle
            else if (c == '\'' && !in_dquote) {
                in_squote = !in_squote;
                i++;
            }
            // double-quote toggle
            else if (c == '"' && !in_squote) {
                in_dquote = !in_dquote;
                i++;
            }
            // unquoted whitespace ends arg
            else if (!in_squote && !in_dquote && (c == ' ' || c == '\t')) {
                i++;
                break;
            }
            else {
                buf[bi++] = c;
                i++;
            }
        }

        buf[bi] = '\0';
        args[argi++] = buf;
        if (argi >= MAX_ARGS-1) break;
    }

    args[argi] = NULL;
}

// Built-in: echo
void handle_echo(char **args) {
    for (int i = 1; args[i]; i++) {
        if (i > 1) fputc(' ', stdout);
        fputs(args[i], stdout);
    }
    fputc('\n', stdout);
}

// Built-in: type
void handle_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }
    if (is_builtin(args[1])) {
        printf("%s is a shell builtin\n", args[1]);
        return;
    }
    char *path = getenv("PATH");
    if (!path) {
        fprintf(stderr, "type: PATH not set\n");
        return;
    }
    char *pcopy = strdup(path);
    char *dir = strtok(pcopy, ":");
    char full[MAX_PATH];
    int found = 0;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[1]);
        if (access(full, X_OK) == 0) {
            printf("%s is %s\n", args[1], full);
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }
    if (!found) {
        printf("%s: not found\n", args[1]);
    }
    free(pcopy);
}

// Built-in: pwd
void handle_pwd(void) {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        puts(cwd);
    } else {
        perror("pwd");
    }
}

// Built-in: cd
void handle_cd(char **args) {
    const char *target = args[1];
    if (!target || strcmp(target, "~") == 0) {
        target = getenv("HOME");
        if (!target) {
            fprintf(stderr, "cd: HOME not set\n");
            return;
        }
    }
    char resolved[MAX_PATH];
    if (!realpath(target, resolved)) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return;
    }
    if (chdir(resolved) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
    }
}

// Launch external command via PATH lookup
void run_external(char **args) {
    char *path = getenv("PATH");
    if (!path) {
        fprintf(stderr, "PATH not set\n");
        return;
    }
    char *pcopy = strdup(path);
    char *dir = strtok(pcopy, ":");
    char full[MAX_PATH];
    int found = 0;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[0]);
        if (access(full, X_OK) == 0) {
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }
    free(pcopy);
    if (!found) {
        fprintf(stderr, "%s: command not found\n", args[0]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        execv(full, args);
        perror("execv");
        _exit(1);
    } else {
        waitpid(pid, NULL, 0);
    }
}

int main(void) {
    char line[MAX_INPUT];
    char *args[MAX_ARGS];

    setbuf(stdout, NULL);
    while (1) {
        fputs("$ ", stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        // strip newline
        line[strcspn(line, "\n")] = '\0';

        parse_input(line, args);
        if (!args[0]) continue;

        if (strcmp(args[0], "echo") == 0) {
            handle_echo(args);
        }
        else if (strcmp(args[0], "type") == 0) {
            handle_type(args);
        }
        else if (strcmp(args[0], "pwd") == 0) {
            handle_pwd();
        }
        else if (strcmp(args[0], "cd") == 0) {
            handle_cd(args);
        }
        else if (strcmp(args[0], "exit") == 0) {
            int code = 0;
            if (args[1]) code = atoi(args[1]);
            free_args(args);
            exit(code);
        }
        else {
            run_external(args);
        }

        free_args(args);
    }
    return 0;
}
