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

// Parse input into args, handling single quotes
void parse_input(char *input, char **args) {
  int arg_idx = 0;  // Index for args array
  int char_idx = 0; // Index for current argument's characters
  char current_arg[MAX_INPUT]; // Buffer for building current argument
  int in_quotes = 0; // Flag for being inside single quotes
  int i = 0; // Index for input string

  while (input[i] != '\0' && arg_idx < MAX_ARGS - 1) {
    // Skip leading whitespace outside quotes
    if (!in_quotes && (input[i] == ' ' || input[i] == '\t')) {
      i++;
      continue;
    }

    // Start of a quoted string
    if (input[i] == '\'') {
      in_quotes = !in_quotes; // Toggle quote state
      i++;
      continue;
    }

    // If inside quotes, copy character literally
    if (in_quotes) {
      current_arg[char_idx++] = input[i++];
      continue;
    }

    // Outside quotes, hitting a space or tab ends the current argument
    if (input[i] == ' ' || input[i] == '\t') {
      if (char_idx > 0) { // Only store if we have characters
        current_arg[char_idx] = '\0';
        args[arg_idx] = strdup(current_arg);
        arg_idx++;
        char_idx = 0;
      }
      i++;
      continue;
    }

    // Copy character to current argument
    current_arg[char_idx++] = input[i++];
  }

  // Handle the last argument if any characters were collected
  if (char_idx > 0 && arg_idx < MAX_ARGS - 1) {
    current_arg[char_idx] = '\0';
    args[arg_idx] = strdup(current_arg);
    arg_idx++;
  }

  // Null-terminate the args array
  args[arg_idx] = NULL;
}

// Handle echo command (prints args after echo)
void handle_echo(char **args) {
  if (args[1] == NULL) {
    printf("\n"); // prints a blank line
  } else {
    // Join args[1...n] with spaces
    for (int i = 1; args[i] != NULL; i++) {
      printf("%s", args[i]);
      if (args[i + 1] != NULL) {
        printf(" ");
      }
    }
    printf("\n");
  }
}

// Handle type command (shows if a command is builtin or in PATH)
void handle_type(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "type: missing argument\n");
    return;
  }

  // check if shell builtin
  if (is_builtin(args[1])) {
    printf("%s is a shell builtin\n", args[1]);
    return; // stop here, no need to check PATH
  }

  // Get the PATH environment variable
  char *path_env = getenv("PATH");
  if (!path_env) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path_copy = strdup(path_env); // make a copy since strtok modifies the string
  char *dir = strtok(path_copy, ":"); // gets the first dir
  int found = 0; // flag to track if command is found in any PATH dir
  char full_path[MAXIMUM_PATH];

  // Go through each dir in PATH
  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);

    if (access(full_path, X_OK) == 0) {
      printf("%s is %s\n", args[1], full_path);
      found = 1;
      break;
    }

    dir = strtok(NULL, ":"); // move to next dir
  }

  if (!found) {
    printf("%s: not found\n", args[1]);
  }

  free(path_copy);
}

// Handle pwd command (prints current directory)
void handle_pwd() {
  char full_path[MAXIMUM_PATH];
  if (getcwd(full_path, sizeof(full_path)) != NULL) {
    printf("%s\n", full_path);
  } else {
    perror("getcwd failed");
  }
}

void handle_cd(char **args) {
  char current_dir[MAXIMUM_PATH];
  char resolved_path[MAXIMUM_PATH];
  char *target_path = NULL;

  // Use home directory if no path given or path is ~
  if (args[1] == NULL || strcmp(args[1], "~") == 0) {
    target_path = getenv("HOME");
    if (target_path == NULL) {
      fprintf(stderr, "cd: HOME not set\n");
      return;
    }
  } else {
    target_path = args[1];
  }

  // gets current working dir
  if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
    perror("getcwd failed");
    return;
  }

  // Resolve/converts to full absolute path (handles ./, ../, etc.)
  if (realpath(target_path, resolved_path) == NULL) {
    fprintf(stderr, "cd: %s: No such file or directory\n", args[1] ? args[1] : "~");
    return;
  }

  // Change to the resolved dir
  if (chdir(resolved_path) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", args[1] ? args[1] : "~");
  }
}

void run_external_cmd(char **args) {
  char *path_env = getenv("PATH");
  if (path_env == NULL) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path_copy = strdup(path_env); // make a copy since strtok modifies the string
  if (path_copy == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    return;
  }

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
    execv(full_path, args); // Child process: try to run the command
    perror("execv");
    exit(1);
  } else if (pid > 0) {
    // parent: wait for child
    waitpid(pid, NULL, 0);
  } else {
    perror("fork");
  }

  free(path_copy);
}

int main() {
  setbuf(stdout, NULL); // disables output buffering so we see "$ " right away
  char input[MAX_INPUT]; // storage for the user's input
  char *args[MAX_ARGS]; // array to hold command and arguments

  while (1) {
    printf("$ ");

    // fgets() returns NULL if the user types Ctrl+D (EOF)
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break; // if Ctrl+D is pressed (EOF), exit the loop and end the shell
    }

    input[strcspn(input, "\n")] = '\0'; // remove newline

    // Parse input into args
    parse_input(input, args);

    // Skip if no command entered
    if (args[0] == NULL) {
      continue;
    }

    // Check for builtins
    if (strcmp(args[0], "echo") == 0) {
      handle_echo(args);
    } else if (strcmp(args[0], "type") == 0) {
      handle_type(args);
    } else if (strcmp(args[0], "pwd") == 0) {
      handle_pwd();
    } else if (strcmp(args[0], "cd") == 0) {
      handle_cd(args);
    } else if (strcmp(args[0], "exit") == 0 && args[1] != NULL && strcmp(args[1], "0") == 0) {
      // Free allocated args before exiting
      for (int i = 0; args[i] != NULL; i++) {
        free(args[i]);
      }
      exit(0);
    } else {
      run_external_cmd(args);
    }

    // Free allocated args
    for (int i = 0; args[i] != NULL; i++) {
      free(args[i]);
    }
  }

  return 0;
}