#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

// Function to decode escape sequences
char decode_escape(char c) {
  switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case ' ': return ' ';
    default:
      if (c >= '0' && c <= '9') return c;
      return c;
  }
}

// Parses input into arguments with escape + quote support
void parse_input(const char *input, char **args) {
  int arg_index = 0;
  int i = 0;
  int len = strlen(input);

  while (i < len) {
    while (i < len && (input[i] == ' ' || input[i] == '\t')) i++;
    if (i >= len) break;

    char *arg = malloc(MAX_INPUT);
    int j = 0;
    int in_single = 0, in_double = 0;

    while (i < len) {
      char c = input[i];

      if (c == '\'' && !in_double) {
        in_single = !in_single;
        i++;
        continue;
      } else if (c == '"' && !in_single) {
        in_double = !in_double;
        i++;
        continue;
      } else if (c == '\\') {
        if (i + 1 < len) {
          arg[j++] = decode_escape(input[++i]);
          i++;
        } else {
          i++;
        }
        continue;
      } else if (!in_single && !in_double && (c == ' ' || c == '\t')) {
        i++;
        break;
      } else {
        arg[j++] = c;
        i++;
      }
    }

    arg[j] = '\0';
    args[arg_index++] = arg;
  }

  args[arg_index] = NULL;
}

void free_args(char **args) {
  for (int i = 0; args[i] != NULL; i++) free(args[i]);
}

int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 ||
         strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "type") == 0;
}

void handle_echo(char **args) {
  for (int i = 1; args[i]; i++) {
    printf("%s", args[i]);
    if (args[i + 1]) printf(" ");
  }
  printf("\n");
}

void handle_pwd() {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd))) {
    printf("%s\n", cwd);
  } else {
    perror("pwd");
  }
}

void handle_cd(char **args) {
  const char *path = args[1];
  if (!path || strcmp(path, "~") == 0) {
    path = getenv("HOME");
    if (!path) {
      fprintf(stderr, "cd: HOME not set\n");
      return;
    }
  }

  if (chdir(path) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", path);
  }
}

void handle_type(char **args) {
  if (!args[1]) {
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

  char *path = strdup(path_env);
  char *dir = strtok(path, ":");
  char full_path[PATH_MAX];

  while (dir) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);
    if (access(full_path, X_OK) == 0) {
      printf("%s is %s\n", args[1], full_path);
      free(path);
      return;
    }
    dir = strtok(NULL, ":");
  }

  printf("%s: not found\n", args[1]);
  free(path);
}

void run_external(char **args) {
  char *path_env = getenv("PATH");
  if (!path_env) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path = strdup(path_env);
  char *dir = strtok(path, ":");
  char full_path[PATH_MAX];

  while (dir) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
    if (access(full_path, X_OK) == 0) {
      pid_t pid = fork();
      if (pid == 0) {
        execv(full_path, args);
        perror("execv");
        exit(1);
      } else if (pid > 0) {
        waitpid(pid, NULL, 0);
        free(path);
        return;
      } else {
        perror("fork");
        free(path);
        return;
      }
    }
    dir = strtok(NULL, ":");
  }

  fprintf(stderr, "%s: command not found\n", args[0]);
  free(path);
}

int main() {
  setbuf(stdout, NULL);
  char input[MAX_INPUT];
  char *args[MAX_ARGS];

  while (1) {
    printf("$ ");

    if (!fgets(input, sizeof(input), stdin)) break;
    input[strcspn(input, "\n")] = '\0';

    parse_input(input, args);
    if (!args[0]) continue;

    if (strcmp(args[0], "echo") == 0) {
      handle_echo(args);
    } else if (strcmp(args[0], "pwd") == 0) {
      handle_pwd();
    } else if (strcmp(args[0], "cd") == 0) {
      handle_cd(args);
    } else if (strcmp(args[0], "type") == 0) {
      handle_type(args);
    } else if (strcmp(args[0], "exit") == 0) {
      if (args[1] && strcmp(args[1], "0") == 0) {
        free_args(args);
        exit(0);
      }
    } else {
      run_external(args);
    }

    free_args(args);
  }

  return 0;
}
