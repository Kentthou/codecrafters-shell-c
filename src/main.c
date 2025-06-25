#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT     128
#define MAX_ARGS      16
#define MAX_PATH_LEN 4096
#define MAX_MATCHES   16

typedef struct TrieNode {
    struct TrieNode *children[26];
    int is_end;
    char *word;  // full command stored at word-end
} TrieNode;

TrieNode *new_node() {
    TrieNode *n = calloc(1, sizeof(TrieNode));
    return n;
}

// Insert a word into the trie
void trie_insert(TrieNode *root, const char *s) {
    TrieNode *cur = root;
    for (; *s; ++s) {
        char c = *s;
        if (c < 'a' || c > 'z') continue;
        int idx = c - 'a';
        if (!cur->children[idx])
            cur->children[idx] = new_node();
        cur = cur->children[idx];
    }
    cur->is_end = 1;
    cur->word = strdup(s - strlen(s)); // point back to full word
}

// Recursively collect all words under a trie node
void collect_words(TrieNode *node, char *matches[], int *count) {
    if (*count >= MAX_MATCHES || !node) return;
    if (node->is_end) {
        matches[(*count)++] = node->word;
    }
    for (int i = 0; i < 26; i++) {
        if (node->children[i])
            collect_words(node->children[i], matches, count);
    }
}

// Find the trie node matching the prefix
TrieNode *find_prefix_node(TrieNode *root, const char *prefix) {
    TrieNode *cur = root;
    for (; *prefix; ++prefix) {
        char c = *prefix;
        if (c < 'a' || c > 'z') return NULL;
        cur = cur->children[c - 'a'];
        if (!cur) return NULL;
    }
    return cur;
}

void handle_echo(char **args) {
    for (int i = 1; args[i]; i++) {
        printf("%s", args[i]);
        if (args[i+1]) printf(" ");
    }
    printf("\n");
}

void handle_exit(char **args) {
    exit(0);
}

void handle_pwd(char **args) {
    char cwd[MAX_PATH_LEN];
    if (getcwd(cwd, sizeof(cwd)))
        printf("%s\n", cwd);
    else
        perror("pwd");
}

void handle_cd(char **args) {
    char *target = args[1] ? args[1] : getenv("HOME");
    if (!target) {
        fprintf(stderr, "cd: HOME not set\n");
        return;
    }
    if (chdir(target) != 0)
        perror("cd");
}

void handle_type(char **args) {
    if (!args[1]) {
        fprintf(stderr, "type: missing argument\n");
        return;
    }
    // Check built-ins
    extern TrieNode *builtin_trie;
    TrieNode *n = find_prefix_node(builtin_trie, args[1]);
    if (n && n->is_end) {
        printf("%s is a shell builtin\n", args[1]);
        return;
    }
    // Otherwise search $PATH
    char *path = getenv("PATH");
    if (!path) { fprintf(stderr, "PATH not set\n"); return; }
    char *copy = strdup(path), *dir = strtok(copy, ":");
    char full[MAX_PATH_LEN];
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[1]);
        if (access(full, X_OK) == 0) {
            printf("%s is %s\n", args[1], full);
            free(copy);
            return;
        }
        dir = strtok(NULL, ":");
    }
    printf("%s: not found\n", args[1]);
    free(copy);
}

struct {
    const char *name;
    void (*func)(char **);
} builtins[] = {
    { "echo", handle_echo },
    { "exit", handle_exit },
    { "pwd",  handle_pwd  },
    { "cd",   handle_cd   },
    { "type", handle_type},
    { NULL,   NULL        }
};

TrieNode *builtin_trie;

// Readline completion functions
static char *matches[MAX_MATCHES];
static int  match_count, match_index;

static char *trie_generator(const char *text, int state) {
    if (state == 0) {
        // first call: build match list
        match_index = 0;
        match_count = 0;
        TrieNode *p = find_prefix_node(builtin_trie, text);
        if (p) collect_words(p, matches, &match_count);
    }
    if (match_index < match_count)
        return strdup(matches[match_index++]);
    return NULL;
}

static char **my_completion(const char *text, int start, int end) {
    // only autocomplete first word (the command)
    if (start == 0)
        return rl_completion_matches(text, trie_generator);
    return NULL;
}

/* Input parsing (simple whitespace split) */

void parse_input(char *input, char **args) {
    int i = 0, arg = 0;
    char *token = strtok(input, " \t");
    while (token && arg < MAX_ARGS - 1) {
        args[arg++] = strdup(token);
        token = strtok(NULL, " \t");
    }
    args[arg] = NULL;
}

/* ======== Run external commands ======== */

void run_external(char **args) {
    char *path = getenv("PATH");
    if (!path) { fprintf(stderr, "PATH not set\n"); return; }
    char *copy = strdup(path), *dir = strtok(copy, ":");
    char full[MAX_PATH_LEN];
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, args[0]);
        if (access(full, X_OK) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                execv(full, args);
                perror("execv");
                exit(1);
            }
            waitpid(pid, NULL, 0);
            free(copy);
            return;
        }
        dir = strtok(NULL, ":");
    }
    fprintf(stderr, "%s: command not found\n", args[0]);
    free(copy);
}

/* ======== Main loop ======== */

int main() {
    // build trie of built-ins
    builtin_trie = new_node();
    for (int i = 0; builtins[i].name; i++)
        trie_insert(builtin_trie, builtins[i].name);

    // configure readline
    rl_readline_name = "mysh";
    rl_attempted_completion_function = my_completion;
    rl_completion_append_character = ' ';

    while (1) {
        char *line = readline("$ ");
        if (!line) {
            printf("\n");
            break;
        }
        if (*line) add_history(line);

        // handle redirection
        int rd_fd = -1, saved_fd = -1;
        char *args[MAX_ARGS];
        parse_input(line, args);
        free(line);

        // look for > or >> or 2> etc.
        for (int i = 0; args[i]; i++) {
            if (strchr(args[i], '>')) {
                int fdnum = (args[i][1] == '>') ? 1 : 1;  // only stdout in this simple version
                int append = (args[i][1] == '>');
                if (args[i+1]) {
                    rd_fd = open(args[i+1],
                                 O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC),
                                 0666);
                    saved_fd = dup(fdnum);
                    dup2(rd_fd, fdnum);
                    args[i] = NULL;
                }
                break;
            }
        }

        if (!args[0]) {
            // nothing left after stripping redirection
        }
        // check built-ins
        else {
            int ran = 0;
            for (int i = 0; builtins[i].name; i++) {
                if (strcmp(args[0], builtins[i].name) == 0) {
                    builtins[i].func(args);
                    ran = 1;
                    break;
                }
            }
            if (!ran) {
                run_external(args);
            }
        }

        // restore fds if needed
        if (saved_fd != -1) {
            dup2(saved_fd, STDOUT_FILENO);
            close(saved_fd);
            close(rd_fd);
        }

        // free args
        for (int i = 0; args[i]; i++)
            free(args[i]);
    }
    return 0;
}
