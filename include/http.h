#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_MAX_REQUEST_SIZE 8192
#define HTTP_MAX_METHOD_SIZE 16
#define HTTP_MAX_URI_SIZE 2048
#define HTTP_MAX_VERSION_SIZE 16
#define HTTP_MAX_HEADER_VALUE_SIZE 512

typedef struct {
    char method[HTTP_MAX_METHOD_SIZE];
    char uri[HTTP_MAX_URI_SIZE];
    char version[HTTP_MAX_VERSION_SIZE];

    char host[HTTP_MAX_HEADER_VALUE_SIZE];
    char connection[HTTP_MAX_HEADER_VALUE_SIZE];
    char user_agent[HTTP_MAX_HEADER_VALUE_SIZE];

    int keep_alive;
} http_request_t;

int http_parse_request(const char *raw_request, size_t raw_length, http_request_t *request);

const char *http_status_reason(int status_code);

int http_build_headers(
    int status_code,
    const char *content_type,
    size_t content_length,
    int keep_alive,
    char *out_buffer,
    size_t out_size
);

int http_build_text_response(
    int status_code,
    const char *body,
    int keep_alive,
    char *out_buffer,
    size_t out_size
);

#endif
