#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT     128
#define MAX_ARGS      16
#define MAX_PATH      4096

// List of builtin commands we support and want to autocomplete
static const char *builtins[] = { "echo", 
                                  "exit", 
                                  "pwd", 
                                  "cd", 
                                  "type", 
                                  NULL };

/*
 * readline completion: called when <TAB> is pressed.
 * If we are completing the first word, offer builtin matches.
 */
static char **completion_entry(const char *text, int start, int end) {
    // Only complete the first token (the command)
    if (start == 0) {
        // Tell readline to append a space after the match
        rl_completion_append_character = ' ';
        return rl_completion_matches(text, rl_filename_completion_function);
    }
    return NULL;
}

/*
 * Initialize GNU Readline for our shell.
 */
static void init_readline(void) {
    rl_readline_name = "mysh";
    rl_attempted_completion_function = completion_entry;
    using_history();  // enable history
}

/*
 * Split the input line into arguments (very simple parser).
 * Handles quotes for grouping but does not support redirection here.
 */
static void parse_input(char *line, char **argv) {
    int argc = 0;
    char *token = strtok(line, " \t");
    while (token && argc < MAX_ARGS - 1) {
        argv[argc++] = strdup(token);
        token = strtok(NULL, " \t");
    }
    argv[argc] = NULL;
}

/* Builtin: echo */
static void do_echo(char **argv) {
    for (int i = 1; argv[i]; i++) {
        printf("%s%s", argv[i], argv[i+1] ? " " : "");
    }
    printf("\n");
}

/* Builtin: exit (optional exit code) */
static void do_exit(char **argv) {
    int code = (argv[1] ? atoi(argv[1]) : 0);
    exit(code);
}

/* Builtin: pwd */
static void do_pwd(void) {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)))
        puts(cwd);
    else
        perror("pwd");
}

/* Builtin: cd */
static void do_cd(char **argv) {
    const char *dir = argv[1] ? argv[1] : getenv("HOME");
    if (!dir || chdir(dir) != 0)
        perror("cd");
}

/* Builtin: type */
static void do_type(char **argv) {
    if (!argv[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }

    for (int i = 0; builtins[i]; i++) {
        if (strcmp(argv[1], builtins[i]) == 0) {
            printf("%s is a shell builtin\n", argv[1]);
            return;
        }
    }
    // Not builtin, look in PATH
    char *path = strdup(getenv("PATH"));
    char *dir = strtok(path, ":");
    char candidate[MAX_PATH];
    while (dir) {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, argv[1]);
        if (access(candidate, X_OK) == 0) {
            printf("%s is %s\n", argv[1], candidate);
            free(path);
            return;
        }
        dir = strtok(NULL, ":");
    }
    printf("%s: not found\n", argv[1]);
    free(path);
}

/*
 * Launch an external program by searching PATH.
 */
static void launch_external(char **argv) {
    char *path_env = getenv("PATH");
    char *path = strdup(path_env);
    char *dir = strtok(path, ":");
    char full[MAX_PATH];

    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, argv[0]);
        if (access(full, X_OK) == 0) {
            pid_t pid = fork();
            if (pid == 0) execv(full, argv);
            else waitpid(pid, NULL, 0);
            free(path);
            return;
        }
        dir = strtok(NULL, ":");
    }
    fprintf(stderr, "%s: command not found\n", argv[0]);
    free(path);
}

int main(void) {
    init_readline();

    while (1) {
        char *line = readline("$ ");
        if (!line) break;               // EOF
        if (*line) add_history(line);   // store non-empty lines

        char *argv[MAX_ARGS];
        parse_input(line, argv);
        free(line);

        if (!argv[0]) continue;

        // Dispatch builtins
        if (strcmp(argv[0], "echo") == 0)        do_echo(argv);
        else if (strcmp(argv[0], "exit") == 0)   do_exit(argv);
        else if (strcmp(argv[0], "pwd") == 0)    do_pwd();
        else if (strcmp(argv[0], "cd") == 0)     do_cd(argv);
        else if (strcmp(argv[0], "type") == 0)   do_type(argv);
        else                                       launch_external(argv);

        // Free argument strings
        for (int i = 0; argv[i]; i++) free(argv[i]);
    }

    printf("\nGoodbye!\n");
    return 0;
}
