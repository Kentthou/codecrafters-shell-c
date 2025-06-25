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

void parse_input(char *input, char **args) {
  int i = 0, arg_index = 0;
  char arg_buf[MAX_INPUT];
  int buf_index = 0;
  int in_single_quote = 0, in_double_quote = 0;

  while (input[i] != '\0') {
    char c = input[i];

    if (in_single_quote) {
      if (c == '\'') in_single_quote = 0;
      else arg_buf[buf_index++] = c;
    } else if (in_double_quote) {
      if (c == '\\' && (input[i+1] == '"' || input[i+1] == '\\')) {
        arg_buf[buf_index++] = input[++i];
      } else if (c == '"') in_double_quote = 0;
      else arg_buf[buf_index++] = c;
    } else {
      if (c == '\\' && input[i+1] != '\0') {
        arg_buf[buf_index++] = input[++i];
      } else if (c == '\'' ) {
        in_single_quote = 1;
      } else if (c == '"') {
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
    if (args[i+1]) printf(" ");
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

void run_external_cmd(char **args) {
  char *path_env = getenv("PATH"); if (!path_env) { fprintf(stderr, "PATH not set\n"); return; }
  char *path_copy = strdup(path_env);
  char *dir = strtok(path_copy, ":");
  char full_path[MAXIMUM_PATH]; int found = 0;
  while (dir) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
    if (access(full_path, X_OK) == 0) { found = 1; break; }
    dir = strtok(NULL, ":");
  }
  if (!found) { fprintf(stderr, "%s: command not found\n", args[0]); free(path_copy); return; }
  pid_t pid = fork();
  if (pid == 0) { execv(full_path, args); perror("execv"); exit(1); }
  else if (pid > 0) waitpid(pid, NULL, 0);
  else perror("fork");
  free(path_copy);
}

void free_args(char **args) {
  for (int i = 0; args[i]; i++) free(args[i]);
}

int main() {
  // Enable tab completion for our builtins
  rl_attempted_completion_function = my_completion;

  while (1) {
    // Read input with readline, showing prompt
    char *line = readline("$ ");
    if (!line) break;               // Ctrl-D
    if (*line) add_history(line);   // add non-empty to history

    // Prepare args array
    char *args[MAX_ARGS];
    *args = NULL;

    // Copy input into buffer for parsing
    char buf[MAX_INPUT];
    strncpy(buf, line, MAX_INPUT);
    buf[MAX_INPUT-1] = '\0';
    free(line);

    parse_input(buf, args);
    if (!args[0]) continue;

    // Execute builtins or external
    if (strcmp(args[0], "echo") == 0)      handle_echo(args);
    else if (strcmp(args[0], "type") == 0) handle_type(args);
    else if (strcmp(args[0], "pwd") == 0)  handle_pwd();
    else if (strcmp(args[0], "cd") == 0)   handle_cd(args);
    else if (strcmp(args[0], "exit") == 0 && (!args[1] || strcmp(args[1], "0")==0)) {
      free_args(args);
      break;
    } else run_external_cmd(args);

    free_args(args);
  }

  return 0;
}
