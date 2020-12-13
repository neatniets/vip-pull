#include "json-parse.h"
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define PROG_NAME        "vip-pull"
#define VIP_SRC_DIR      "source/"

struct dir_namelist {
        const char *dirpath;      // assigned prior to dir_read call
        char **namelist;          // assigned by dir_read call
        int num_names;            // assigned by dir_read call
};
static void *dir_read(void *dir_namelist);
static int mystrcmp(const void *a, const void *b) {
        int result = strcmp(*(char **)a, *(char **)b);
        /*
        printf("a: %s\n"
               "b: %s\n"
               "result: %d\n",
               *(char **)a, *(char **)b, result);
        */
        return result;
}

enum args {
        ARGV_DIR = 1,
        ARGV_CHOSEN,
        ARGV_SOURCED
};
int main(int argc, char **argv) {
        int err_code = 0;
        if (argc < 4) {
                fputs("error: invalid arguments\n"
                      "should have the form:\n"
                      PROG_NAME" music-dir \"chosen export\" "
                      "\"source export\"\n\n",
                      stderr);
                return -1;
        }
        if (isatty(STDIN_FILENO)) {
                fputs("error: input should be VIP JSON redirected\n\n",
                      stderr);
                return -1;
        }

        /* call thread to populate a list of all directory entries */
        pthread_t thrd;
        struct dir_namelist dnl = {.dirpath = argv[ARGV_DIR]};
        pthread_create(&thrd, NULL, dir_read, &dnl);

        /* get url and ext */
        char *download_url;
        char *file_ext;
        err_code = get_url_ext(&download_url, &file_ext, stdin);
        if (err_code < 0) {
                pthread_join(thrd, NULL);
                fputs("failed to parse download url or file extension\n",
                      stderr);
                goto cleanup;
        }

        /* get tracks */
        struct track *tracks;
        int num_tracks;
        num_tracks = get_all_tracks(&tracks, argv[ARGV_CHOSEN],
                                    argv[ARGV_SOURCED], stdin);

        /* join thread to retrieve list of directory entries */
        pthread_join(thrd, NULL);
        if (dnl.num_names == 0) {
                fprintf(stderr, "failed to read directory %s\n", dnl.dirpath);
                err_code = -1;
                goto cleanup;
        }

        /* add extension to strings */
        const size_t extlen = strlen(file_ext);
        for (int i = 0; i < num_tracks; i++) {
                size_t tracklen = strlen(tracks[i].filename);
                tracks[i].filename = realloc(tracks[i].filename,
                                             (tracklen + extlen + 2)
                                             * sizeof(*tracks[i].filename));
                tracks[i].filename[tracklen] = '.';
                memcpy(tracks[i].filename + tracklen + 1,
                       file_ext, extlen + 1);
        }

        /* compare to directory entries and print un-downloaded files */
        for (int i = 0; i < num_tracks; i++) {
                if (bsearch(&tracks[i].filename, dnl.namelist,
                            dnl.num_names, sizeof(*dnl.namelist),
                            mystrcmp) == NULL) {
                        fputs(download_url, stdout);
                        if (tracks[i].is_sourced) {
                                fputs(VIP_SRC_DIR, stdout);
                        }
                        puts(tracks[i].filename);
                }
        }

        /*
        printf("DOWNLOAD URL: %s\n", download_url);
        printf("FILE EXTENSION: %s\n", file_ext);
        putchar('\n');

        puts("DIRECTORY ENTRIES:");
        for (int i = 0; i < dnl.num_names; i++) {
                puts(dnl.namelist[i]);
        }

        puts("TRACKS:");
        printf("NUMBER OF TRACKS = %d\n", num_tracks);
        for (int i = 0; i < num_tracks; i++) {
                fputs("filename = ", stdout);
                if (tracks[i].filename != NULL) {
                        puts(tracks[i].filename);
                } else {
                        putchar('\n');
                }
                printf("id = %d\n", tracks[i].id);
        }
        */

cleanup:
        /* free stuff */
        for (int i = 0; i < dnl.num_names; i++) {
                free(dnl.namelist[i]);
        }
        free(dnl.namelist);
        for (int i = 0; i < num_tracks; i++) {
                free(tracks[i].filename);
        }
        free(tracks);
        free(download_url);
        free(file_ext);

        return err_code;
}

static void *dir_read(void *dir_namelist) {
        struct dir_namelist *dnl = dir_namelist;
        struct dirent **namelist;
        dnl->num_names = scandir(dnl->dirpath, &namelist,
                                 NULL, alphasort);
        dnl->namelist = malloc(dnl->num_names * sizeof(*dnl->namelist));
        for (int i = 0; i < dnl->num_names; i++) {
                size_t strsz = strlen(namelist[i]->d_name) + 1;
                dnl->namelist[i] = malloc(strsz);
                memcpy(dnl->namelist[i], namelist[i]->d_name, strsz);
                free(namelist[i]);
        }
        free(namelist);
        return NULL;
}
