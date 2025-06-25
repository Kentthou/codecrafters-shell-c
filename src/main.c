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
  int arg_index = 0; // Tracks where to store the next argument
  char *current = input; // Points to the current position in input
  char buffer[MAX_INPUT]; // Temporary storage for building an argument
  int buffer_index = 0; // Tracks position in buffer

  while (*current != '\0' && arg_index < MAX_ARGS - 1) {
    // Skips leading spaces
    while (*current == ' ') {
      current++;
    }

    // If we hit a single quote
    if (*current == '\'') {
      current++; // Move past the opening quote
      buffer_index = 0; // Reset buffer

      // Copy all until closing quote
      while (*current != '\0' && *current != '\'') {
        if (buffer_index < MAX_INPUT - 1) {
          buffer[buffer_index++] = *current;
        }
        current++;
      }

      // Move past closing quote
      if (*current == '\'') {
        current++;
      }

      if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        args[arg_index] = strdup(buffer);
        arg_index++;
      }
    } else if (*current != '\0') {
      // Handle unquoted text
      buffer_index = 0;
      while (*current != '\0' && *current != ' ' && *current != '\'') {
        if (buffer_index < MAX_INPUT - 1) {
          buffer[buffer_index++] = *current;
        }
        current++;
      }

      if (buffer_index > 0) {
        buffer[buffer_index] = '\0';
        args[arg_index] = strdup(buffer);
        arg_index++;
      }
    }
  }

  args[arg_index] = NULL; // Mark the end of arguments
}

// Handle echo command (prints args after echo)
void handle_echo(char **args) {
  if (args[1] == NULL) {
    printf("\n"); // Prints a blank line
  } else {
    // Print args[1...n] with a single space between them
    for (int i = 1; args[i] != NULL; i++) {
      printf("%s", args[i]);
      if (args[i + 1] != NULL) {
        printf(" "); // Add space only if another argument follows
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

  // Check if shell builtin
  if (is_builtin(args[1])) {
    printf("%s is a shell builtin\n", args[1]);
    return; // Stop here, no need to check PATH
  }

  // Get the PATH environment variable
  char *path_env = getenv("PATH");
  if (!path_env) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path_copy = strdup(path_env); // Make a copy since strtok modifies the string
  char *dir = strtok(path_copy, ":"); // Gets the first dir
  int found = 0; // Flag to track if command is found in any PATH dir
  char full_path[MAXIMUM_PATH];

  // Go through each dir in PATH
  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[1]);

    if (access(full_path, X_OK) == 0) {
      printf("%s is %s\n", args[1], full_path);
      found = 1;
      break;
    }

    dir = strtok(NULL, ":"); // Move to next dir
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

  // Gets current working dir
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

  char *path_copy = strdup(path_env); // Make a copy since strtok modifies the string
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
    // Parent: wait for child
    waitpid(pid, NULL, 0);
  } else {
    perror("fork");
  }

  free(path_copy);
}

int main() {
  setbuf(stdout, NULL); // Disables output buffering so we see "$ " right away
  char input[MAX_INPUT]; // Storage for the user's input
  char *args[MAX_ARGS]; // Array to hold command and arguments

  while (1) {
    printf("$ ");

    // fgets() returns NULL if the unordered user types Ctrl+D (EOF)
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break; // If Ctrl+D is pressed (EOF), exit the loop and end the shell
    }

    input[strcspn(input, "\n")] = '\0'; // Remove newline

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
      exit(0);
    } else {
      run_external_cmd(args);
    }

    // Free allocated arguments
    for (int i = 0; args[i] != NULL; i++) {
      free(args[i]);
      args[i] = NULL; // Set to NULL for safety
    }
  }

  return 0;
}