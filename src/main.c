#include "server.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    const char *port = "8080";

    if (argc >= 2) {
        port = argv[1];
    }

    int listen_fd = create_listening_socket(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Error: no se pudo crear el socket servidor\n");
        return EXIT_FAILURE;
    }

    printf("MiniHTTPd escuchando en el puerto %s\n", port);
    printf("Prueba con: curl -v http://localhost:%s/\n", port);

    int result = run_event_loop(listen_fd);

    close(listen_fd);

    if (result < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
