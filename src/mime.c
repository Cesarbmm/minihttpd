#include "mime.h"

#include <stddef.h>
#include <string.h>
#include <strings.h>

typedef struct {
    const char *extension;
    const char *mime_type;
} mime_entry_t;

static const mime_entry_t MIME_TABLE[] = {
    {".html", "text/html"},
    {".htm",  "text/html"},
    {".css",  "text/css"},
    {".js",   "application/javascript"},
    {".png",  "image/png"},
    {".jpg",  "image/jpeg"},
    {".jpeg", "image/jpeg"}
};

const char *mime_get_type(const char *path)
{
    if (path == NULL) {
        return "application/octet-stream";
    }

    const char *dot = strrchr(path, '.');

    if (dot == NULL) {
        return "application/octet-stream";
    }

    size_t table_size = sizeof(MIME_TABLE) / sizeof(MIME_TABLE[0]);

    for (size_t i = 0; i < table_size; i++) {
        if (strcasecmp(dot, MIME_TABLE[i].extension) == 0) {
            return MIME_TABLE[i].mime_type;
        }
    }

    return "application/octet-stream";
}
