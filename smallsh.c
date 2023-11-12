#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
char * strip_string(char const * word);
void sigint_handler(int sig){};

pid_t foreground_pid = -4;
int foreground_status = 0;    // $?
pid_t background_pid = -4;    // $!
int bg_process;

int main(int argc, char *argv[])
{
  signal(SIGTSTP, SIG_IGN);
  FILE *input = stdin;
  char *input_fn = "(stdin)";             // fn means file name.
  if (argc == 2) {
    input_fn = argv[1];                   // if we read from a file, set it to file name.
    input = fopen(input_fn, "re");        // r is read. e is O_CLOEXEC flag
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;

  // always ignotr SIGTSTP;Ustruct sigaction 
  struct sigaction ign_sig_act = {0}; 
  ign_sig_act.sa_handler = SIG_IGN;
  struct sigaction getline_sig_act = {0};
  getline_sig_act.sa_handler = sigint_handler;
  struct sigaction default_sig_act = {0};
  default_sig_act.sa_handler = SIG_DFL;

  for (;;) {
  // Can use goto to jump back to here.
  prompt:;                                
    /* TODO: Manage background processes */
    int background_status = 0;
    pid_t unwaited_pid = waitpid(0, &background_status, WUNTRACED | WNOHANG);
     
    if (unwaited_pid > 0) {
      if (WIFEXITED(background_status)) {
        fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) unwaited_pid, WEXITSTATUS(background_status));  
      } else if (WIFSIGNALED(background_status)) {
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) unwaited_pid, WTERMSIG(background_status));
      } else if (WIFSTOPPED(background_status)) {
        fprintf(stderr, "Child process %d stopped. Continuing.\n", unwaited_pid);
        kill(0, SIGCONT);
      }
    };
    /* TODO: prompt */      // The prompt in smallsh assignment page.
    if (input == stdin) {   // if input == stdin, we're in interactive mode. otherwise it's a file.
      fprintf(stderr,"%s", getenv("PS1"));
      sigaction(SIGTSTP, &ign_sig_act, &default_sig_act);
      sigaction(SIGINT, &ign_sig_act, &default_sig_act);
      
      // only ignore SIGINT in interactive mode.
    } 

    bg_process = 0;
    // clearing out word so it doesn't retain garbage values.
    for (int i = 0; i < MAX_WORDS; i++) {
      words[i] = 0;
    }
    // reset errors and clear errno.
    clearerr(input);
    errno = 0;
    
    sigaction(SIGINT, &getline_sig_act,0);
    ssize_t line_len = getline(&line, &n, input);
    if (errno ==  EINTR) {
      fprintf(stderr, "\n");
      goto  prompt;
    }
    sigaction(SIGINT, &ign_sig_act, 0);

    // get line will return -1 for EOF and for errors, so we check for EOF to exit before it errors out.
    if (feof(input) != 0) {
      exit(0);
    } else if (line_len < 0) {
      err(1, "%s", input_fn);
    }
    // number of words. wordsplit puts line into individual words into words array.
    size_t nwords = wordsplit(line);
    
    if(nwords == 0) {
      goto prompt;
    }
    
    // check to see if background process.
    if(nwords > 1 && (strcmp(words[nwords-1], "&")) == 0) {
      bg_process = 1;
      words[nwords-1] = 0;
      nwords -= 1;
    }

    // Expand $$, $?, $! in words.
    for (size_t i = 0; i < nwords; ++i) {
      // fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      // fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
    }

    // builtin command for exit.
    if(strcmp(words[0], "exit") == 0) {
      if(nwords == 1) {
        exit(foreground_status);
      } else if(nwords > 2) {
        perror("Too many arguments for exit()");
        goto prompt;
      } else if (nwords == 2) {
        // atoi returns 0 if conversation cannot take place, so check to make sure the string is not "0" as well.
        if (atoi(words[1]) == 0 && strcmp(words[1], "0") != 0) {
          perror("Invalid argument");
          goto prompt;
        } else {
          foreground_status = atoi(words[1]);
          exit(foreground_status);
        }
      }
    }

    // builtin command for cd.
    if(strcmp(words[0], "cd") == 0) {
      if (nwords > 2) {
        perror("Too many arguments");
      } else if (nwords == 1) {
        chdir(getenv("HOME"));
      } else {
        int chdir_status = chdir(words[1]);
        if(chdir_status < 0) {
         perror("Invalid directory"); 
        }
      }
      goto prompt;
    }

    pid_t spawnPid = -5;
    int child_status;

    spawnPid = fork();
    if (spawnPid == -1) {
        perror("fork() failed\n");
        exit(1);
    } else if (spawnPid == 0) {

        // array of pointers to strings.
        char *args[MAX_WORDS] = {0};
        // copy words to args array, unless it's "<", ">", or ">>"
        int args_index = 0; 
        for(int i = 0; i < nwords; ++i) {
          if (strcmp(words[i], "<") == 0) {
            int fd = open(words[i + 1], O_RDONLY);
            if (fd == -1) {
              perror("Invalid input file");
            }
            int result = dup2(fd, 0);
            if (result == -1) {
              perror("source dup2 failed");
            }
            // increment i to skip the redirection filename
            i++;
          } else if (strcmp(words[i], ">") == 0) {
            int fd = open(words[i + 1], O_CREAT | O_WRONLY | O_TRUNC, 0777);
            if (fd == -1) {
              perror("Output fd failed");
            }
            int result = dup2(fd, 1);
            if (result == -1) {
              perror("output dup2 failed");
            }
            i++;
          } else if (strcmp(words[i], ">>") == 0) {
            int fd = open(words[i + 1], O_CREAT | O_WRONLY | O_APPEND, 0777);
            if (fd == -1) {
              perror("Appending fd failed");
            }
            int result = dup2(fd, 1);
            if (result == -1) {
              perror("appending dup2 failed");
            }
            i++;
          } else {
            args[args_index] = words[i];
            args_index++;
          }
        }
        execvp(args[0], args);
        perror("execvp() error");
        exit(1);
    } else { // parent process
      if(bg_process) {
        background_pid = spawnPid; 
        waitpid(spawnPid, &child_status, WNOHANG);
      } else {
        background_pid = spawnPid;
        waitpid(spawnPid, &child_status, WUNTRACED);
        if (WIFSIGNALED(child_status)) {
          foreground_status = 128 + WTERMSIG(child_status);
        } else if (WIFEXITED(child_status)) {
          foreground_status = WEXITSTATUS(child_status);
        } else if (WIFSTOPPED(child_status)) {
          kill(spawnPid, SIGCONT);
          fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) spawnPid);
        }
      }
    }  
  }
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;      // word length
  size_t wind = 0;      // word index

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';       // adds null termination to string, not the array
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}

/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}
/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') {
      char my_pid[6] = {0};
      // if no existing background pid, set to ""
      if (background_pid < -1) {
        sprintf(my_pid, "%s", "");
      } else {
        sprintf(my_pid, "%d", background_pid);
      }
      build_str(my_pid, NULL);
    } else if (c == '$') {
      int pid = getpid();
      char my_pid[6];
      sprintf(my_pid, "%d", pid);
      build_str(my_pid, NULL);
      // status of last run process.
    } else if (c == '?') {
      char stat[6];
      sprintf(stat, "%d", foreground_status);
      build_str(stat, NULL);
    } else if (c == '{') {
      size_t str_length = end - start - 3;
      char * new_string = calloc(str_length, sizeof(char));
      memcpy(new_string, start + 2, str_length);
      build_str(getenv(new_string), NULL); 
      //build_str("<Parameter: ", NULL);
      //build_str(start + 2, end - 1);
      //build_str(">", NULL);
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
// work cited: https://stackoverflow.com/questions/53230155/converting-pid-t-to-string

