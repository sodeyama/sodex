#ifndef _USR_ESHELL_PARSER_H
#define _USR_ESHELL_PARSER_H

#define ESHELL_ARGV_MAX 4
#define ESHELL_MAX_COMMANDS 8
#define ESHELL_MAX_TOKENS 32
#define ESHELL_STORAGE_SIZE 256

struct eshell_command {
  char *argv[ESHELL_ARGV_MAX];
  int argc;
  char *input_path;
  char *output_path;
  int append_output;
};

struct eshell_pipeline {
  struct eshell_command commands[ESHELL_MAX_COMMANDS];
  int command_count;
  char storage[ESHELL_STORAGE_SIZE];
};

int eshell_parse_line(char *line, int len, struct eshell_pipeline *pipeline);

#endif /* _USR_ESHELL_PARSER_H */
