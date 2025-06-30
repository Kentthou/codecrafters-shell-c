#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>

#define MAX_INPUT 128
#define MAX_ARGS 16
#define MAXIMUM_PATH 4096

/* List of builtins to autocomplete */
static char *builtin_commands[] = {
    "echo", "exit", "pwd", "cd", "type", NULL
};

/* Generator function for readline completion */
static char *command_generator(const char *text, int state) {
    static char **matches = NULL;
    static int num_matches = 0;
    static int match_index = 0;

    if (state == 0) {
        for (int i = 0; i < num_matches; i++) free(matches[i]);
        free(matches);
        matches = NULL;
        num_matches = 0;

        int len = strlen(text);
        for (int i = 0; builtin_commands[i] != NULL; i++) {
            if (strncmp(builtin_commands[i], text, len) == 0) {
                matches = realloc(matches, (num_matches + 1) * sizeof(char*));
                matches[num_matches++] = strdup(builtin_commands[i]);
            }
        }

        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");
            while (dir) {
                DIR *d = opendir(dir);
                if (d) {
                    struct dirent *entry;
                    while ((entry = readdir(d)) != NULL) {
                        char full_path[MAXIMUM_PATH];
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
                        if (access(full_path, X_OK) == 0 && strncmp(entry->d_name, text, len) == 0) {
                            int already = 0;
                            for (int j = 0; j < num_matches; j++) {
                                if (strcmp(matches[j], entry->d_name) == 0) { already = 1; break; }
                            }
                            if (!already) {
                                matches = realloc(matches, (num_matches + 1) * sizeof(char*));
                                matches[num_matches++] = strdup(entry->d_name);
                            }
                        }
                    }
                    closedir(d);
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
        match_index = 0;
    }

    if (match_index < num_matches) return strdup(matches[match_index++]);
    return NULL;
}

/* Completion entry point */
static char **command_completion(const char *text, int start, int end) {
    if (start == 0) return rl_completion_matches(text, command_generator);
    return NULL;
}

int is_builtin(const char *cmd) {
    return strcmp(cmd, "echo") == 0 || strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "pwd") == 0 || strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "type") == 0;
}

void parse_input(char *input, char **args) {
    int i = 0, arg_index = 0;
    char buf[MAX_INPUT];
    int bi = 0;
    int in_sq = 0, in_dq = 0;

    while (input[i]) {
        char c = input[i];
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            else buf[bi++] = c;
        } else if (in_dq) {
            if (c == '\\' && (input[i+1] == '"' || input[i+1] == '\\')) {
                buf[bi++] = input[i+1]; i++;
            } else if (c == '"') in_dq = 0;
            else buf[bi++] = c;
        } else {
            if (c == '\\' && input[i+1]) { buf[bi++] = input[i+1]; i++; }
            else if (c == '\'' && !in_dq) in_sq = 1;
            else if (c == '"' && !in_sq) in_dq = 1;
            else if (c == ' ' || c == '\t') {
                if (bi > 0) { buf[bi] = '\0'; args[arg_index++] = strdup(buf); bi = 0; }
            } else { buf[bi++] = c; }
        }
        i++;
    }
    if (bi > 0) { buf[bi] = '\0'; args[arg_index++] = strdup(buf); }
    args[arg_index] = NULL;
}

void handle_echo(char **args) {
    for (int i = 1; args[i]; i++) {
        printf("%s", args[i]);
        if (args[i+1]) printf(" ");
    }
    printf("\n");
}

void handle_type(char **args) {
    if (!args[1]) { fprintf(stderr, "type: missing argument\n"); return; }
    if (is_builtin(args[1])) { printf("%s is a shell builtin\n", args[1]); return; }
    char *path_env = getenv("PATH"); if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
    char *pc = strdup(path_env), *dir = strtok(pc, ":");
    char full[MAXIMUM_PATH]; int found = 0;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[1]);
        if (access(full, X_OK) == 0) { printf("%s is %s\n", args[1], full); found = 1; break; }
        dir = strtok(NULL, ":");
    }
    if (!found) printf("%s: not found\n", args[1]); free(pc);
}

void handle_pwd() {
    char cwd[MAXIMUM_PATH];
    if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
    else perror("getcwd failed");
}

void handle_cd(char **args) {
    char resolved[MAXIMUM_PATH];
    char *target = (!args[1] || strcmp(args[1], "~") == 0) ? getenv("HOME") : args[1];
    if (!target) { fprintf(stderr, "cd: HOME not set\n"); return; }
    if (!realpath(target, resolved) || chdir(resolved) != 0)
        fprintf(stderr, "cd: %s: No such file or directory\n", target);
}

char* find_command_path(char *cmd) {
    char *pe = getenv("PATH"); if (!pe) return NULL;
    char *pc = strdup(pe), *dir = strtok(pc, ":");
    char full[MAXIMUM_PATH]; char *res = NULL;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) { res = strdup(full); break; }
        dir = strtok(NULL, ":");
    }
    free(pc);
    return res;
}

void run_builtin(char **args) {
    if (strcmp(args[0], "echo") == 0) handle_echo(args);
    else if (strcmp(args[0], "type") == 0) handle_type(args);
    else if (strcmp(args[0], "pwd") == 0) handle_pwd();
    else if (strcmp(args[0], "cd") == 0) handle_cd(args);
    else fprintf(stderr, "Unknown built-in: %s\n", args[0]);
}

void run_command(char **args, int input_fd, int output_fd) {
    if (input_fd != STDIN_FILENO) {
        if (dup2(input_fd, STDIN_FILENO) == -1) { perror("dup2 input"); exit(1); }
        close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
        if (dup2(output_fd, STDOUT_FILENO) == -1) { perror("dup2 output"); exit(1); }
        close(output_fd);
    }
    if (is_builtin(args[0])) {
        run_builtin(args);
        exit(0);
    } else {
        char *path = find_command_path(args[0]);
        if (path) {
            execv(path, args);
            perror("execv"); free(path);
        } else {
            fprintf(stderr, "%s: command not found\n", args[0]);
        }
        exit(1);
    }
}

void free_args(char **args) {
    for (int i = 0; args[i]; i++) free(args[i]);
}

int main() {
    setbuf(stdout, NULL);
    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    rl_readline_name = "mysh";
    rl_attempted_completion_function = command_completion;
    rl_completion_append_character = ' ';

    while (1) {
        char *line = readline("$ ");
        if (!line) break;
        if (*line) add_history(line);
        strncpy(input, line, sizeof(input)-1);
        input[sizeof(input)-1] = '\0';
        free(line);

        parse_input(input, args);
        if (!args[0]) continue;

        /* Check for multi-stage pipeline */
        int pipe_count = 0;
        for (int i = 0; args[i]; i++) if (strcmp(args[i], "|") == 0) pipe_count++;

        if (pipe_count > 0) {
            int cmd_count = pipe_count + 1;
            char **cmds[cmd_count];
            /* split commands */
            cmds[0] = args;
            int idx = 1;
            for (int i = 0; args[i]; i++) {
                if (strcmp(args[i], "|") == 0) {
                    args[i] = NULL;
                    cmds[idx++] = &args[i+1];
                }
            }
            /* create pipes */
            int pipes[pipe_count][2];
            for (int i = 0; i < pipe_count; i++) {
                if (pipe(pipes[i]) == -1) { perror("pipe"); }
            }
            /* fork each command */
            pid_t pids[cmd_count];
            for (int i = 0; i < cmd_count; i++) {
                pid_t pid = fork();
                if (pid == 0) {
                    /* child */
                    int in_fd = (i == 0) ? STDIN_FILENO : pipes[i-1][0];
                    int out_fd = (i == cmd_count-1) ? STDOUT_FILENO : pipes[i][1];
                    /* close unused fds */
                    for (int j = 0; j < pipe_count; j++) {
                        close(pipes[j][0]); close(pipes[j][1]);
                    }
                    run_command(cmds[i], in_fd, out_fd);
                } else if (pid < 0) {
                    perror("fork");
                } else {
                    pids[i] = pid;
                }
            }
            /* parent closes pipes */
            for (int i = 0; i < pipe_count; i++) {
                close(pipes[i][0]); close(pipes[i][1]);
            }
            /* wait for children */
            for (int i = 0; i < cmd_count; i++) waitpid(pids[i], NULL, 0);

        } else {
            /* No pipeline: existing logic for redirection and single commands */
            int redirect_fd_num = 0, append = 0, ridx = -1;
            for (int j = 0; args[j]; j++) {
                if (!strcmp(args[j], "2>>")) { redirect_fd_num = 2; append = 1; ridx = j; break; }
                if (!strcmp(args[j], "2>"))  { redirect_fd_num = 2; append = 0; ridx = j; break; }
                if (!strcmp(args[j], ">>")||!strcmp(args[j],"1>>")) { redirect_fd_num = 1; append = 1; ridx = j; break; }
                if (!strcmp(args[j], ">")||!strcmp(args[j],"1>"))   { redirect_fd_num = 1; append = 0; ridx = j; break; }
            }
            char *rfile = NULL; int orig_fd = -1, rdfd = -1;
            if (ridx != -1 && args[ridx+1]) {
                rfile = args[ridx+1]; args[ridx] = NULL;
                int flags = O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC);
                rdfd = open(rfile, flags, 0666);
                if (rdfd == -1) { perror("open"); free_args(args); continue; }
                orig_fd = dup(redirect_fd_num);
                if (orig_fd==-1||dup2(rdfd,redirect_fd_num)==-1) { perror("dup2"); if(orig_fd!=-1) close(orig_fd); close(rdfd); free_args(args); continue; }
            }
            /* handle builtins or external */
            if (!strcmp(args[0],"echo")) handle_echo(args);
            else if (!strcmp(args[0],"type")) handle_type(args);
            else if (!strcmp(args[0],"pwd")) handle_pwd();
            else if (!strcmp(args[0],"cd")) handle_cd(args);
            else if (!strcmp(args[0],"exit") && (!args[1]||!strcmp(args[1],"0"))) { free_args(args); if(rfile){ dup2(orig_fd,redirect_fd_num); close(orig_fd); close(rdfd);} exit(0); }
            else {
                pid_t pid = fork();
                if (pid==0) { run_command(args, STDIN_FILENO, STDOUT_FILENO); }
                else if (pid>0) waitpid(pid,NULL,0);
                else perror("fork");
            }
            if (rfile) { dup2(orig_fd, redirect_fd_num); close(orig_fd); close(rdfd); }
        }
        free_args(args);
    }
    return 0;
}
