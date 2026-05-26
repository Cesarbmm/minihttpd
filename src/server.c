#include "server.h"
#include "http.h"
#include "files.h"
#include "mime.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define DOCUMENT_ROOT "www"

typedef struct client client_t;

typedef enum {
    CONTEXT_LISTENER = 1,
    CONTEXT_CLIENT = 2
} context_kind_t;

typedef struct {
    context_kind_t kind;
    int fd;
    client_t *client;
} epoll_context_t;

struct client {
    int fd;

    char read_buffer[HTTP_MAX_REQUEST_SIZE + 1];
    size_t read_length;

    unsigned char *write_buffer;
    size_t write_length;
    size_t write_sent;

    int close_after_write;

    epoll_context_t context;
};

static int update_client_events(int epoll_fd, client_t *client, uint32_t events)
{
    struct epoll_event event;

    memset(&event, 0, sizeof(event));
    event.events = events;
    event.data.ptr = &client->context;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client->fd, &event) < 0) {
        perror("epoll_ctl MOD client");
        return -1;
    }

    return 0;
}

static void close_client(int epoll_fd, client_t *client)
{
    if (client == NULL) {
        return;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);

    close(client->fd);

    free(client->write_buffer);
    client->write_buffer = NULL;

    printf("Cliente cerrado: fd=%d\n", client->fd);

    free(client);
}

static int request_headers_complete(const client_t *client)
{
    if (client == NULL) {
        return 0;
    }

    return strstr(client->read_buffer, "\r\n\r\n") != NULL;
}

static int set_client_write_buffer(
    client_t *client,
    const void *data,
    size_t length,
    int close_after_write
)
{
    if (client == NULL || data == NULL) {
        return -1;
    }

    unsigned char *buffer = malloc(length == 0 ? 1 : length);

    if (buffer == NULL) {
        return -1;
    }

    if (length > 0) {
        memcpy(buffer, data, length);
    }

    free(client->write_buffer);

    client->write_buffer = buffer;
    client->write_length = length;
    client->write_sent = 0;
    client->close_after_write = close_after_write;

    return 0;
}

static int queue_error_response(client_t *client, int status_code, int close_after_write)
{
    char body[256];

    snprintf(
        body,
        sizeof(body),
        "%d %s\n",
        status_code,
        http_status_reason(status_code)
    );

    char response[1024];

    int response_length = http_build_text_response(
        status_code,
        body,
        close_after_write ? 0 : 1,
        response,
        sizeof(response)
    );

    if (response_length < 0) {
        return -1;
    }

    return set_client_write_buffer(
        client,
        response,
        (size_t)response_length,
        close_after_write
    );
}

static int queue_file_response(client_t *client, const file_content_t *file, int close_after_write)
{
    char header[1024];

    const char *content_type = mime_get_type(file->resolved_path);

    int header_length = http_build_headers(
        200,
        content_type,
        file->size,
        close_after_write ? 0 : 1,
        header,
        sizeof(header)
    );

    if (header_length < 0) {
        return queue_error_response(client, 500, 1);
    }

    size_t total_length = (size_t)header_length + file->size;

    unsigned char *response = malloc(total_length == 0 ? 1 : total_length);

    if (response == NULL) {
        return queue_error_response(client, 500, 1);
    }

    memcpy(response, header, (size_t)header_length);

    if (file->size > 0) {
        memcpy(response + header_length, file->data, file->size);
    }

    int result = set_client_write_buffer(
        client,
        response,
        total_length,
        close_after_write
    );

    free(response);

    return result;
}

static int process_client_request(int epoll_fd, client_t *client)
{
    http_request_t request;

    int status_code = http_parse_request(
        client->read_buffer,
        client->read_length,
        &request
    );

    if (status_code != 200) {
        printf("Solicitud rechazada: %d %s\n",
               status_code,
               http_status_reason(status_code));

        if (queue_error_response(client, status_code, 1) < 0) {
            close_client(epoll_fd, client);
            return -1;
        }

        if (update_client_events(epoll_fd, client, EPOLLOUT | EPOLLRDHUP) < 0) {
            close_client(epoll_fd, client);
            return -1;
        }

        return 0;
    }

    printf("Solicitud valida: metodo=%s uri=%s version=%s keep_alive=%d\n",
           request.method,
           request.uri,
           request.version,
           request.keep_alive);

    file_content_t file;

    status_code = files_load_static(DOCUMENT_ROOT, request.uri, &file);

    int close_after_write = request.keep_alive ? 0 : 1;

    if (status_code != 200) {
        printf("Archivo no servido: uri=%s status=%d %s\n",
               request.uri,
               status_code,
               http_status_reason(status_code));

        if (queue_error_response(client, status_code, close_after_write) < 0) {
            close_client(epoll_fd, client);
            return -1;
        }

        if (update_client_events(epoll_fd, client, EPOLLOUT | EPOLLRDHUP) < 0) {
            close_client(epoll_fd, client);
            return -1;
        }

        return 0;
    }

    printf("Archivo servido: %s (%zu bytes, %s)\n",
           file.resolved_path,
           file.size,
           mime_get_type(file.resolved_path));

    if (queue_file_response(client, &file, close_after_write) < 0) {
        files_free_content(&file);
        close_client(epoll_fd, client);
        return -1;
    }

    files_free_content(&file);

    if (update_client_events(epoll_fd, client, EPOLLOUT | EPOLLRDHUP) < 0) {
        close_client(epoll_fd, client);
        return -1;
    }

    return 0;
}

static void handle_client_read(int epoll_fd, client_t *client)
{
    while (1) {
        if (client->read_length >= HTTP_MAX_REQUEST_SIZE) {
            if (queue_error_response(client, 400, 1) < 0) {
                close_client(epoll_fd, client);
                return;
            }

            if (update_client_events(epoll_fd, client, EPOLLOUT | EPOLLRDHUP) < 0) {
                close_client(epoll_fd, client);
            }

            return;
        }

        size_t remaining = HTTP_MAX_REQUEST_SIZE - client->read_length;

        ssize_t received = recv(
            client->fd,
            client->read_buffer + client->read_length,
            remaining,
            0
        );

        if (received == 0) {
            close_client(epoll_fd, client);
            return;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            perror("recv");
            close_client(epoll_fd, client);
            return;
        }

        client->read_length += (size_t)received;
        client->read_buffer[client->read_length] = '\0';

        if (request_headers_complete(client)) {
            printf("\n----- Solicitud recibida desde fd=%d -----\n", client->fd);
            printf("%s\n", client->read_buffer);
            printf("----- Fin de solicitud -----\n\n");

            process_client_request(epoll_fd, client);
            return;
        }
    }
}

static void handle_client_write(int epoll_fd, client_t *client)
{
    while (client->write_sent < client->write_length) {
        ssize_t sent = send(
            client->fd,
            client->write_buffer + client->write_sent,
            client->write_length - client->write_sent,
            0
        );

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            perror("send");
            close_client(epoll_fd, client);
            return;
        }

        if (sent == 0) {
            close_client(epoll_fd, client);
            return;
        }

        client->write_sent += (size_t)sent;
    }

    free(client->write_buffer);
    client->write_buffer = NULL;
    client->write_length = 0;
    client->write_sent = 0;

    if (client->close_after_write) {
        close_client(epoll_fd, client);
        return;
    }

    client->read_length = 0;
    client->read_buffer[0] = '\0';

    printf("Cliente fd=%d queda en keep-alive\n", client->fd);

    if (update_client_events(epoll_fd, client, EPOLLIN | EPOLLRDHUP) < 0) {
        close_client(epoll_fd, client);
    }
}

int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        perror("fcntl F_GETFL");
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}

int create_listening_socket(const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp = NULL;
    int listen_fd = -1;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai_status = getaddrinfo(NULL, port, &hints, &result);

    if (gai_status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_status));
        return -1;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (listen_fd < 0) {
            continue;
        }

        int opt = 1;

        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("setsockopt SO_REUSEADDR");
            close(listen_fd);
            listen_fd = -1;
            continue;
        }

        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        perror("bind");
        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(result);

    if (listen_fd < 0) {
        fprintf(stderr, "Error: no se pudo hacer bind en el puerto %s\n", port);
        return -1;
    }

    if (set_nonblocking(listen_fd) < 0) {
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, SERVER_BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

static int accept_new_clients(int epoll_fd, int listen_fd)
{
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            perror("accept");
            return -1;
        }

        if (set_nonblocking(client_fd) < 0) {
            close(client_fd);
            continue;
        }

        client_t *client = calloc(1, sizeof(*client));

        if (client == NULL) {
            close(client_fd);
            continue;
        }

        client->fd = client_fd;
        client->context.kind = CONTEXT_CLIENT;
        client->context.fd = client_fd;
        client->context.client = client;

        struct epoll_event event;

        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.ptr = &client->context;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
            perror("epoll_ctl ADD client");
            close(client_fd);
            free(client);
            continue;
        }

        printf("Cliente aceptado: fd=%d\n", client_fd);
    }

    return 0;
}

int run_event_loop(int listen_fd)
{
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd < 0) {
        perror("epoll_create1");
        return -1;
    }

    epoll_context_t listener_context;

    listener_context.kind = CONTEXT_LISTENER;
    listener_context.fd = listen_fd;
    listener_context.client = NULL;

    struct epoll_event listen_event;

    memset(&listen_event, 0, sizeof(listen_event));
    listen_event.events = EPOLLIN;
    listen_event.data.ptr = &listener_context;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &listen_event) < 0) {
        perror("epoll_ctl ADD listen_fd");
        close(epoll_fd);
        return -1;
    }

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            close(epoll_fd);
            return -1;
        }

        for (int i = 0; i < n; i++) {
            epoll_context_t *context = (epoll_context_t *)events[i].data.ptr;

            if (context == NULL) {
                continue;
            }

            if (context->kind == CONTEXT_LISTENER) {
                accept_new_clients(epoll_fd, listen_fd);
                continue;
            }

            client_t *client = context->client;

            if (client == NULL) {
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                close_client(epoll_fd, client);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                handle_client_read(epoll_fd, client);
                continue;
            }

            if (events[i].events & EPOLLOUT) {
                handle_client_write(epoll_fd, client);
                continue;
            }
        }
    }

    close(epoll_fd);
    return 0;
}
