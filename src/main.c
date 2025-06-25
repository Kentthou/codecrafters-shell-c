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

// List of builtin commands for completion
const char *builtin_names[] = {"echo", "exit", "pwd", "cd", "type", NULL};

int is_builtin(const char *cmd) {
    return strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "exit") == 0 ||
           strcmp(cmd, "pwd") == 0 ||
           strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "type") == 0;
}

// Readline completion generator for builtin commands
char *command_generator(const char *text, int state) {
    static int list_index, len;
    const char *name;

    if (state == 0) {
        list_index = 0;
        len = strlen(text);
    }

    while ((name = builtin_names[list_index]) != NULL) {
        list_index++;
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

// Hook readline to use our completion
char **my_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
}

// Structure to hold command details including redirection
typedef struct {
    char **args;
    char *stdout_redir;
    int stdout_append;
    char *stderr_redir;
    int stderr_append;
} Command;

void parse_input(char *input, Command *cmd) {
    char *tokens[MAX_ARGS];
    int token_index = 0;
    char token_buf[MAX_INPUT];
    int buf_index = 0;
    int in_single_quote = 0, in_double_quote = 0;
    int i = 0;

    // Initialize command struct
    cmd->args = NULL;
    cmd->stdout_redir = NULL;
    cmd->stdout_append = 0;
    cmd->stderr_redir = NULL;
    cmd->stderr_append = 0;

    // Step 1: Split input into tokens
    while (input[i] != '\0') {
        char c = input[i];

        if (in_single_quote) {
            if (c == '\'') in_single_quote = 0;
            else token_buf[buf_index++] = c;
        } else if (in_double_quote) {
            if (c == '\\' && (input[i + 1] == '"' || input[i + 1] == '\\')) {
                token_buf[buf_index++] = input[++i];
            } else if (c == '"') in_double_quote = 0;
            else token_buf[buf_index++] = c;
        } else {
            if (c == '\\' && input[i + 1] != '\0') {
                token_buf[buf_index++] = input[++i];
            } else if (c == '\'') {
                in_single_quote = 1;
            } else if (c == '"') {
                in_double_quote = 1;
            } else if (c == ' ' || c == '\t') {
                if (buf_index > 0) {
                    token_buf[buf_index] = '\0';
                    tokens[token_index++] = strdup(token_buf);
                    buf_index = 0;
                }
            } else {
                token_buf[buf_index++] = c;
            }
        }
        i++;
    }
    if (buf_index > 0) {
        token_buf[buf_index] = '\0';
        tokens[token_index++] = strdup(token_buf);
    }
    tokens[token_index] = NULL;

    // Step 2: Process tokens into Command struct
    cmd->args = malloc(MAX_ARGS * sizeof(char *));
    int arg_index = 0;
    for (int j = 0; tokens[j] != NULL; j++) {
        if (strcmp(tokens[j], ">") == 0) {
            if (tokens[j + 1] != NULL) {
                cmd->stdout_redir = strdup(tokens[j + 1]);
                cmd->stdout_append = 0;
                j++;
            }
        } else if (strcmp(tokens[j], ">>") == 0 || strcmp(tokens[j], "1>>") == 0) {
            if (tokens[j + 1] != NULL) {
                cmd->stdout_redir = strdup(tokens[j + 1]);
                cmd->stdout_append = 1;
                j++;
            }
        } else if (strcmp(tokens[j], "2>") == 0) {
            if (tokens[j + 1] != NULL) {
                cmd->stderr_redir = strdup(tokens[j + 1]);
                cmd->stderr_append = 0;
                j++;
            }
        } else if (strcmp(tokens[j], "2>>") == 0) {
            if (tokens[j + 1] != NULL) {
                cmd->stderr_redir = strdup(tokens[j + 1]);
                cmd->stderr_append = 1;
                j++;
            }
        } else {
            cmd->args[arg_index++] = strdup(tokens[j]);
        }
    }
    cmd->args[arg_index] = NULL;

    // Free tokens
    for (int j = 0; tokens[j]; j++) free(tokens[j]);
}

void handle_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s", args[i]);
        if (args[i + 1]) printf(" ");
    }
    printf("\n");
}

void handle_type(char **args) {
    if (!args[1]) { fprintf(stderr, "type: missing argument\n"); return; }
    if (is_builtin(args[1])) { printf("%s is a shell builtin\n", args[1]); return; }
    char *path_env = getenv("PATH"); if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH]; int found = 0;
    while (dir) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);
        if (access(full_path, X_OK) == 0) { printf("%s is %s\n", args[1], full_path); found = 1; break; }
        dir = strtok(NULL, ":");
    }
    if (!found) printf("%s: not found\n", args[1]);
    free(path_copy);
}

void handle_pwd() {
    char full_path[MAXIMUM_PATH];
    if (getcwd(full_path, sizeof(full_path))) printf("%s\n", full_path);
    else perror("getcwd failed");
}

void handle_cd(char **args) {
    char resolved[MAXIMUM_PATH]; char *target = args[1] ? args[1] : getenv("HOME");
    if (!target) { fprintf(stderr, "cd: HOME not set\n"); return; }
    if (!realpath(target, resolved)) { fprintf(stderr, "cd: %s: No such file or directory\n", target); return; }
    if (chdir(resolved) != 0) fprintf(stderr, "cd: %s: No such file or directory\n", target);
}

void run_external_cmd(Command *cmd) {
    char *path_env = getenv("PATH"); if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
    char *path_copy = strdup(path_env);
    char *dir = strtok(path_copy, ":");
    char full_path[MAXIMUM_PATH]; int found = 0;
    while (dir) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd->args[0]);
        if (access(full_path, X_OK) == 0) { found = 1; break; }
        dir = strtok(NULL, ":");
    }
    if (!found) { fprintf(stderr, "%s: command not found\n", cmd->args[0]); free(path_copy); return; }
    pid_t pid = fork();
    if (pid == 0) {
        // Set up redirections in child
        if (cmd->stdout_redir) {
            int flags = O_WRONLY | O_CREAT;
            if (cmd->stdout_append) flags |= O_APPEND;
            else flags |= O_TRUNC;
            int fd = open(cmd->stdout_redir, flags, 0644);
            if (fd == -1) { perror("open"); exit(1); }
            dup2(fd, 1);
            close(fd);
        }
        if (cmd->stderr_redir) {
            int flags = O_WRONLY | O_CREAT;
            if (cmd->stderr_append) flags |= O_APPEND;
            else flags |= O_TRUNC;
            int fd = open(cmd->stderr_redir, flags, 0644);
            if (fd == -1) { perror("open"); exit(1); }
            dup2(fd, 2);
            close(fd);
        }
        execv(full_path, cmd->args);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
    }
    free(path_copy);
}

void free_command(Command *cmd) {
    for (int i = 0; cmd->args[i]; i++) free(cmd->args[i]);
    free(cmd->args);
    if (cmd->stdout_redir) free(cmd->stdout_redir);
    if (cmd->stderr_redir) free(cmd->stderr_redir);
}

int main() {
    // Enable tab completion for our builtins
    rl_attempted_completion_function = my_completion;

    while (1) {
        char *line = readline("$ ");
        if (!line) break;              // Ctrl-D
        if (*line) add_history(line);  // Add non-empty to history

        char buf[MAX_INPUT];
        strncpy(buf, line, MAX_INPUT);
        buf[MAX_INPUT - 1] = '\0';
        free(line);

        Command cmd = {0};
        parse_input(buf, &cmd);
        if (!cmd.args[0]) {
            free_command(&cmd);
            continue;
        }

        // Handle redirections for built-ins
        int saved_stdout = -1, saved_stderr = -1;
        if (cmd.stdout_redir) {
            saved_stdout = dup(1);
            int flags = O_WRONLY | O_CREAT;
            if (cmd.stdout_append) flags |= O_APPEND;
            else flags |= O_TRUNC;
            int fd = open(cmd.stdout_redir, flags, 0644);
            if (fd == -1) {
                perror("open");
                free_command(&cmd);
                continue;
            }
            dup2(fd, 1);
            close(fd);
        }
        if (cmd.stderr_redir) {
            saved_stderr = dup(2);
            int flags = O_WRONLY | O_CREAT;
            if (cmd.stderr_append) flags |= O_APPEND;
            else flags |= O_TRUNC;
            int fd = open(cmd.stderr_redir, flags, 0644);
            if (fd == -1) {
                perror("open");
                if (cmd.stdout_redir) {
                    dup2(saved_stdout, 1);
                    close(saved_stdout);
                }
                free_command(&cmd);
                continue;
            }
            dup2(fd, 2);
            close(fd);
        }

        // Execute commands
        if (strcmp(cmd.args[0], "echo") == 0) {
            handle_echo(cmd.args);
        } else if (strcmp(cmd.args[0], "type") == 0) {
            handle_type(cmd.args);
        } else if (strcmp(cmd.args[0], "pwd") == 0) {
            handle_pwd();
        } else if (strcmp(cmd.args[0], "cd") == 0) {
            handle_cd(cmd.args);
        } else if (strcmp(cmd.args[0], "exit") == 0 && (!cmd.args[1] || strcmp(cmd.args[1], "0") == 0)) {
            if (cmd.stdout_redir) {
                dup2(saved_stdout, 1);
                close(saved_stdout);
            }
            if (cmd.stderr_redir) {
                dup2(saved_stderr, 2);
                close(saved_stderr);
            }
            free_command(&cmd);
            break;
        } else {
            run_external_cmd(&cmd);
        }

        // Restore file descriptors for built-ins
        if (cmd.stdout_redir) {
            dup2(saved_stdout, 1);
            close(saved_stdout);
        }
        if (cmd.stderr_redir) {
            dup2(saved_stderr, 2);
            close(saved_stderr);
        }

        free_command(&cmd);
    }

    return 0;
}