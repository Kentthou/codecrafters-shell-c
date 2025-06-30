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

#define MY_MAX_INPUT 128  // Renamed to avoid conflict with system MAX_INPUT
#define MAX_ARGS 16
#define MAXIMUM_PATH 4096

/* Function prototypes */
char *find_command_path(char *cmd);

/* List of builtins to autocomplete */
static char *builtin_commands[] = {
    "echo", "exit", "pwd", "cd", "type", NULL
};

/* Autocomplete generator */
char *command_generator(const char *text, int state) {
    static int list_index, len;
    const char *name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while ((name = builtin_commands[list_index++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

/* Autocomplete function */
char **command_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, command_generator);
}

/* Check if command is a built-in */
int is_builtin(char *cmd) {
    if (!cmd) return 0;
    for (int i = 0; builtin_commands[i]; i++) {
        if (strcmp(cmd, builtin_commands[i]) == 0) return 1;
    }
    return 0;
}

/* Parse input into arguments */
void parse_input(char *input, char **args) {
    int i = 0;
    char *token = strtok(input, " \t\n");
    while (token && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
}

/* Handle echo command */
void handle_echo(char **args) {
    int i = 1;
    while (args[i]) {
        char *arg = args[i];
        while (*arg) {
            if (*arg == '\\' && *(arg + 1) == 'n') {
                printf("\n");
                arg += 2;
            } else {
                putchar(*arg++);
            }
        }
        if (args[i + 1]) putchar(' ');
        i++;
    }
    putchar('\n');
}

/* Handle type command */
void handle_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }
    if (is_builtin(args[1])) {
        printf("%s is a shell builtin\n", args[1]);
    } else {
        char *path = find_command_path(args[1]);
        if (path) {
            printf("%s is %s\n", args[1], path);
            free(path);
        } else {
            printf("%s: not found\n", args[1]);
        }
    }
}

/* Handle pwd command */
void handle_pwd(void) {
    char cwd[MAXIMUM_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("getcwd");
    }
}

/* Handle cd command */
void handle_cd(char **args) {
    char *dir = args[1] ? args[1] : getenv("HOME");
    if (chdir(dir) == -1) {
        perror("cd");
    }
}

/* Run external command */
void run_external_cmd(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        char *path = find_command_path(args[0]);
        if (path) {
            execv(path, args);
            free(path);
        }
        fprintf(stderr, "%s: command not found\n", args[0]);
        exit(1);
    } else if (pid > 0) {
        wait(NULL);
    } else {
        perror("fork");
    }
}

/* Free argument array */
void free_args(char **args) {
    /* Since args are pointers to input string, no need to free */
}

/* Find command in PATH */
char *find_command_path(char *cmd) {
    char *path = getenv("PATH");
    if (!path) return NULL;
    char *path_copy = strdup(path);
    char full_path[MAXIMUM_PATH];
    char *dir = strtok(path_copy, ":");
    while (dir) {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, cmd);
        if (access(full_path, X_OK) == 0) {
            free(path_copy);
            return strdup(full_path);
        }
        dir = strtok(NULL, ":");
    }
    free(path_copy);
    return NULL;
}

/* Run built-in command */
void run_builtin(char **args) {
    if (strcmp(args[0], "echo") == 0) {
        handle_echo(args);
    } else if (strcmp(args[0], "type") == 0) {
        handle_type(args);
    } else if (strcmp(args[0], "pwd") == 0) {
        handle_pwd();
    } else if (strcmp(args[0], "cd") == 0) {
        handle_cd(args);
    } else if (strcmp(args[0], "exit") == 0) {
        if (args[1] == NULL || strcmp(args[1], "0") == 0) {
            exit(0);
        }
    }
}

/* Execute pipeline */
void execute_pipeline(char **cmd1_args, char **cmd2_args) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid1 = -1, pid2 = -1;

    if (is_builtin(cmd1_args[0])) {
        int saved_stdout = dup(STDOUT_FILENO);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            close(saved_stdout);
            close(pipefd[0]);
            close(pipefd[1]);
            return;
        }
        close(pipefd[1]);
        run_builtin(cmd1_args);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    } else {
        pid1 = fork();
        if (pid1 == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            char *cmd1_path = find_command_path(cmd1_args[0]);
            if (cmd1_path) {
                execv(cmd1_path, cmd1_args);
                free(cmd1_path);
            }
            fprintf(stderr, "%s: command not found\n", cmd1_args[0]);
            exit(1);
        } else if (pid1 < 0) {
            perror("fork");
        }
    }

    if (is_builtin(cmd2_args[0])) {
        run_builtin(cmd2_args);
    } else {
        pid2 = fork();
        if (pid2 == 0) {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            char *cmd2_path = find_command_path(cmd2_args[0]);
            if (cmd2_path) {
                execv(cmd2_path, cmd2_args);
                free(cmd2_path);
            }
            fprintf(stderr, "%s: command not found\n", cmd2_args[0]);
            exit(1);
        } else if (pid2 < 0) {
            perror("fork");
        }
    }

    close(pipefd[0]);
    close(pipefd[1]);

    if (pid1 != -1) {
        waitpid(pid1, NULL, 0);
    }
    if (pid2 != -1) {
        waitpid(pid2, NULL, 0);
    }
}

int main() {
    setbuf(stdout, NULL);
    char input[MY_MAX_INPUT];  // Use renamed macro
    char *args[MAX_ARGS];

    rl_readline_name = "mysh";
    rl_attempted_completion_function = command_completion;
    rl_completion_append_character = ' ';

    while (1) {
        char *line = readline("$ ");
        if (line == NULL) break;
        if (*line) add_history(line);
        strncpy(input, line, sizeof(input) - 1);
        input[sizeof(input) - 1] = '\0';
        free(line);

        parse_input(input, args);
        if (args[0] == NULL) continue;

        int pipe_index = -1;
        for (int i = 0; args[i] != NULL; i++) {
            if (strcmp(args[i], "|") == 0) {
                pipe_index = i;
                break;
            }
        }

        if (pipe_index != -1) {
            char **cmd1_args = args;
            args[pipe_index] = NULL;
            char **cmd2_args = &args[pipe_index + 1];
            execute_pipeline(cmd1_args, cmd2_args);
        } else {
            if (is_builtin(args[0])) {
                run_builtin(args);
            } else {
                run_external_cmd(args);
            }
        }

        free_args(args);
    }
    return 0;
}