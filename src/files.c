#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int path_starts_with_root(const char *resolved_path, const char *resolved_root)
{
    size_t root_length = strlen(resolved_root);

    if (strncmp(resolved_path, resolved_root, root_length) != 0) {
        return 0;
    }

    if (resolved_path[root_length] == '\0' || resolved_path[root_length] == '/') {
        return 1;
    }

    return 0;
}

static int uri_contains_parent_segment(const char *uri)
{
    const char *p = uri;

    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }

        const char *segment_start = p;

        while (*p != '\0' && *p != '/' && *p != '?') {
            p++;
        }

        size_t segment_length = (size_t)(p - segment_start);

        if (segment_length == 2 && strncmp(segment_start, "..", 2) == 0) {
            return 1;
        }

        if (*p == '?') {
            break;
        }
    }

    return 0;
}

static int build_request_path(const char *uri, char *out_path, size_t out_size)
{
    if (uri == NULL || out_path == NULL || out_size == 0) {
        return -1;
    }

    if (uri[0] != '/') {
        return -1;
    }

    if (uri_contains_parent_segment(uri)) {
        return -2;
    }

    const char *query = strchr(uri, '?');
    size_t path_length;

    if (query != NULL) {
        path_length = (size_t)(query - uri);
    } else {
        path_length = strlen(uri);
    }

    if (path_length == 0 || path_length >= out_size) {
        return -1;
    }

    if (path_length == 1 && uri[0] == '/') {
        const char index_path[] = "/index.html";

        if (sizeof(index_path) > out_size) {
            return -1;
        }

        memcpy(out_path, index_path, sizeof(index_path));
        return 0;
    }

    memcpy(out_path, uri, path_length);
    out_path[path_length] = '\0';

    return 0;
}

int files_load_static(const char *document_root, const char *uri, file_content_t *out_file)
{
    if (document_root == NULL || uri == NULL || out_file == NULL) {
        return 500;
    }

    memset(out_file, 0, sizeof(*out_file));

    char resolved_root[PATH_MAX];

    if (realpath(document_root, resolved_root) == NULL) {
        perror("realpath document_root");
        return 500;
    }

    char request_path[PATH_MAX];

    int path_status = build_request_path(uri, request_path, sizeof(request_path));

    if (path_status == -2) {
        return 403;
    }

    if (path_status < 0) {
        return 400;
    }

    char candidate_path[PATH_MAX];

    int written = snprintf(
        candidate_path,
        sizeof(candidate_path),
        "%s%s",
        resolved_root,
        request_path
    );

    if (written < 0 || (size_t)written >= sizeof(candidate_path)) {
        return 400;
    }

    char resolved_file[PATH_MAX];

    if (realpath(candidate_path, resolved_file) == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return 404;
        }

        if (errno == EACCES) {
            return 403;
        }

        perror("realpath file");
        return 500;
    }

    if (!path_starts_with_root(resolved_file, resolved_root)) {
        return 403;
    }

    struct stat st;

    if (stat(resolved_file, &st) < 0) {
        if (errno == EACCES) {
            return 403;
        }

        if (errno == ENOENT || errno == ENOTDIR) {
            return 404;
        }

        perror("stat");
        return 500;
    }

    if (!S_ISREG(st.st_mode)) {
        return 403;
    }

    FILE *fp = fopen(resolved_file, "rb");

    if (fp == NULL) {
        if (errno == EACCES) {
            return 403;
        }

        perror("fopen");
        return 500;
    }

    size_t file_size = (size_t)st.st_size;
    unsigned char *data = malloc(file_size == 0 ? 1 : file_size);

    if (data == NULL) {
        fclose(fp);
        return 500;
    }

    size_t bytes_read = fread(data, 1, file_size, fp);

    if (bytes_read != file_size) {
        free(data);
        fclose(fp);
        return 500;
    }

    fclose(fp);

    out_file->data = data;
    out_file->size = file_size;

    if (snprintf(out_file->resolved_path, sizeof(out_file->resolved_path), "%s", resolved_file) < 0) {
        files_free_content(out_file);
        return 500;
    }

    return 200;
}

void files_free_content(file_content_t *file)
{
    if (file == NULL) {
        return;
    }

    free(file->data);
    file->data = NULL;
    file->size = 0;
    file->resolved_path[0] = '\0';
}
