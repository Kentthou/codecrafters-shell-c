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
        for (int i = 0; i < num_matches; i++) { free(matches[i]); }
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
            while (dir != NULL) {
                DIR *d = opendir(dir);
                if (d) {
                    struct dirent *entry;
                    while ((entry = readdir(d)) != NULL) {
                        char full_path[MAXIMUM_PATH];
                        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);
                        if (access(full_path, X_OK) == 0 && strncmp(entry->d_name, text, len) == 0) {
                            int already_added = 0;
                            for (int j = 0; j < num_matches; j++) {
                                if (strcmp(matches[j], entry->d_name) == 0) { already_added = 1; break; }
                            }
                            if (!already_added) {
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

    if (match_index < num_matches) { return strdup(matches[match_index++]); }
    return NULL;
}

/* Completion entry point */
static char **command_completion(const char *text, int start, int end) {
    if (start == 0) { return rl_completion_matches(text, command_generator); }
    return NULL;
}

int is_builtin(const char *cmd) {
    return strcmp(cmd, "echo") == 0 || strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "pwd") == 0 || strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "type") == 0;
}

void parse_input(char *input, char **args) {
    int i = 0, arg_index = 0;
    char arg_buf[MAX_INPUT];
    int buf_index = 0;
    int in_single_quote = 0, in_double_quote = 0;

    while (input[i] != '\0') {
        char c = input[i];
        if (in_single_quote) {
            if (c == '\'') { in_single_quote = 0; } else { arg_buf[buf_index++] = c; }
        } else if (in_double_quote) {
            if (c == '\\' && (input[i + 1] == '"' || input[i + 1] == '\\')) {
                arg_buf[buf_index++] = input[i + 1]; i++;
            } else if (c == '"') { in_double_quote = 0; } else { arg_buf[buf_index++] = c; }
        } else {
            if (c == '\\' && input[i + 1] != '\0') { arg_buf[buf_index++] = input[i + 1]; i++; }
            else if (c == '\'' && !in_double_quote) { in_single_quote = 1; }
            else if (c == '"' && !in_single_quote) { in_double_quote = 1; }
            else if (c == ' ' || c == '\t') {
                if (buf_index > 0) {
                    arg_buf[buf_index] = '\0';
                    args[arg_index++] = strdup(arg_buf);
                    buf_index = 0;
                }
            } else { arg_buf[buf_index++] = c; }
        }
        i++;
    }
    if (buf_index > 0) { arg_buf[buf_index] = '\0'; args[arg_index++] = strdup(arg_buf); }
    args[arg_index] = NULL;
}

void handle_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL) { printf(" "); }
    }
    printf("\n");
}

void handle_type(char **args) {
    if (args[1] == NULL) { fprintf(stderr, "type: missing argument\n"); return; }
    if (is_builtin(args[1])) { printf("%s is a shell builtin\n", args[1]); return; }

    char *path_env = getenv("PATH");
    if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH];
    int found = 0;

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);
        if (access(full_path, X_OK) == 0) {
            printf("%s is %s\n", args[1], full_path);
            found = 1; break;
        }
        dir = strtok(NULL, ":");
    }
    if (!found) { printf("%s: not found\n", args[1]); }
    free(path_copy);
}

void handle_pwd() {
    char full_path[MAXIMUM_PATH];
    if (getcwd(full_path, sizeof(full_path)) != NULL) { printf("%s\n", full_path); }
    else { perror("getcwd failed"); }
}

void handle_cd(char **args) {
    char resolved_path[MAXIMUM_PATH];
    char *target_path = (args[1] == NULL || strcmp(args[1], "~") == 0) ? getenv("HOME") : args[1];
    if (!target_path) { fprintf(stderr, "cd: HOME not set\n"); return; }
    if (realpath(target_path, resolved_path) == NULL || chdir(resolved_path) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", target_path);
    }
}

void run_external_cmd(char **args) {
    char *path_env = getenv("PATH");
    if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH];
    int found = 0;

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
        if (access(full_path, X_OK) == 0) { found = 1; break; }
        dir = strtok(NULL, ":");
    }
    if (!found) { fprintf(stderr, "%s: command not found\n", args[0]); free(path_copy); return; }

    pid_t pid = fork();
    if (pid == 0) { execv(full_path, args); perror("execv"); exit(1); }
    else if (pid > 0) { waitpid(pid, NULL, 0); } else { perror("fork"); }
    free(path_copy);
}

void free_args(char **args) {
    for (int i = 0; args[i] != NULL; i++) { free(args[i]); }
}

/* NEW FUNCTIONS FOR PIPELINES */

/* Finds the full path of a command (e.g., "cat" -> "/bin/cat") */
char* find_command_path(char *cmd) {
    char *path_env = getenv("PATH");
    if (!path_env) return NULL;
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH];

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (access(full_path, X_OK) == 0) { free(path_copy); return strdup(full_path); }
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

/* Runs a built-in command */
void run_builtin(char **args) {
    if (strcmp(args[0], "echo") == 0) {
        handle_echo(args);
    } else if (strcmp(args[0], "type") == 0) {
        handle_type(args);
    } else if (strcmp(args[0], "pwd") == 0) {
        handle_pwd();
    } else if (strcmp(args[0], "cd") == 0) {
        handle_cd(args);
    } else {
        fprintf(stderr, "Unknown built-in: %s\n", args[0]);
    }
}

/* Runs a command (built-in or external) with specified input and output file descriptors */
void run_command(char **args, int input_fd, int output_fd) {
    if (input_fd != STDIN_FILENO) {
        if (dup2(input_fd, STDIN_FILENO) == -1) {
            perror("dup2 input");
            exit(1);
        }
        close(input_fd);
    }
    if (output_fd != STDOUT_FILENO) {
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 output");
            exit(1);
        }
        close(output_fd);
    }
    if (is_builtin(args[0])) {
        run_builtin(args);
        exit(0);
    } else {
        char *path = find_command_path(args[0]);
        if (path) {
            execv(path, args);
            perror("execv");
            free(path);
        } else {
            fprintf(stderr, "%s: command not found\n", args[0]);
        }
        exit(1);
    }
}

/* Executes two commands in a pipeline */
void execute_pipeline(char **cmd1_args, char **cmd2_args) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        run_command(cmd1_args, STDIN_FILENO, pipefd[1]);
    } else if (pid1 < 0) {
        perror("fork");
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[1]);
        run_command(cmd2_args, pipefd[0], STDOUT_FILENO);
    } else if (pid2 < 0) {
        perror("fork");
    }

    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
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
        if (line == NULL) { break; }
        if (*line) { add_history(line); }
        strncpy(input, line, sizeof(input) - 1);
        input[sizeof(input) -  1] = '\0';
        free(line);

        parse_input(input, args);
        if (args[0] == NULL) { continue; }

        /* Check for pipeline */
        int pipe_index = -1;
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "|") == 0) { pipe_index = i; break; }
        }

        if (pipe_index != -1) {
            char **cmd1_args = args;
            args[pipe_index] = NULL;
            char **cmd2_args = &args[pipe_index + 1];
            execute_pipeline(cmd1_args, cmd2_args);
        } else {
            int redirect_fd_num = 0, append_mode = 0, redirect_index = -1;
            for (int j = 0; args[j] != NULL; j++) {
                if (strcmp(args[j], "2>>") == 0) { redirect_fd_num = 2; append_mode = 1; redirect_index = j; break; }
                if (strcmp(args[j], "2>") == 0)  { redirect_fd_num = 2; append_mode = 0; redirect_index = j; break; }
                if (strcmp(args[j], ">>") == 0 || strcmp(args[j], "1>>") == 0) { redirect_fd_num = 1; append_mode = 1; redirect_index = j; break; }
                if (strcmp(args[j], ">") == 0 || strcmp(args[j], "1>") == 0)   { redirect_fd_num = 1; append_mode = 0; redirect_index = j; break; }
            }
            char *redirect_file = NULL;
            int original_fd = -1, redirect_fd = -1;
            if (redirect_index != -1 && args[redirect_index + 1] != NULL) {
                redirect_file = args[redirect_index + 1];
                args[redirect_index] = NULL;
                int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
                redirect_fd = open(redirect_file, flags, 0666);
                if (redirect_fd == -1) { perror("open"); free_args(args); continue; }
                original_fd = dup(redirect_fd_num);
                if (original_fd == -1 || dup2(redirect_fd, redirect_fd_num) == -1) {
                    perror("dup2"); close(redirect_fd); if (original_fd != -1) close(original_fd); free_args(args); continue;
                }
            }

            if (strcmp(args[0], "echo") == 0) { handle_echo(args); }
            else if (strcmp(args[0], "type") == 0) { handle_type(args); }
            else if (strcmp(args[0], "pwd") == 0) { handle_pwd(); }
            else if (strcmp(args[0], "cd") == 0) { handle_cd(args); }
            else if (strcmp(args[0], "exit") == 0 && (args[1] == NULL || strcmp(args[1], "0") == 0)) {
                free_args(args);
                if (redirect_file) { dup2(original_fd, redirect_fd_num); close(original_fd); close(redirect_fd); }
                exit(0);
            } else { run_external_cmd(args); }

            if (redirect_file) {
                dup2(original_fd, redirect_fd_num);
                close(original_fd);
                close(redirect_fd);
            }
        }

        free_args(args);
    }
    return 0;
}