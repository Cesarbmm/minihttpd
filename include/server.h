#ifndef SERVER_H
#define SERVER_H

#define SERVER_BACKLOG 128
#define MAX_EVENTS 64

int create_listening_socket(const char *port);
int set_nonblocking(int fd);
int run_event_loop(int listen_fd);

#endif
