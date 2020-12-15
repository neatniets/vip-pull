#include "json-parse.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

static int intcmp(const void *a, const void *b);
static int trackcmp(const void *a, const void *b);

struct track_source {
        struct track track;
        char *s_filename;
};
static int get_track(struct track_source *trackptr,
                     FILE *json_file);

/* turns the 'export chosen' and 'export sourced' strings into an array of
 * numbers */
struct parse_export_args {
        const char *export_str;
        int *ids;
        int num_ids;
};
static void *thrd_parse_export_str(void *args);
/* checks the number of '\' characters preceeds a character and determines if
 * that character is escaped or not */
static bool is_escaped(const char *str,
                       size_t ind2check);
/* skips past whitespace in file */
static int fgetws(FILE *fp);
/* unescapes escaped \\ and \" characters */
static void unesc(char *str);

/* works like getdelim, but persists past escaped (with a '\' before)
 * characters */
ssize_t getdelime(char **restrict lineptr,
                  size_t *restrict n,
                  int delim,
                  FILE *restrict stream) {
        ssize_t len = 0;

        char *lineptr2 = NULL;
        size_t n2 = 0;

        ssize_t len2;
        while (len2 = getdelim(&lineptr2, &n2, delim, stream), len2 > 0) {
                /* if not enough space for new string + null term, realloc */
                if (len + len2 >= *n) {
                        *n += n2;
                        *lineptr = realloc(*lineptr, *n * sizeof(**lineptr));
                }
                /* copy contents + null term of lineptr2's string */
                memcpy(*lineptr + len, lineptr2, len2 + 1);
                len += len2;

                /* if the found delimiter has not been escaped, break;
                 * if len2 is only 1 character, it must be the delimiter */
                if (!is_escaped(lineptr2, len2 - 1)) {
                        break;
                }
        }
        /* if an error returned not due to EOF, return an error */
        if ((len2 < 0) && (!feof(stream))) {
                len = -1;
        }

        free(lineptr2);
        return len;
}

int next_field(struct json_field *restrict field,
               FILE *restrict json_file) {
        *field = (struct json_field){.key = NULL, .val = NULL};
        size_t keyn = 0; // allocated size for key
        ssize_t keylen; // string length of key
        size_t valn = 0; // allocated size for val
        ssize_t vallen; // string length of key

        bool field_not_found = true;
        while (field_not_found) {
                /* KEY */
                /* consume all characters leading up to first \" */
                while (fgetc(json_file) != '"') {
                        if (feof(json_file)) {
                                goto fail;
                        }
                }
                /* get key */
                keylen = getdelime(&field->key, &keyn, '"', json_file);
                if (keylen <= 0) {
                        goto fail;
                }
                /* get rid of possible whitespace */
                fgetws(json_file);
                /* check for colon indicating key-val pair */
                if (fgetc(json_file) != ':') {
                        field_not_found = true;
                        continue;
                }
                /* remove delimiter from string */
                field->key[keylen - 1] = '\0';
                keylen--;

                /* VAL */
                /* remove possible whitespace */
                fgetws(json_file);
                /* check what type of value we're looking at */
                char ch;
                ch = fgetc(json_file);
                if ((ch == '{') || (ch == '[')) { // skip objects/arrays
                        field_not_found = true;
                } else if (ch == '"') { // value is string
                        /* get string up to \" */
                        vallen = getdelime(&field->val, &valn, '"', json_file);
                        /* fail if EOF reached */
                        if (vallen <= 0) {
                                goto fail;
                        }
                        /* remove delimiter */
                        field->val[vallen - 1] = '\0';
                        vallen--;
                        field_not_found = false;
                } else { // value is number
                        /* there might already be a string in val */
                        vallen = 0;
                        /* make some space */
                        if (valn == 0) {
                                valn = 2;
                                field->val = malloc(valn
                                                    * sizeof(*field->val));
                        }
                        /* fill string with digits */
                        do {
                                field->val[vallen] = ch;
                                vallen++;
                                /* expand string if necessary (including
                                 * null term) */
                                if (vallen + 1 > valn) {
                                        valn <<= 1;
                                        size_t tmp = sizeof(*field->val);
                                        field->val = realloc(field->val,
                                                             valn * tmp);
                                }
                        } while (ch = fgetc(json_file), isdigit(ch));
                        /* add null term */
                        field->val[vallen] = '\0';
                        field_not_found = false;
                        ungetc(ch, json_file);
                }
        }

        /* remove escape characters from strings */
        unesc(field->key);
        unesc(field->val);
        return 0;

fail:
        free(field->key);
        free(field->val);
        return -1;
}

int get_url_ext(char **restrict download_url,
                char **restrict file_ext,
                FILE *restrict json_file) {
        struct json_field field;
        *download_url = NULL;
        *file_ext = NULL;
        while ((*download_url == NULL) || (*file_ext == NULL)) {
                /* get next json field */
                if (next_field(&field, json_file) < 0) {
                        free(*download_url);
                        free(*file_ext);
                        return -1;
                }

                /* check field's key */
                if (strcmp(field.key, "url") == 0) { // url found
                        *download_url = field.val;
                } else if (strcmp(field.key, "ext") == 0) { // ext found
                        *file_ext = field.val;
                } else {
                        free(field.val);
                }
                free(field.key);
        }

        return 0;
}

int get_all_tracks(struct track **restrict tracks,
                   const char *restrict export_chosen_str,
                   const char *restrict export_sourced_str,
                   FILE *restrict json_file) {
        /* parse both export strings */
        pthread_t thrd_chosen;
        struct parse_export_args chosen_args = {
                .export_str = export_chosen_str
        };
        struct parse_export_args sourced_args = {
                .export_str = export_sourced_str
        };
        pthread_create(&thrd_chosen, NULL, thrd_parse_export_str,
                       &chosen_args);
        thrd_parse_export_str(&sourced_args);
        pthread_join(thrd_chosen, NULL);

        /* initialize chosen tracks */
        int num_tracks = chosen_args.num_ids;
        *tracks = malloc(num_tracks * sizeof(**tracks));
        for (int i = 0; i < num_tracks; i++) {
                (*tracks)[i].filename = NULL;
                (*tracks)[i].id = chosen_args.ids[i];
                /* if the ID is found in the sourced export string,
                 * flag the track as sourced */
                (*tracks)[i].is_sourced = (bsearch(&(*tracks)[i].id,
                                                   sourced_args.ids,
                                                   sourced_args.num_ids,
                                                   sizeof(*sourced_args.ids),
                                                   intcmp) != NULL);
        }

        /* get all tracks */
        while (!feof(json_file)) {
                struct track_source track;
                if (get_track(&track, json_file) < 0) {
                        continue;
                }
                /* try to find track in track list */
                struct track *trackptr;
                trackptr = bsearch(&track.track, *tracks, num_tracks,
                                   sizeof(**tracks), trackcmp);
                if (trackptr == NULL) { // if not in list, ignore
                        free(track.track.filename);
                        free(track.s_filename);
                } else { // else add the filename
                        if (trackptr->is_sourced) {
                                trackptr->filename = track.s_filename;
                                free(track.track.filename);
                        } else {
                                trackptr->filename = track.track.filename;
                                free(track.s_filename);
                        }
                }
        }

        int real_num_tracks = 0;
        for (int i = 0; i < num_tracks; i++) {
                if ((*tracks)[i].filename != NULL) {
                        (*tracks)[real_num_tracks] = (*tracks)[i];
                        real_num_tracks++;
                }
        }

        free(chosen_args.ids);
        free(sourced_args.ids);
        return real_num_tracks;
}

static bool is_escaped(const char *str,
                       size_t ind2check) {
        // keep track of how many backslashes preceed the character in question
        size_t backslash_count = 0;
        /* count backslashes until there isn't one or the beginning of the
         * string has been reached */
        for (size_t i = ind2check; i > 0; i--) {
                if (str[i - 1] == '\\') {
                        backslash_count++;
                } else {
                        break;
                }
        }
        /* if there's an odd number of backslashes before the character in
         * question, then the character has been escaped */
        return (backslash_count % 2);
}

static int fgetws(FILE *fp) {
        int num_ws = 0;
        char ch;
        while (ch = fgetc(fp), isspace(ch)) {
                num_ws++;
        }
        ungetc(ch, fp);
        return num_ws;
}

static void unesc(char *str) {
        size_t num_esc = 0;
        while (*str != '\0') {
                if (*str == '\\') {
                        num_esc++;
                        str++;
                }
                str[-num_esc] = *str;
                str++;
        }
        str[-num_esc] = *str;
}

static void *thrd_parse_export_str(void *args) {
        struct parse_export_args *argsv = args;
        /* set up array of ids */
        int max_ids = 1;
        argsv->num_ids = 0;
        argsv->ids = malloc(max_ids * sizeof(*argsv->ids));

        int scan_ret; // holds the return of sscanf
        int bytes_read; // holds number of bytes read
        /* keep looping until no more ids to read */
        while (scan_ret = sscanf(argsv->export_str,
                                 " %d%n",
                                 (argsv->ids) + argsv->num_ids,
                                 &bytes_read),
               scan_ret > 0) {
                /* advance string past what was read */
                argsv->export_str += bytes_read;
                if (*argsv->export_str == ',') {
                        argsv->export_str++;
                }
                argsv->num_ids++;
                /* make space if needed */
                if (argsv->num_ids >= max_ids) {
                        max_ids <<= 1;
                        argsv->ids = realloc(argsv->ids,
                                             max_ids * sizeof(*argsv->ids));
                }
        }
        qsort(argsv->ids, argsv->num_ids, sizeof(*argsv->ids), intcmp);

        return NULL;
}

static int intcmp(const void *a, const void *b) {
        const int *av = a;
        const int *bv = b;
        if (*av > *bv) {
                return 1;
        } else if (*av < *bv) {
                return -1;
        } else {
                return 0;
        }
}

static int get_track(struct track_source *trackptr,
                     FILE *json_file) {
        /* move up to first { for track */
        while (fgetc(json_file) != '{') {
                if (feof(json_file)) {
                        return -1;
                }
        }
        /* undo the last fgetc so as to not mess up the upcoming loop */
        ungetc('{', json_file);

        struct track_source track = {
                .track = {
                        .filename = NULL,
                        .id = -1
                },
                .s_filename = NULL
        };
        /* get track info */
        /* not every track has a source version; looking for '}' indicates the
         * end of the track object whether or not it has a source version */
        while (fgetws(json_file), fgetc(json_file) != '}') {
                struct json_field field;
                if (next_field(&field, json_file) < 0) {
                        free(track.track.filename);
                        free(track.s_filename);
                        return -1;
                }
                if (strcmp(field.key, "file") == 0) {
                        if (track.track.filename != NULL) {
                                free(track.track.filename);
                        }
                        track.track.filename = field.val;
                } else if (strcmp(field.key, "s_file") == 0) {
                        if (track.s_filename != NULL) {
                                free(track.s_filename);
                        }
                        track.s_filename = field.val;
                } else if (strcmp(field.key, "id") == 0) {
                        track.track.id = atoi(field.val);
                        free(field.val);
                } else {
                        free(field.val);
                }
                free(field.key);
        }

        *trackptr = track;
        return 0;
}

static int trackcmp(const void *a, const void *b) {
        const struct track *av = a;
        const struct track *bv = b;
        if (av->id > bv->id) {
                return 1;
        } else if (av->id < bv->id) {
                return -1;
        } else {
                return 0;
        }
}
