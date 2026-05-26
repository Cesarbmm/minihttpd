#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int safe_copy(char *dest, size_t dest_size, const char *src)
{
    size_t src_len = strlen(src);

    if (src_len >= dest_size) {
        return -1;
    }

    memcpy(dest, src, src_len + 1);
    return 0;
}

static char *trim_whitespace(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    char *end = text + strlen(text) - 1;

    while (end > text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return text;
}

const char *http_status_reason(int status_code)
{
    switch (status_code) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 500:
        return "Internal Server Error";
    default:
        return "Internal Server Error";
    }
}

static int parse_request_line(char *line, http_request_t *request)
{
    char *saveptr = NULL;

    char *method = strtok_r(line, " ", &saveptr);
    char *uri = strtok_r(NULL, " ", &saveptr);
    char *version = strtok_r(NULL, " ", &saveptr);
    char *extra = strtok_r(NULL, " ", &saveptr);

    if (method == NULL || uri == NULL || version == NULL || extra != NULL) {
        return 400;
    }

    if (safe_copy(request->method, sizeof(request->method), method) < 0) {
        return 400;
    }

    if (safe_copy(request->uri, sizeof(request->uri), uri) < 0) {
        return 400;
    }

    if (safe_copy(request->version, sizeof(request->version), version) < 0) {
        return 400;
    }

    if (strcasecmp(request->method, "GET") != 0) {
        return 405;
    }

    if (strcmp(request->version, "HTTP/1.1") != 0 &&
        strcmp(request->version, "HTTP/1.0") != 0) {
        return 400;
    }

    if (request->uri[0] != '/') {
        return 400;
    }

    return 200;
}

static int parse_header_line(char *line, http_request_t *request)
{
    char *colon = strchr(line, ':');

    if (colon == NULL) {
        return 400;
    }

    *colon = '\0';

    char *name = trim_whitespace(line);
    char *value = trim_whitespace(colon + 1);

    if (strcasecmp(name, "Host") == 0) {
        if (safe_copy(request->host, sizeof(request->host), value) < 0) {
            return 400;
        }
    } else if (strcasecmp(name, "Connection") == 0) {
        if (safe_copy(request->connection, sizeof(request->connection), value) < 0) {
            return 400;
        }
    } else if (strcasecmp(name, "User-Agent") == 0) {
        if (safe_copy(request->user_agent, sizeof(request->user_agent), value) < 0) {
            return 400;
        }
    }

    return 200;
}

int http_parse_request(const char *raw_request, size_t raw_length, http_request_t *request)
{
    if (raw_request == NULL || request == NULL) {
        return 500;
    }

    if (raw_length == 0 || raw_length >= HTTP_MAX_REQUEST_SIZE) {
        return 400;
    }

    memset(request, 0, sizeof(*request));

    char buffer[HTTP_MAX_REQUEST_SIZE + 1];

    memcpy(buffer, raw_request, raw_length);
    buffer[raw_length] = '\0';

    if (strstr(buffer, "\r\n\r\n") == NULL) {
        return 400;
    }

    char *request_line_end = strstr(buffer, "\r\n");

    if (request_line_end == NULL) {
        return 400;
    }

    *request_line_end = '\0';

    int status = parse_request_line(buffer, request);

    if (status != 200) {
        return status;
    }

    char *current_line = request_line_end + 2;

    while (1) {
        char *line_end = strstr(current_line, "\r\n");

        if (line_end == NULL) {
            return 400;
        }

        if (line_end == current_line) {
            break;
        }

        *line_end = '\0';

        status = parse_header_line(current_line, request);

        if (status != 200) {
            return status;
        }

        current_line = line_end + 2;
    }

    if (strcmp(request->version, "HTTP/1.1") == 0) {
        request->keep_alive = 1;
    } else {
        request->keep_alive = 0;
    }

    if (request->connection[0] != '\0') {
        if (strcasecmp(request->connection, "close") == 0) {
            request->keep_alive = 0;
        } else if (strcasecmp(request->connection, "keep-alive") == 0) {
            request->keep_alive = 1;
        }
    }

    return 200;
}

int http_build_headers(
    int status_code,
    const char *content_type,
    size_t content_length,
    int keep_alive,
    char *out_buffer,
    size_t out_size
)
{
    if (content_type == NULL || out_buffer == NULL || out_size == 0) {
        return -1;
    }

    const char *reason = http_status_reason(status_code);
    const char *connection_value = keep_alive ? "keep-alive" : "close";

    int written = snprintf(
        out_buffer,
        out_size,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status_code,
        reason,
        content_type,
        content_length,
        connection_value
    );

    if (written < 0 || (size_t)written >= out_size) {
        return -1;
    }

    return written;
}

int http_build_text_response(
    int status_code,
    const char *body,
    int keep_alive,
    char *out_buffer,
    size_t out_size
)
{
    if (body == NULL || out_buffer == NULL || out_size == 0) {
        return -1;
    }

    size_t body_length = strlen(body);

    int header_length = http_build_headers(
        status_code,
        "text/plain",
        body_length,
        keep_alive,
        out_buffer,
        out_size
    );

    if (header_length < 0) {
        return -1;
    }

    size_t used = (size_t)header_length;

    if (used + body_length >= out_size) {
        return -1;
    }

    memcpy(out_buffer + used, body, body_length);

    return (int)(used + body_length);
}
