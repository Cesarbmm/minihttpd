#ifndef FILES_H
#define FILES_H

#include <stddef.h>
#include <limits.h>

typedef struct {
    unsigned char *data;
    size_t size;
    char resolved_path[PATH_MAX];
} file_content_t;

int files_load_static(const char *document_root, const char *uri, file_content_t *out_file);
void files_free_content(file_content_t *file);

#endif
