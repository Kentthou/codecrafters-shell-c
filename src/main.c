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

typedef struct {
    char *args[MAX_ARGS];
    char *stdout_redir;
    char *stderr_redir;
    int stdout_append;
    int stderr_append;
} Command;

void free_command(Command *cmd) {
    for (int i = 0; cmd->args[i]; i++) free(cmd->args[i]);
    free(cmd->stdout_redir);
    free(cmd->stderr_redir);
    memset(cmd, 0, sizeof(Command));
}

void parse_input(char *input, Command *cmd) {
    char *token = strtok(input, " ");
    int arg_index = 0;
    while (token && arg_index < MAX_ARGS - 1) {
        if (strcmp(token, ">>") == 0) {
            cmd->stdout_redir = strdup(strtok(NULL, " "));
            cmd->stdout_append = 1;
        } else if (strcmp(token, ">") == 0) {
            cmd->stdout_redir = strdup(strtok(NULL, " "));
            cmd->stdout_append = 0;
        } else if (strcmp(token, "2>>") == 0) {
            cmd->stderr_redir = strdup(strtok(NULL, " "));
            cmd->stderr_append = 1;
        } else if (strcmp(token, "2>") == 0) {
            cmd->stderr_redir = strdup(strtok(NULL, " "));
            cmd->stderr_append = 0;
        } else {
            cmd->args[arg_index++] = strdup(token);
        }
        token = strtok(NULL, " ");
    }
    cmd->args[arg_index] = NULL;
}

void run_external_cmd(Command *cmd) {
    pid_t pid = fork();
    if (pid == 0) { // Child
        if (cmd->stdout_redir) {
            int flags = O_WRONLY | O_CREAT | (cmd->stdout_append ? O_APPEND : O_TRUNC);
            int fd = open(cmd->stdout_redir, flags, 0644);
            if (fd == -1) { perror("open"); exit(1); }
            dup2(fd, 1);
            close(fd);
        }
        if (cmd->stderr_redir) {
            int flags = O_WRONLY | O_CREAT | (cmd->stderr_append ? O_APPEND : O_TRUNC);
            int fd = open(cmd->stderr_redir, flags, 0644);
            if (fd == -1) { perror("open"); exit(1); }
            dup2(fd, 2);
            close(fd);
        }
        execvp(cmd->args[0], cmd->args);
        perror("execvp");
        exit(1);
    } else if (pid > 0) { // Parent
        wait(NULL);
    } else {
        perror("fork");
    }
}

char *builtin_generator(const char *text, int state) {
    static int list_index, len;
    const char *builtins[] = {"echo", "exit", NULL};
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    const char *name;
    while ((name = builtins[list_index++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    return NULL;
}

char **my_completion(const char *text, int start, int end) {
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, builtin_generator);
}

int main() {
    // Set readline to output to /dev/tty instead of stdout
    FILE *tty = fopen("/dev/tty", "w");
    if (tty == NULL) {
        perror("fopen /dev/tty");
        exit(1);
    }
    rl_outstream = tty;

    // Enable autocompletion
    rl_attempted_completion_function = my_completion;

    while (1) {
        char *line = readline("$ ");
        if (!line) break; // Ctrl-D
        if (*line) add_history(line);

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

        int saved_stdout = -1, saved_stderr = -1;
        if (strcmp(cmd.args[0], "echo") == 0) {
            if (cmd.stdout_redir) {
                saved_stdout = dup(1);
                int flags = O_WRONLY | O_CREAT | (cmd.stdout_append ? O_APPEND : O_TRUNC);
                int fd = open(cmd.stdout_redir, flags, 0644);
                if (fd == -1) { perror("open"); goto cleanup; }
                dup2(fd, 1);
                close(fd);
            }
            printf("%s\n", cmd.args[1] ? cmd.args[1] : "");
        } else if (strcmp(cmd.args[0], "exit") == 0) {
            free_command(&cmd);
            fclose(tty);
            exit(0);
        } else {
            run_external_cmd(&cmd);
        }

    cleanup:
        if (saved_stdout != -1) {
            dup2(saved_stdout, 1);
            close(saved_stdout);
        }
        if (saved_stderr != -1) {
            dup2(saved_stderr, 2);
            close(saved_stderr);
        }
        free_command(&cmd);
    }

    fclose(tty);
    return 0;
}