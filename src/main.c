#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
  setbuf(stdout, NULL);  // disables output buffering so we see "$ " right away

  char input[128];       // storage for the user's input (up to [100] is 99 characters + null terminator)
  int cmd_offset = 5;

  while (1) {            // infinite loop – keeps the shell running until we break
    printf("$ ");

    // fgets() returns NULL if the user types Ctrl+D (EOF)
    if (fgets(input, sizeof(input), stdin) == NULL) {
      break;             // if Ctrl+D is pressed (EOF), exit the loop and end the shell
    }

    input[strcspn(input, "\n")] = '\0';

    if (strcmp(input, "echo") == 0)
    {
      printf("\n");
    }
    else if (strncmp(input, "echo ", cmd_offset) == 0)
    {
      char *message = input + cmd_offset;
      printf("%s\n", message);
    }
    else if (strncmp(input, "type ", cmd_offset) == 0)
    {
      char *check_type = input + cmd_offset;

      // First: check if shell builtin
      if (
        strcmp(check_type, "echo") == 0 ||
        strcmp(check_type, "exit") == 0 ||
        strcmp(check_type, "type") == 0
      ) {
        printf("%s is a shell builtin\n", check_type);
        continue; //stop here, no need to check PATH
      }

      // Get the PATH environment variable
      char *path_env = getenv("PATH");
      char *path = strdup(path_env); // make copy, strtok modifies the string
      char *dir = strtok(path, ":"); // gets the first directory

      int found = 0; // flag to track if command is found in any PATH dir
      char full_path[256];

      //go through each dir in PATH
      while (dir != NULL) 
      {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, check_type);

        if (access(full_path, X_OK) == 0)
        {
          printf("%s is %s\n", check_type, full_path);
          found = 1;
          break;
        }

        dir = strtok(NULL, ":"); //move to next dir
      }

      if (!found)
      {
        printf("%s: not found \n", check_type);
      }

      free(path);
    }
    else if (strcmp(input, "exit 0") == 0) 
    {
      exit(0);
    }
    else
    {
      printf("%s: command not found\n", input);
    }
  }

  return 0;
}
