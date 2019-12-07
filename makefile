CC = gcc
LFLAGS = -lpthread
CFLAGS = -Wall -g

BUILD_DIR = ./build

INCLUDE = 
SRC = 
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)
DEP := $(OBJ:%.o=$.d)

CFLAGS += $(foreach DIR, $(INCLUDE), -I$(DIR))

all: client server

client: client.c $(OBJ)
	$(CC) $(CFLAGS) $(LFLAGS) $^ -o $@

server: server.c $(OBJ)
	$(CC) $(CFLAGS) $(LFLAGS) $^ -o $@

-include $(DEP)

$(BUILD_DIR)/%.o : %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

.PHONY: clean
clean :
	-rm -rf $(BUILD_DIR) client server
