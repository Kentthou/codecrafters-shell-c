#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT 128
#define MAX_ARGS 16
#define MAXIMUM_PATH 4096

int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "type") == 0;
}

void parse_input(char *input, char **args) {
  int i = 0, arg_index = 0;
  char arg_buf[MAX_INPUT];
  int buf_index = 0;
  int in_single_quote = 0, in_double_quote = 0;
  int backslash = 0;

  while (input[i] != '\0') {
    char c = input[i];

    // Handle backslash
    if (c == '\\' && !backslash && !in_single_quote) {
      backslash = 1;
      i++;
      continue;
    }

    // Process character after backslash
    if (backslash) {
      if (c == 'n') {
        arg_buf[buf_index++] = '\n';
      } else if (c == 't') {
        arg_buf[buf_index++] = '\t';
      } else if (c == '\\' || c == '\'' || c == '"') {
        arg_buf[buf_index++] = c;
      } else if (c >= '0' && c <= '9') {
        // Handle numeric characters literally
        arg_buf[buf_index++] = c;
      } else {
        // Preserve other characters literally
        arg_buf[buf_index++] = c;
      }
      backslash = 0;
      i++;
      continue;
    }

    // Handle single quotes
    if (c == '\'' && !in_double_quote && !backslash) {
      in_single_quote = !in_single_quote;
      i++;
      continue;
    }

    // Handle double quotes
    if (c == '"' && !in_single_quote && !backslash) {
      in_double_quote = !in_double_quote;
      i++;
      continue;
    }

    // Handle spaces outside quotes
    if (!in_single_quote && !in_double_quote && (c == ' ' || c == '\t')) {
      if (buf_index > 0) {
        arg_buf[buf_index] = '\0';
        args[arg_index++] = strdup(arg_buf);
        buf_index = 0;
      }
      i++;
      continue;
    }

    // Add character to buffer
    arg_buf[buf_index++] = c;
    i++;
  }

  // Finalize last argument if any
  if (buf_index > 0) {
    arg_buf[buf_index] = '\0';
    args[arg_index++] = strdup(arg_buf);
  }

  args[arg_index] = NULL;
}

void handle_echo(char **args) {
  for (int i = 1; args[i] != NULL; i++) {
    printf("%s", args[i]);
    if (args[i + 1] != NULL) {
      printf(" ");
    }
  }
  printf("\n");
}

void handle_type(char **args) {
  if (args[1] == NULL) {
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

  char *path_copy = strdup(path_env);
  char *dir = strtok(path_copy, ":");
  char full_path[MAXIMUM_PATH];
  int found = 0;

  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);
    if (access(full_path, X_OK) == 0) {
      printf("%s is %s\n", args[1], full_path);
      found = 1;
      break;
    }
    dir = strtok(NULL, ":");
  }

  if (!found) {
    printf("%s: not found\n", args[1]);
  }

  free(path_copy);
}

void handle_pwd() {
  char full_path[MAXIMUM_PATH];
  if (getcwd(full_path, sizeof(full_path)) != NULL) {
    printf("%s\n", full_path);
  } else {
    perror("getcwd failed");
  }
}

void handle_cd(char **args) {
  char resolved_path[MAXIMUM_PATH];
  char *target_path = NULL;

  if (args[1] == NULL || strcmp(args[1], "~") == 0) {
    target_path = getenv("HOME");
    if (!target_path) {
      fprintf(stderr, "cd: HOME not set\n");
      return;
    }
  } else {
    target_path = args[1];
  }

  if (realpath(target_path, resolved_path) == NULL) {
    fprintf(stderr, "cd: %s: No such file or directory\n", target_path);
    return;
  }

  if (chdir(resolved_path) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", target_path);
  }
}

void run_external_cmd(char **args) {
  char *path_env = getenv("PATH");
  if (!path_env) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path_copy = strdup(path_env);
  char *dir = strtok(path_copy, ":");
  char full_path[MAXIMUM_PATH];
  int found = 0;

  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
    if (access(full_path, X_OK) == 0) {
      found = 1;
      break;
    }
    dir = strtok(NULL, ":");
  }

  if (!found) {
    fprintf(stderr, "%s: command not found\n", args[0]);
    free(path_copy);
    return;
  }

  pid_t pid = fork();

  if (pid == 0) {
    execv(full_path, args);
    perror("execv");
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, NULL, 0);
  } else {
    perror("fork");
  }

  free(path_copy);
}

void free_args(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    free(args[i]);
  }
}

int main() {
  setbuf(stdout, NULL);
  char input[MAX_INPUT];
  char *args[MAX_ARGS];

  while (1) {
    printf("$ ");

    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;
    }

    input[strcspn(input, "\n")] = '\0';

    parse_input(input, args);

    if (args[0] == NULL) {
      continue;
    }

    if (strcmp(args[0], "echo") == 0) {
      handle_echo(args);
    } else if (strcmp(args[0], "type") == 0) {
      handle_type(args);
    } else if (strcmp(args[0], "pwd") == 0) {
      handle_pwd();
    } else if (strcmp(args[0], "cd") == 0) {
      handle_cd(args);
    } else if (strcmp(args[0], "exit") == 0 && args[1] != NULL && strcmp(args[1], "0") == 0) {
      free_args(args);
      exit(0);
    } else {
      run_external_cmd(args);
    }

    free_args(args);
  }

  return 0;
}