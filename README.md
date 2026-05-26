# MiniHTTPd

MiniHTTPd es un servidor HTTP/1.1 basico desarrollado en C para Linux. El servidor implementa manualmente sockets TCP, parsing de solicitudes HTTP, servicio de archivos estaticos, tipos MIME, codigos de estado, protecciones basicas de seguridad y un modelo de concurrencia basado en epoll.

## Caracteristicas implementadas

- Servidor TCP sobre IPv4.
- Socket servidor en modo no bloqueante.
- Bucle de eventos con epoll.
- Atencion de multiples clientes.
- Conexiones persistentes basicas con Connection: keep-alive.
- Parsing del metodo GET.
- Analisis de encabezados basicos:
  - Host
  - Connection
  - User-Agent
- Servicio de archivos estaticos desde la carpeta www/.
- Tipos MIME:
  - .html -> text/html
  - .css -> text/css
  - .js -> application/javascript
  - .png -> image/png
  - .jpg -> image/jpeg
  - .jpeg -> image/jpeg
- Codigos de estado:
  - 200 OK
  - 400 Bad Request
  - 403 Forbidden
  - 404 Not Found
  - 405 Method Not Allowed
  - 500 Internal Server Error
- Proteccion contra Directory Traversal usando realpath().
- Control de tamano maximo de solicitud.
- Evita funciones inseguras como strcpy y sprintf.
- Ignora SIGPIPE para evitar que un cliente desconectado termine el servidor.
- Script de pruebas automatizadas.

## Estructura del proyecto

minihttpd/
|-- Makefile
|-- README.md
|-- include/
|   |-- files.h
|   |-- http.h
|   |-- mime.h
|   |-- server.h
|-- src/
|   |-- files.c
|   |-- http.c
|   |-- main.c
|   |-- mime.c
|   |-- server.c
|-- tests/
|   |-- run_tests.sh
|-- www/
    |-- image.png
    |-- index.html
    |-- style.css

## Descripcion de los modulos

main.c:
Inicializa el servidor, ignora la senal SIGPIPE y ejecuta el bucle principal.

server.c:
Contiene la logica de sockets, configuracion no bloqueante, bind, listen, accept, registro en epoll, lectura, escritura y administracion de conexiones persistentes.

http.c:
Procesa solicitudes HTTP. Extrae metodo, URI, version y encabezados principales. Tambien genera encabezados y respuestas HTTP.

files.c:
Accede al sistema de archivos. Convierte una URI en una ruta dentro de www/, valida la ruta con realpath() y evita accesos fuera del directorio publico.

mime.c:
Determina el tipo MIME de un archivo segun su extension.

## Compilacion

Ejecutar:

make clean
make

## Ejecucion

Ejecutar:

./minihttpd 8080

El servidor escuchara en el puerto 8080.

## Pruebas manuales

En otra terminal:

curl -i http://localhost:8080/
curl -i http://localhost:8080/style.css
curl -v http://localhost:8080/image.png -o /tmp/minihttpd_image.png
curl -i http://localhost:8080/noexiste.html
curl -i -X POST http://localhost:8080/
curl -i --path-as-is http://127.0.0.1:8080/../../etc/passwd

## Prueba de conexion persistente

Ejecutar:

{
printf 'GET / HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n'
sleep 1
printf 'GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n'
} | nc 127.0.0.1 8080

La respuesta debe contener dos respuestas HTTP/1.1 200 OK en la misma conexion TCP.

## Pruebas automatizadas

Ejecutar:

make test

Tambien se puede ejecutar directamente:

./tests/run_tests.sh

El script valida:

- 200 OK para /
- 200 OK para /style.css
- 200 OK para /image.png
- MIME text/html
- MIME text/css
- MIME image/png
- 404 Not Found
- 405 Method Not Allowed
- 403 Forbidden para Directory Traversal
- 400 Bad Request
- 400 Bad Request para solicitud demasiado grande
- Keep-alive con dos solicitudes en una misma conexion

Resultado esperado:

Aprobadas: 12
Fallidas: 0

## Limpieza

Ejecutar:

make clean

## Notas

El servidor implementa conexiones persistentes basicas de forma secuencial. No implementa HTTP pipelining, TLS, compresion, cache avanzada, proxy reverso ni balanceo de carga, porque esos elementos estan fuera del alcance del MiniHTTPd.
