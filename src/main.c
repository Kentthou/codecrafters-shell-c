/*
 * Mini Shell with Trie-based Autocomplete (Beginner-Friendly)
 * ----------------------------------------------------------
 * Supports builtins: echo, exit, pwd, cd, type
 * Handles simple quoting, I/O redirection, and external commands
 * Uses a Trie for fast builtin autocompletion via <TAB>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_INPUT 128
#define MAX_ARGS 16
#define MAX_PATH 4096

// ===== Trie definitions for autocomplete =====
typedef struct TrieNode {
    char ch;
    int is_end;
    struct TrieNode *first;   // first child
    struct TrieNode *next;    // next sibling
} TrieNode;

// Create a new Trie node for character c
static TrieNode* trie_node(char c) {
    TrieNode *n = malloc(sizeof(TrieNode));
    n->ch = c;
    n->is_end = 0;
    n->first = n->next = NULL;
    return n;
}

// Insert word into trie rooted at root
static void trie_insert(TrieNode *root, const char *word) {
    TrieNode *cur = root;
    for (; *word; word++) {
        TrieNode *p = cur->first, *prev = NULL;
        while (p && p->ch != *word) prev = p, p = p->next;
        if (!p) {
            p = trie_node(*word);
            if (prev) prev->next = p;
            else        cur->first = p;
        }
        cur = p;
    }
    cur->is_end = 1;
}

// Find trie node matching prefix; return node or NULL
static TrieNode* trie_find(TrieNode *root, const char *pref) {
    TrieNode *cur = root;
    for (; *pref && cur; pref++) {
        TrieNode *p = cur->first;
        while (p && p->ch != *pref) p = p->next;
        cur = p;
    }
    return cur;
}

// Recursively collect words from node, assemble into buffer
static void trie_collect(TrieNode *node, char *buf, int depth,
                         char **out, int *count) {
    if (!node) return;
    if (node->is_end) {
        buf[depth] = '\0';
        out[(*count)++] = strdup(buf);
    }
    for (TrieNode *c = node->first; c; c = c->next) {
        buf[depth] = c->ch;
        trie_collect(c, buf, depth+1, out, count);
    }
}

// Readline generator for completions
static char *gen_match(const char *text, int state) {
    static TrieNode *root;
    static char *matches[10];
    static int nmatch, idx;
    static int init;

    if (!init) {
        root = trie_node('\0');
        trie_insert(root, "echo");
        trie_insert(root, "exit");
        trie_insert(root, "pwd");
        trie_insert(root, "cd");
        trie_insert(root, "type");
        init = 1;
    }
    if (state == 0) {
        nmatch = idx = 0;
        int len = strlen(text);
        TrieNode *node = trie_find(root, text);
        char buf[32];
        strcpy(buf, text);
        if (node) trie_collect(node, buf, len, matches, &nmatch);
    }
    return (idx < nmatch ? matches[idx++] : NULL);
}

// Hook into readline for <TAB> completion
static char **myshell_completion(const char *text, int start, int end) {
    if (start == 0) {
        rl_completion_append_character = ' ';
        return rl_completion_matches(text, gen_match);
    }
    return NULL;
}

// Simple split by whitespace (no quotes)
static void split_args(char *s, char **argv) {
    int i = 0;
    char *tok = strtok(s, " \t");
    while (tok && i < MAX_ARGS-1) {
        argv[i++] = tok;
        tok = strtok(NULL, " \t");
    }
    argv[i] = NULL;
}

// Main loop
int main() {
    rl_readline_name = "myshell";
    rl_attempted_completion_function = myshell_completion;
    using_history();

    while (1) {
        char *line = readline("$ ");
        if (!line) break;
        if (*line) add_history(line);
        char buf[MAX_INPUT]; strncpy(buf, line, MAX_INPUT-1);
        free(line);

        // handle redirection tokens: >, >>, 2>, 2>>
        char *argv[MAX_ARGS];
        split_args(buf, argv);
        if (!argv[0]) continue;

        int fd = -1, saved = -1, mode = 0;  // mode: 1=stdout,2=stderr
        for (int i=0; argv[i]; i++) {
            if (strcmp(argv[i], ">")==0 || strcmp(argv[i], ">>")==0 ||
                strcmp(argv[i], "2>")==0 || strcmp(argv[i], "2>>")==0) {
                mode = (argv[i][0]=='2' ? 2 : 1);
                int append = (strstr(argv[i], ">>")!=NULL);
                char *file = argv[i+1];
                argv[i] = NULL;
                saved = dup(mode);
                fd = open(file, O_CREAT | O_WRONLY | (append?O_APPEND:O_TRUNC), 0666);
                dup2(fd, mode);
                break;
            }
        }

        // Builtin: echo
        if (strcmp(argv[0], "echo")==0) {
            for (int i=1; argv[i]; i++)
                printf("%s%s", argv[i], argv[i+1]?" ":"\n");
        }
        // exit
        else if (strcmp(argv[0], "exit")==0) break;
        // pwd
        else if (strcmp(argv[0], "pwd")==0) {
            char cwd[MAX_PATH]; if (getcwd(cwd,MAX_PATH)) puts(cwd);
        }
        // cd
        else if (strcmp(argv[0], "cd")==0) {
            char *d = argv[1] ? argv[1] : getenv("HOME");
            chdir(d);
        }
        // type
        else if (strcmp(argv[0], "type")==0) {
            if (!argv[1]) printf("type: missing arg\n");
            else {
                int found=0;
                for (char *b: (char*[]){"echo","exit","pwd","cd","type",NULL})
                    if (!strcmp(argv[1], b)) { printf("%s is a shell builtin\n",argv[1]); found=1; }
                if (!found) printf("%s: not found\n",argv[1]);
            }
        }
        // external
        else {
            pid_t pid = fork();
            if (pid==0) execvp(argv[0], argv);
            else if (pid>0) waitpid(pid,NULL,0);
            else perror("fork");
        }

        // restore redirection
        if (fd!=-1) {
            dup2(saved, mode);
            close(fd); close(saved);
        }
    }

    printf("Goodbye!\n");
    return 0;
}
