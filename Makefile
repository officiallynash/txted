CC = clang
CFLAGS = -Wall -Wextra -Iinclude -O2 -fsanitize=leak -std=gnu23
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -ltree-sitter -lssl -lcrypto -lz

SRC_DIR = src
INC_DIR = include
OBJ_DIR = build
TARGET = txted

SRCS = $(shell find $(SRC_DIR) -name '*.c')
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
LIBGIT = ./src/git/libgit2.a

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CC) $(OBJS) $(LIBGIT) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(OBJ_DIR) $(TARGET)

run:
	@./$(TARGET)

.PHONY: all clean run
