.RECIPEPREFIX := >

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -D_GNU_SOURCE -Iinclude
TARGET = minihttpd

SRC = src/main.c src/server.c src/http.c src/files.c src/mime.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
>$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

%.o: %.c
>$(CC) $(CFLAGS) -c $< -o $@

clean:
>rm -f $(OBJ) $(TARGET)

run:
>./$(TARGET) 8080

test:
>./tests/run_tests.sh
