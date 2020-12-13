#ifndef JSON_PARSE_H
#define JSON_PARSE_H

#include <stdio.h>
#include <stdbool.h>

/* works like getdelim, but persists past escaped (with a '\' before)
 * characters */
ssize_t getdelime(char **restrict lineptr,
                  size_t *restrict n,
                  int delim,
                  FILE *restrict stream);

struct json_field {
        char *key;
        char *val;
};
int next_field(struct json_field *restrict field,
               FILE *restrict json_file);
int get_url_ext(char **restrict download_url,
                char **restrict file_ext,
                FILE *restrict json_file);
struct track {
        char *filename;
        int id;
        bool is_sourced;
};
int get_all_tracks(struct track **restrict tracks,
                   const char *restrict export_chosen_str,
                   const char *restrict export_sourced_str,
                   FILE *restrict json_file);

#endif /* !JSON_PARSE_H */
