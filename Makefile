CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -O2
LDFLAGS = -pthread
PKGS = libwebsockets libcjson
LIBS = $(shell pkg-config --libs $(PKGS))
PKG_CFLAGS = $(shell pkg-config --cflags $(PKGS))

SRC_DIR = src
OBJ_DIR = obj
BIN = rtes_monitor

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/circular_buffer.c \
          $(SRC_DIR)/producer.c \
          $(SRC_DIR)/consumer.c \
          $(SRC_DIR)/monitor.c \
          $(SRC_DIR)/health_monitor.c
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)
