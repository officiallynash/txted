CC = clang
CFLAGS = -Wall -Wextra -Iinclude -O2
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -ltree-sitter -ltree-sitter-c -ltree-sitter-go

SRC_DIR = src
INC_DIR = include
OBJ_DIR = build
TARGET = txted

SRCS = $(shell find $(SRC_DIR) -name '*.c')
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(OBJ_DIR) $(TARGET)

run:
	@./$(TARGET)

.PHONY: all clean run
