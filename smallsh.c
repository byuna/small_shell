#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

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
  prompt:;                                // Can use goto to jump back to here.
    /* TODO: Manage background processes */

    /* TODO: prompt */      // The prompt in smallsh assignment page.
    if (input == stdin) {   // if input == stdin, we're in interactive mode. otherwise it's a file.
      fprintf(stderr, "$");
    }

    //  Reads an line from stream and sets pointer line to it.
    //  %n is the size.
    //  reads from input.
    ssize_t line_len = getline(&line, &n, input);       // Read getline man pages.
    if (line_len < 0) err(1, "%s", input_fn);
   
    // number of words. wordsplit puts line into individual words into words array. 
    size_t nwords = wordsplit(line);
    
    if(strcmp(words[0], "exit") == 0) {
      exit(0);
    }

    if(strcmp(words[0], "cd") == 0) {
      int success = chdir(words[1]);

      if(success == -1) {
        perror("Unable to find diretory");
      }
    }
   
    if(strcmp(words[0], "pwd") == 0) {
      pid_t spawnPid = -5;                   // garbage value
      
      spawnPid = fork();
      if (spawnPid == 0) {
        execlp("pwd", words[0], NULL);
      }
    }

    /* Eventually comment out the below 6 lines */
    /*
    for (size_t i = 0; i < nwords; ++i) {
      fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      fprintf(stderr, "Expanded Word %zu: %s\n", i, words[i]);
    }
    */
    /* COMMENT OUT TO HERE! */

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
      words[wind][wlen] = '\0';
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
  *end = NULL;
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
    if (c == '!') build_str("<BGPID>", NULL);
    else if (c == '$') build_str("<PID>", NULL);
    else if (c == '?') build_str("<STATUS>", NULL);
    else if (c == '{') {
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
