#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT 128
#define MAX_ARGS 16
#define MAXIMUM_PATH 4096

/* List of builtins to autocomplete */
static char *builtin_commands[] = {
    "echo",
    "exit",
    NULL
};

/* Generator function for readline completion */
static char *builtin_generator(const char *text, int state) {
    static int list_index, len;
    char *name;

    if (state == 0) {
        list_index = 0;
        len = strlen(text);
    }

    while ((name = builtin_commands[list_index]) != NULL) {
        list_index++;
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

/* Completion entry point */
static char **builtin_completion(const char *text, int start, int end) {
    /* Only autocomplete the first word */
    if (start == 0) {
        return rl_completion_matches(text, builtin_generator);
    }
    return NULL;
}

int is_builtin(const char *cmd) {
    return strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "cd") == 0 ||
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
            if (c == '\'') {
                in_single_quote = 0;
            } else {
                arg_buf[buf_index++] = c;
            }
        } else if (in_double_quote) {
            if (c == '\\') {
                if (input[i + 1] == '"' || input[i + 1] == '\\') {
                    arg_buf[buf_index++] = input[i + 1];
                    i++;
                } else {
                    arg_buf[buf_index++] = '\\';
                }
            } else if (c == '"') {
                in_double_quote = 0;
            } else {
                arg_buf[buf_index++] = c;
            }
        } else {
            if (c == '\\') {
                if (input[i + 1] != '\0') {
                    arg_buf[buf_index++] = input[i + 1];
                    i++;
                } else {
                    arg_buf[buf_index++] = '\\';
                }
            } else if (c == '\'' && !in_double_quote) {
                in_single_quote = 1;
            } else if (c == '"' && !in_single_quote) {
                in_double_quote = 1;
            } else if (c == ' ' || c == '\t') {
                if (buf_index > 0) {
                    arg_buf[buf_index] = '\0';
                    args[arg_index++] = strdup(arg_buf);
                    buf_index = 0;
                }
            } else {
                arg_buf[buf_index++] = c;
            }
        }
        i++;
    }

    if (buf_index > 0) {
        arg_buf[buf_index] = '\0';
        args[arg_index++] = strdup(arg_buf);
    }

    args[arg_index] = NULL;
}

void handle_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1] != NULL) {
            printf(" ");
        }
    }
    printf("\n");
}

void handle_type(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }

    if (is_builtin(args[1])) {
        printf("%s is a shell builtin\n", args[1]);
        return;
    }

    char *path_env = getenv("PATH");
    if (!path_env) {
        fprintf(stderr, "PATH not set\n");
        return;
    }

    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH];
    int found = 0;

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);
        if (access(full_path, X_OK) == 0) {
            printf("%s is %s\n", args[1], full_path);
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }

    if (!found) {
        printf("%s: not found\n", args[1]);
    }

    free(path_copy);
}

void handle_pwd() {
    char full_path[MAXIMUM_PATH];
    if (getcwd(full_path, sizeof(full_path)) != NULL) {
        printf("%s\n", full_path);
    } else {
        perror("getcwd failed");
    }
}

void handle_cd(char **args) {
    char resolved_path[MAXIMUM_PATH];
    char *target_path = NULL;

    if (args[1] == NULL || strcmp(args[1], "~") == 0) {
        target_path = getenv("HOME");
        if (!target_path) {
            fprintf(stderr, "cd: HOME not set\n");
            return;
        }
    } else {
        target_path = args[1];
    }

    if (realpath(target_path, resolved_path) == NULL) {
        fprintf(stderr, "cd: %s: No such file or directory\n", target_path);
        return;
    }

    if (chdir(resolved_path) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", target_path);
    }
}

void run_external_cmd(char **args) {
    char *path_env = getenv("PATH");
    if (!path_env) {
        fprintf(stderr, "PATH not set\n");
        return;
    }

    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH];
    int found = 0;

    while (dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
        if (access(full_path, X_OK) == 0) {
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }

    if (!found) {
        fprintf(stderr, "%s: command not found\n", args[0]);
        free(path_copy);
        return;
    }

    pid_t pid = fork();

    if (pid == 0) {
        execv(full_path, args);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
    }

    free(path_copy);
}

void free_args(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        free(args[i]);
    }
}

int main() {
    setbuf(stdout, NULL);
    char input[MAX_INPUT];
    char *args[MAX_ARGS];

    /* Setup readline completion */
    rl_readline_name = "mysh";
    rl_attempted_completion_function = builtin_completion;
    rl_completion_append_character = ' ';

    while (1) {
        char *line = readline("$ ");
        if (line == NULL) {
            break;
        }
        if (*line) {
            add_history(line);
        }
        strncpy(input, line, sizeof(input) - 1);
        input[sizeof(input) - 1] = '\0';
        free(line);

        parse_input(input, args);

        if (args[0] == NULL) {
            continue;
        }

        /* Detect and setup redirection */
        int redirect_fd_num = 0;
        int append_mode = 0;
        int redirect_index = -1;
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

        /* Execute commands */
        if (strcmp(args[0], "echo") == 0) {
            handle_echo(args);
        } else if (strcmp(args[0], "type") == 0) {
            handle_type(args);
        } else if (strcmp(args[0], "pwd") == 0) {
            handle_pwd();
        } else if (strcmp(args[0], "cd") == 0) {
            handle_cd(args);
        } else if (strcmp(args[0], "exit") == 0 && (args[1] == NULL || strcmp(args[1], "0") == 0)) {
            free_args(args);
            if (redirect_file) { dup2(original_fd, redirect_fd_num); close(original_fd); close(redirect_fd); }
            exit(0);
        } else {
            run_external_cmd(args);
        }

        /* Restore fds if redirected */
        if (redirect_file) {
            dup2(original_fd, redirect_fd_num);
            close(original_fd);
            close(redirect_fd);
        }

        free_args(args);
    }

    return 0;
}
