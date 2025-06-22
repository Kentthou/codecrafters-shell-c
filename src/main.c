#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_INPUT 128
#define CMD_OFFSET 5

// Checks if a command is shell builtins
int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "type") == 0;
}

// Handles echo functionality
void handle_echo(const char *input) {
  if (strcmp(input, "echo") == 0) {
    printf("\n");  // prints a blank line
  } else {
    const char *message = input + CMD_OFFSET;
    printf("%s\n", message);  // prints message after "echo "
  }
}

// Handles the type command, checking builtins and PATH
void handle_type(const char *arg) {
  // First: check if shell builtin
  if (is_builtin(arg)) {
    printf("%s is a shell builtin\n", arg);
    return;  // stop here, no need to check PATH
  }

  // Get the PATH environment variable
  char *path_env = getenv("PATH");
  char *path_copy = strdup(path_env);  // make a copy since strtok modifies the string
  char *dir = strtok(path_copy, ":");  // gets the first directory

  int found = 0;  // flag to track if command is found in any PATH dir
  char full_path[256];

  // Go through each dir in PATH
  while (dir != NULL) {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, arg);

    if (access(full_path, X_OK) == 0) {
      printf("%s is %s\n", arg, full_path);
      found = 1;
      break;
    }

    dir = strtok(NULL, ":");  // move to next dir
  }

  if (!found) {
    printf("%s: not found\n", arg);  // if nothing matched in PATH
  }

  free(path_copy);
}

int main() {
  setbuf(stdout, NULL);  // disables output buffering so we see "$ " right away

  char input[MAX_INPUT];  // storage for the user's input (up to 127 characters + null terminator)

  while (1) {  // infinite loop – keeps the shell running until we break
    printf("$ ");

    // fgets() returns NULL if the user types Ctrl+D (EOF)
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;  // if Ctrl+D is pressed (EOF), exit the loop and end the shell
    }

    input[strcspn(input, "\n")] = '\0';  // strip newline from input

    if (strncmp(input, "echo", 4) == 0) {
      handle_echo(input);  // handles echo commands
    }
    else if (strncmp(input, "type ", CMD_OFFSET) == 0) {
      handle_type(input + CMD_OFFSET);  // handles type command
    }
    else if (strcmp(input, "exit 0") == 0) {
      exit(0);  // exits the shell
    }
    else {
      printf("%s: command not found\n", input);  // fallback for unknown commands
    }
  }

  return 0;
}
