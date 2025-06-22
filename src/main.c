#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
  setbuf(stdout, NULL);  // disables output buffering so we see "$ " right away

  char input[100];       // storage for the user's input (up to [100] is 99 characters + null terminator)
  int echo_char_count = 5;

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
    else if (strncmp(input, "echo ", echo_char_count) == 0)
    {
      char *message = input + echo_char_count;
      printf("%s\n", message);
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
