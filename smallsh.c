#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <stdbool.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
char * strip_string(char const * word);

int foreground_status = 0;    // $?
pid_t background_pid = -4;    // $!
bool bg_process;

int main(int argc, char *argv[])
{
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

  for (;;) {
  // Can use goto to jump back to here.
  prompt:;                                
    /* TODO: Manage background processes */
    bg_process = false;
    /* TODO: prompt */      // The prompt in smallsh assignment page.
    if (input == stdin) {   // if input == stdin, we're in interactive mode. otherwise it's a file.
      fprintf(stderr, "$");
    }
   
  redirection_input:;
    // clearing out word so it doesn't retain garbage values.
    for (int i = 0; i < MAX_WORDS; i++) {
      words[i] = 0;
    }
    // reset errors and clear errno.
    clearerr(input);
    errno = 0;

    ssize_t line_len = getline(&line, &n, input);
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
      bg_process = true;
    }

    // Expand $$, $?, $! in words.
    for (size_t i = 0; i < nwords; ++i) {
      //fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      //fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
    }

/*
    char *command = words[0];
    size_t nargs = nwords;
    char **args[MAX_WORDS] = {0};

    // an array of pointers to strings.
    for(int i = 0; i < nwords; ++i) {
      if(words[i] != NULL) {
        args[i] = &words[i + 1];
      }
    }
*/

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
    
    switch(spawnPid) {
      case -1:
        perror("fork() failed");
        exit(1);
        break;
      case 0:   // Child fork()
        if (bg_process) {
          words[nwords - 1] = 0;
        }
        // array of pointers to strings.
        char **args[MAX_WORDS] = {0};
        // copy words to args array, unless it's "<", ">", or ">>"
        for(int i = 0; i < nwords; ++i) {
          if (strcmp(words[i], "<") == 0) {
             // change input to filename i + 1; skip to i + 2;
            fclose(input);
            input = fopen(words[i + 1], "r");
            if (input == NULL) {
              perror("Invalid file for input");
            } else {
              continue;
            }
            i++;
          } else if (strcmp(words[i], ">") == 0) {
            // change output to file name and remove i and i + 1; skip to i + 2.
            close(1);
            freopen(words[i + 1], "w+", stdout);
            i++;
          } else if (strcmp(words[i], ">>") == 0) {
            i++;
          } else {
            args[i] = &words[i];
          }
        }

        execvp(*args[0], *args);
        perror("execvp() error");
        exit(1);
        break;
      default:  // Parent process. 
        if(bg_process) {
          background_pid = spawnPid; 
          waitpid(spawnPid, &child_status, WNOHANG);
        } else {
          waitpid(spawnPid, &child_status, 0);
          if (WIFSIGNALED(child_status) != 0) {
            foreground_status = 128 + WTERMSIG(child_status);
          } else {
            foreground_status = WEXITSTATUS(child_status); 
          }
        }
        break;
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
