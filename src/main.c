#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_ARGS 16
#define MAX_PATH 4096

// --- Trie data structure for autocomplete ---
typedef struct TrieNode {
    char letter;
    int is_end;            // marks end of a word
    struct TrieNode *child;  // first child
    struct TrieNode *sibling;// next sibling
} TrieNode;

// Create a new trie node
static TrieNode* new_node(char letter) {
    TrieNode *node = malloc(sizeof(TrieNode));
    node->letter = letter;
    node->is_end = 0;
    node->child = node->sibling = NULL;
    return node;
}

// Insert a word into the trie
static void trie_insert(TrieNode *root, const char *word) {
    TrieNode *cur = root;
    for (; *word; word++) {
        // find matching child or create
        TrieNode *p = cur->child, *prev = NULL;
        while (p && p->letter != *word) {
            prev = p; p = p->sibling;
        }
        if (!p) {
            TrieNode *n = new_node(*word);
            if (prev) prev->sibling = n;
            else       cur->child = n;
            p = n;
        }
        cur = p;
    }
    cur->is_end = 1;
}

// Helper: collect completions recursively
static void collect(TrieNode *node, char *prefix, int depth,
                    char **matches, int *count, int max) {
    if (*count >= max) return;
    if (node->is_end) {
        prefix[depth] = '\0';
        matches[(*count)++] = strdup(prefix);
    }
    
    for (TrieNode *c = node->child; c; c = c->sibling) {
        prefix[depth] = c->letter;
        collect(c, prefix, depth+1, matches, count, max);
    }
}

// Find node matching the current prefix
static TrieNode* find_node(TrieNode *root, const char *prefix) {
    TrieNode *cur = root;
    for (; *prefix && cur; prefix++) {
        TrieNode *p = cur->child;
        while (p && p->letter != *prefix) p = p->sibling;
        cur = p;
    }
    return cur;
}

// Readline generator: called for each match request
static char *autocomplete_gen(const char *text, int state) {
    static char *matches[16];
    static int match_count, match_index;
    static TrieNode *root;
    static int initialized = 0;
    if (!initialized) {
        // build trie with builtins
        root = new_node('\0');
        trie_insert(root, "echo");
        trie_insert(root, "exit");
        trie_insert(root, "pwd");
        trie_insert(root, "cd");
        trie_insert(root, "type");
        initialized = 1;
    }
    if (state == 0) {
        // first call: find all matches
        match_count = match_index = 0;
        TrieNode *node = find_node(root, text);
        char buffer[32];
        int len = strlen(text);
        strncpy(buffer, text, len);
        collect(node, buffer, len, matches, &match_count, 15);
    }
    if (match_index < match_count)
        return matches[match_index++];
    return NULL;
}

// Hook into readline for completion
static char** minishell_completion(const char *text, int start, int end) {
    // only complete first word
    if (start == 0) {
        rl_completion_append_character = ' ';
        return rl_completion_matches(text, autocomplete_gen);
    }
    return NULL;
}

// Simple parser: split by spaces (no quotes support)
static void parse_args(char *line, char **argv) {
    int i = 0;
    char *token = strtok(line, " \t");
    while (token && i < MAX_ARGS-1) {
        argv[i++] = token;
        token = strtok(NULL, " \t");
    }
    argv[i] = NULL;
}

int main(void) {
    char *line;
    char *argv[MAX_ARGS];

    // Initialize readline and completion
    rl_readline_name = "minishell";
    rl_attempted_completion_function = minishell_completion;
    using_history();

    while ((line = readline("$ ")) != NULL) {
        if (*line) add_history(line);
        parse_args(line, argv);
        if (!argv[0]) { free(line); continue; }

        // Builtins
        if (strcmp(argv[0], "echo") == 0) {
            for (int i = 1; argv[i]; i++)
                printf("%s%s", argv[i], argv[i+1]?" ":"\n");
        }
        else if (strcmp(argv[0], "exit") == 0) {
            free(line);
            break;
        }
        else if (strcmp(argv[0], "pwd") == 0) {
            char cwd[MAX_PATH];
            if (getcwd(cwd, sizeof(cwd))) puts(cwd);
        }
        else if (strcmp(argv[0], "cd") == 0) {
            const char *d = argv[1]?argv[1]:getenv("HOME");
            chdir(d);
        }
        else if (strcmp(argv[0], "type") == 0) {
            if (!argv[1]) printf("type: missing arg\n");
            else {
                int ok = 0;
                // check builtin
                for (char **b = (char*[]){"echo","exit","pwd","cd","type",NULL}; b[0]; b++)
                    if (!strcmp(argv[1], *b)) { printf("%s is builtin\n", argv[1]); ok=1; }
                if (!ok) printf("%s: not found\n", argv[1]);
            }
        }
        // External commands: fork+exec
        else {
            pid_t pid = fork();
            if (pid == 0) execvp(argv[0], argv);
            else if (pid > 0) waitpid(pid, NULL, 0);
            else perror("fork");
        }
        free(line);
    }
    printf("Goodbye!\n");
    return 0;
}
