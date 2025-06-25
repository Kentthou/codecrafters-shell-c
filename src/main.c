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

// Builtin commands
const char *builtin_names[] = {"echo", "exit", "pwd", "cd", "type", NULL};

int is_builtin(const char *cmd) {
  return strcmp(cmd, "echo") == 0 ||
         strcmp(cmd, "exit") == 0 ||
         strcmp(cmd, "pwd") == 0 ||
         strcmp(cmd, "cd") == 0 ||
         strcmp(cmd, "type") == 0;
}

// Readline generator: suggest builtins matching 'text'
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

// Hook for readline completion
char **my_completion(const char *text, int start, int end) {
  rl_attempted_completion_over = 1;
  return rl_completion_matches(text, command_generator);
}

// Split input into args[]
void parse_input(char *input, char **args) {
  int i = 0, arg_index = 0;
  char buf[MAX_INPUT]; int b = 0;
  int in_sq = 0, in_dq = 0;
  while (input[i]) {
    char c = input[i];
    if (in_sq) {
      if (c == '\'') in_sq = 0;
      else buf[b++] = c;
    } else if (in_dq) {
      if (c == '\\' && (input[i+1]=='"'||input[i+1]=='\\')) buf[b++] = input[++i];
      else if (c == '"') in_dq = 0;
      else buf[b++] = c;
    } else {
      if (c=='\\' && input[i+1]) buf[b++] = input[++i];
      else if (c=='\'') in_sq = 1;
      else if (c=='"') in_dq = 1;
      else if (c==' '||c=='\t') {
        if (b) { buf[b] = '\0'; args[arg_index++] = strdup(buf); b=0; }
      } else buf[b++] = c;
    }
    i++;
  }
  if (b) { buf[b] = '\0'; args[arg_index++] = strdup(buf); }
  args[arg_index] = NULL;
}

// Builtin handlers
void handle_echo(char **args) {
  for (int i=1; args[i]; i++) {
    printf("%s", args[i]);
    if (args[i+1]) printf(" ");
  }
  printf("\n");
}
void handle_type(char **args) {
  if (!args[1]) { fprintf(stderr, "type: missing argument\n"); return; }
  if (is_builtin(args[1])) { printf("%s is a shell builtin\n", args[1]); return; }
  char *path = getenv("PATH"); if (!path) { fprintf(stderr, "PATH not set\n"); return; }
  char *cp = strdup(path);
  char *dir = strtok(cp, ":"); char full[MAXIMUM_PATH]; int found = 0;
  while (dir) {
    snprintf(full, sizeof(full), "%s/%s", dir, args[1]);
    if (access(full, X_OK)==0) { printf("%s is %s\n", args[1], full); found=1; break; }
    dir = strtok(NULL, ":");
  }
  if (!found) printf("%s: not found\n", args[1]);
  free(cp);
}
void handle_pwd() {
  char full[MAXIMUM_PATH];
  if (getcwd(full, sizeof(full))) printf("%s\n", full);
  else perror("getcwd failed");
}
void handle_cd(char **args) {
  char rp[MAXIMUM_PATH]; char *t = args[1] ? args[1] : getenv("HOME");
  if (!t) { fprintf(stderr, "cd: HOME not set\n"); return; }
  if (!realpath(t, rp)) { fprintf(stderr, "cd: %s: No such file or directory\n", t); return; }
  if (chdir(rp)!=0) fprintf(stderr, "cd: %s: No such file or directory\n", t);
}

// Run external program
void run_external_cmd(char **args) {
  char *path = getenv("PATH"); if (!path) { fprintf(stderr, "PATH not set\n"); return; }
  char *cp = strdup(path);
  char *dir = strtok(cp, ":"); char full[MAXIMUM_PATH]; int found=0;
  while (dir) {
    snprintf(full, sizeof(full), "%s/%s", dir, args[0]);
    if (access(full, X_OK)==0) { found=1; break; }
    dir = strtok(NULL, ":");
  }
  if (!found) { fprintf(stderr, "%s: command not found\n", args[0]); free(cp); return; }
  pid_t pid = fork();
  if (pid==0) { execv(full, args); perror("execv"); exit(1); }
  else if (pid>0) waitpid(pid, NULL, 0);
  else perror("fork");
  free(cp);
}

// Free arguments
void free_args(char **args) {
  for (int i=0; args[i]; i++) free(args[i]);
}

int main() {
  // Setup Readline completion
  rl_attempted_completion_function = my_completion;
  setbuf(stdout, NULL);

  while (1) {
    // Read a line of input
    char *line = readline("$ ");
    if (!line) break;            // Ctrl-D
    if (*line) add_history(line);

    // Parse into args
    char *args[MAX_ARGS]; memset(args, 0, sizeof(args));
    char buf[MAX_INPUT]; strncpy(buf, line, MAX_INPUT-1); buf[MAX_INPUT-1]='\0';
    free(line);
    parse_input(buf, args);
    if (!args[0]) { free_args(args); continue; }

    // Handle redirection
    int rd_fd = 0, rd_mode = 0, rd_idx = -1;
    for (int j=0; args[j]; j++) {
      if (strcmp(args[j], "2>>")==0) { rd_fd=2; rd_mode=1; rd_idx=j; break; }
      if (strcmp(args[j], "2>")==0)  { rd_fd=2; rd_mode=0; rd_idx=j; break; }
      if (strcmp(args[j], ">>")==0 || strcmp(args[j], "1>>")==0) { rd_fd=1; rd_mode=1; rd_idx=j; break; }
      if (strcmp(args[j], ">")==0 || strcmp(args[j], "1>")==0)   { rd_fd=1; rd_mode=0; rd_idx=j; break; }
    }
    int orig_fd=-1, out_fd=-1;
    if (rd_idx>=0 && args[rd_idx+1]) {
      char *file = args[rd_idx+1]; args[rd_idx]=NULL;
      int flags = O_WRONLY|O_CREAT | (rd_mode?O_APPEND:O_TRUNC);
      out_fd = open(file, flags, 0666);
      if (out_fd<0) { perror("open"); }
      else {
        orig_fd = dup(rd_fd);
        dup2(out_fd, rd_fd);
        close(out_fd);
      }
    }

    // Execute
    if (strcmp(args[0], "echo")==0) handle_echo(args);
    else if (strcmp(args[0], "type")==0) handle_type(args);
    else if (strcmp(args[0], "pwd")==0) handle_pwd();
    else if (strcmp(args[0], "cd")==0) handle_cd(args);
    else if (strcmp(args[0], "exit")==0 && (!args[1] || strcmp(args[1],"0")==0)) {
      free_args(args);
      if (orig_fd>=0) { dup2(orig_fd, (rd_fd>0?rd_fd:1)); close(orig_fd); }
      break;
    } else run_external_cmd(args);

    // Restore **fd**
    if (orig_fd>=0) { dup2(orig_fd, rd_fd); close(orig_fd); }
    free_args(args);
  }
  return 0;
}
