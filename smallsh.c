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

int fore_stat;
int back_stat;
bool b_process = false;

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

  // For use in expansion. Set to garbage values.

  for (;;) {
  // Can use goto to jump back to here.
  prompt:;                                
    /* TODO: Manage background processes */

    /* TODO: prompt */      // The prompt in smallsh assignment page.
    if (input == stdin) {   // if input == stdin, we're in interactive mode. otherwise it's a file.
      fprintf(stderr, "$");
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
    
    if(words[0] == 0) {
      goto prompt;
    }
    
    // builtin command for exit.
    if(strcmp(words[0], "exit") == 0) {
      exit(0);
    }

    // builtin command for cd.
    if(strcmp(words[0], "cd") == 0) {
      int chdirStatus = chdir(words[1]);       // might have to repeat for backslashes? until all words isn't null?
   
      if(chdirStatus == -1) {
        perror("Error");
      } else {
        chdir(words[1]);
      }
      goto prompt;
    }
    
    // Expand $$, $?, $! in words.
    for (size_t i = 0; i < nwords; ++i) {
     // fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      //fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
    }

    pid_t spawnPid = -5;
    int childExitMethod;

    spawnPid = fork();
    
    switch(spawnPid) {
      case -1:
        perror("fork() failed");
        exit(1);
        break;
      case 0:
        execvp(words[0], words);
        perror("execvp() error");
        exit(1);
        break;
      default:
        fore_stat = waitpid(spawnPid, &childExitMethod, 0);
        break;
    }

    // reset words array to NULL so it doesn't contain garbage values.
    // turn this into a helper function if i have time.
    for (int i = 0; i < MAX_WORDS; i++) {
      words[i] = 0;
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
param_scan(char const *word, char **start, char **end)
{
  static char *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = NULL;
  // returns a pointer to first occurnce of char or null if char not found.
  // searches for the first instance of a character in a string.
  // returns a pointer to first occurence of char or null if char not found.
  char *s = strchr(word, '$');
  if (s) {
    char *c = strchr("$!?", s[1]);
    if (c) {
      ret = *c;
      *start = s;
      *end = s + 2;
    }
    else if (s[1] == '{') {
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = '{';
        *start = s;
        *end = e + 1;
      }
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
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') {
      build_str("<BGPID>", NULL);
    } else if (c == '$') {
      // work cited.
      // https://stackoverflow.com/questions/53230155/converting-pid-t-to-string
      int pid = getpid();
      char mypid[6];
      sprintf(mypid, "%d", pid);
      build_str(mypid, NULL);
    } else if (c == '?') {
      char stat[6];
      sprintf(stat, "%d", fore_stat);
      build_str(stat, NULL);
    } else if (c == '{') {
      build_str("<Parameter: ", NULL);
      build_str(start + 2, end - 1);
      build_str(">", NULL);
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
