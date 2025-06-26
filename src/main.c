#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>      // for reading directories
#include <sys/stat.h>    // for file permissions
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT 128
#define MAX_ARGS 16

/* Built-in command names */
static const char *builtins[] = {
    "echo",
    "exit",
    "pwd",
    "cd",
    "type",
    NULL
};

/* Will hold tab-completion candidates */
static char **candidates = NULL;
static int candidate_count = 0;

/* Free out the old candidates list */
static void clear_candidates() {
    if (!candidates) return;
    for (int i = 0; i < candidate_count; i++)
        free(candidates[i]);
    free(candidates);
    candidates = NULL;
    candidate_count = 0;
}

/* Readline “generator”: on first call (state==0) build all matches */
static char *command_generator(const char *text, int state) {
    static int idx, text_len;
    if (state == 0) {
        clear_candidates();
        text_len = strlen(text);
        idx = 0;

        /* 1) Add built-ins that start with what you typed */
        for (int i = 0; builtins[i]; i++) {
            if (strncmp(builtins[i], text, text_len) == 0) {
                candidates = realloc(candidates, sizeof(char*) * (candidate_count + 1));
                candidates[candidate_count++] = strdup(builtins[i]);
            }
        }

        /* 2) For each folder in PATH, scan for executables */
        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = strdup(path_env);
            char *dir = strtok(path_copy, ":");
            while (dir) {
                DIR *d = opendir(dir);
                if (d) {
                    struct dirent *ent;
                    while ((ent = readdir(d))) {
                        if (strncmp(ent->d_name, text, text_len) != 0)
                            continue;
                        /* Build full path to check exec permission */
                        char full[MAX_INPUT];
                        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
                        if (access(full, X_OK) == 0) {
                            candidates = realloc(candidates, sizeof(char*) * (candidate_count + 1));
                            candidates[candidate_count++] = strdup(ent->d_name);
                        }
                    }
                    closedir(d);
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }

    /* Return each match, one by one */
    if (idx < candidate_count)
        return strdup(candidates[idx++]);

    /* No more, so clean up */
    clear_candidates();
    return NULL;
}

/* Hook our generator into readline, but only for the first word */
static char **my_completion(const char *text, int start, int end) {
    (void)end;
    if (start == 0)
        return rl_completion_matches(text, command_generator);
    return NULL;
}

/* Check if a command is built-in */
int is_builtin(const char *cmd) {
    for (int i = 0; builtins[i]; i++)
        if (strcmp(cmd, builtins[i]) == 0)
            return 1;
    return 0;
}

/* Split the input line into args[], respecting quotes and backslashes */
void parse_input(char *input, char **args) {
    int i = 0, ai = 0, bi = 0;
    char buf[MAX_INPUT];
    int in_sq = 0, in_dq = 0;
    while (input[i]) {
        char c = input[i];
        if (in_sq) {
            if (c == '\'') in_sq = 0;
            else buf[bi++] = c;
        }
        else if (in_dq) {
            if (c == '\\' && (input[i+1]=='"'||input[i+1]=='\\')) {
                buf[bi++] = input[++i];
            } else if (c == '"') {
                in_dq = 0;
            } else buf[bi++] = c;
        }
        else {
            if (c == '\\' && input[i+1]) {
                buf[bi++] = input[++i];
            }
            else if (c == '\'') in_sq = 1;
            else if (c == '"') in_dq = 1;
            else if (c==' '||c=='\t') {
                if (bi>0) {
                    buf[bi]='\0';
                    args[ai++]=strdup(buf);
                    bi=0;
                }
            }
            else buf[bi++]=c;
        }
        i++;
    }
    if (bi>0) {
        buf[bi]='\0';
        args[ai++]=strdup(buf);
    }
    args[ai]=NULL;
}

/* echo command */
void handle_echo(char **args) {
    for (int i = 1; args[i]; i++) {
        printf("%s", args[i]);
        if (args[i+1]) printf(" ");
    }
    printf("\n");
}

/* type command */
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
        fprintf(stderr, "PATH not set\n");
        return;
    }
    char *pcopy = strdup(path);
    char *dir = strtok(pcopy, ":");
    char full[MAX_INPUT];
    int found = 0;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[1]);
        if (access(full, X_OK)==0) {
            printf("%s is %s\n", args[1], full);
            found = 1;
            break;
        }
        dir = strtok(NULL, ":");
    }
    if (!found) printf("%s: not found\n", args[1]);
    free(pcopy);
}

/* pwd command */
void handle_pwd() {
    char cwd[MAX_INPUT];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
}

/* cd command */
void handle_cd(char **args) {
    char *target = args[1] && strcmp(args[1],"~")!=0 ? args[1] : getenv("HOME");
    if (!target) {
        fprintf(stderr, "cd: HOME not set\n");
        return;
    }
    if (chdir(target) != 0) {
        perror("cd");
    }
}

/* Run anything not built-in */
void run_external(char **args) {
    char *path = getenv("PATH");
    if (!path) {
        fprintf(stderr, "PATH not set\n");
        return;
    }
    char *pcopy = strdup(path);
    char *dir = strtok(pcopy, ":");
    char full[MAX_INPUT];
    int found = 0;
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[0]);
        if (access(full, X_OK)==0) {
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
    if (pid == 0) {
        execv(full, args);
        perror("execv");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
    }
}

/* Free memory of args[] */
void free_args(char **args) {
    for (int i = 0; args[i]; i++)
        free(args[i]);
}

int main() {
    setbuf(stdout, NULL);

    /* Hook our completion into readline */
    rl_readline_name = "mysh";
    rl_attempted_completion_function = my_completion;
    rl_completion_append_character = ' ';

    while (1) {
        char *line = readline("$ ");
        if (!line) break;             // Ctrl-D
        if (*line) add_history(line);

        char *args[MAX_ARGS];
        parse_input(line, args);
        free(line);

        if (!args[0]) {
            free_args(args);
            continue;
        }

        /* Check for output redirection */
        int rd_fd = -1, save_fd = -1;
        for (int i = 0; args[i]; i++) {
            if (strcmp(args[i], ">")==0 || strcmp(args[i], ">>")==0) {
                int append = (args[i][1]=='>');
                if (args[i+1]) {
                    rd_fd = open(args[i+1],
                                 O_WRONLY | O_CREAT | (append?O_APPEND:O_TRUNC),
                                 0666);
                    save_fd = dup(1);
                    dup2(rd_fd, 1);
                    args[i] = NULL;
                }
                break;
            }
        }

        /* Decide which command to run */
        if (strcmp(args[0],"echo")==0)        handle_echo(args);
        else if (strcmp(args[0],"type")==0)  handle_type(args);
        else if (strcmp(args[0],"pwd")==0)   handle_pwd();
        else if (strcmp(args[0],"cd")==0)    handle_cd(args);
        else if (strcmp(args[0],"exit")==0)  { free_args(args); break; }
        else                                  run_external(args);

        /* Restore stdout if we redirected */
        if (rd_fd != -1) {
            dup2(save_fd, 1);
            close(save_fd);
            close(rd_fd);
        }

        free_args(args);
    }

    return 0;
}
