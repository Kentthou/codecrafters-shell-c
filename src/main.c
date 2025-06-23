#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_INPUT 128
#define CMD_OFFSET 5
#define MAX_ARGS 16
#define MAXIMUM_PATH 4096

int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "type") == 0;
}

void handle_echo(const char *input) {
  if (strcmp(input, "echo") == 0) {
    printf("\n");  // prints a blank line
  } else {
    const char *message = input + CMD_OFFSET;
    printf("%s\n", message);  // prints message after "echo "
  }
}

void handle_type(const char *arg) {
  // First: check if shell builtin
  if (is_builtin(arg)) {
    printf("%s is a shell builtin\n", arg);
    return;  // stop here, no need to check PATH
  }

  // Get the PATH environment variable
  char *path_env = getenv("PATH");
  if (!path_env) {
    fprintf(stderr, "PATH not set\n");
    return;
  }

  char *path_copy = strdup(path_env);  // make a copy since strtok modifies the string
  char *dir = strtok(path_copy, ":");  // gets the first directory
  int found = 0;  // flag to track if command is found in any PATH dir
  char full_path[MAXIMUM_PATH];

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

void handle_pwd() {
  char full_path[MAXIMUM_PATH];
  if (getcwd(full_path, sizeof(full_path)) != NULL) {
    printf("%s\n", full_path);
  } else {
    perror("getcwd failed");
  }
}

void handle_cd(const char*path) {
  char current_dir[MAXIMUM_PATH];
  char resolved_path[MAXIMUM_PATH];

  if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
    perror("getcwd failed");
    return;
  }

  if (realpath(path, resolved_path) == NULL) {
    fprintf(stderr, "cd: %s: No such file or directory\n", path);
    return;
  }

  if (chdir(resolved_path) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", path);
  }
}

void run_external_cmd(char **args)
{
  char *path_env = getenv("PATH");
  char * path_copy = strdup(path_env);
  char *dir = strtok(path_copy, ":");
  char full_path[256];
  int found = 0;

  while (dir != NULL)
  {
    snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);
    if (access(full_path, X_OK) == 0) 
    {
      found = 1;
      break;
    }
    dir = strtok(NULL, ":");
  }

  if (!found)
  {
    printf("%s: command not found\n", args[0]);
    free(path_copy);
    return;
  }

  pid_t pid = fork();

  if (pid == 0) 
  {
    execv(full_path, args); // Child process: try to run the command
    perror("execv");
    exit(1);
  }
  else if (pid > 0)
  {
    // parent: wait for child
    waitpid(pid, NULL, 0);
  }
  else
  {
    perror("fork");
  }

  free(path_copy);
}

int main() {
  setbuf(stdout, NULL);  // disables output buffering so we see "$ " right away

  char input[MAX_INPUT];  // storage for the user's input (up to 127 characters + null terminator)

  while (1) {  // infinite loop – keeps the shell running until break
    printf("$ ");

    // fgets() returns NULL if the user types Ctrl+D (EOF)
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;  // if Ctrl+D is pressed (EOF), exit the loop and end the shell
    }

    input[strcspn(input, "\n")] = '\0';

    if (strncmp(input, "echo", 4) == 0) {
      handle_echo(input);
    }
    else if (strncmp(input, "type ", CMD_OFFSET) == 0) {
      handle_type(input + CMD_OFFSET); 
    }
    else if (strcmp(input, "exit 0") == 0) {
      exit(0);
    }
    else if (strcmp(input, "pwd") == 0){
      handle_pwd();
    }
    else {
      // parse input into args
      char *args[MAX_ARGS];
      int i = 0;

      char *token = strtok(input, " "); // get first word
      while (token  != NULL && i < MAX_ARGS - 1)
      {
        args[i++] = token; // store it
        token = strtok(NULL, " "); // get next word
      }
      args[i] = NULL; // terminate list

      if (args[0] != NULL)
      {
        if (strcmp(args[0], "cd") == 0) {
          if (args[1] == NULL) {
            fprintf(stderr, "cd: missing operand\n");
          } 
          else {
            handle_cd(args[1]);
          }
        } 
        else {
          run_external_cmd(args);
        }
      }
    }
  }

  return 0;
}
