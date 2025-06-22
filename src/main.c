#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main() {
  // Flush after every printf
  setbuf(stdout, NULL);

  char input[100];

  printf("$ ");

  while (fgets(input, sizeof(input), stdin) != NULL) {
    input[strcspn(input, "\n")] = '\0';

    if (strcmp(input, "exit 0") == 0) {
      exit(0); 
    }

    printf("%s: command not found\n", input);
    printf("$ ");
  }

  return 0;
}
