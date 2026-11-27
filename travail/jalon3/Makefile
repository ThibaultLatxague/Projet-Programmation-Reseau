CFLAGS=-Wall -Wextra
CFLAGS+=-I. -I../include
BUILD_DIR=build
CLIENT=$(BUILD_DIR)/client
SERVER=$(BUILD_DIR)/server
HEADERS=$(wildcard *.h) ../include/protocol.h

CLIENT_OBJECTS=$(BUILD_DIR)/client.o $(BUILD_DIR)/client_commands.o $(BUILD_DIR)/file_transfer.o $(BUILD_DIR)/protocol.o
SERVER_OBJECTS=$(BUILD_DIR)/server.o $(BUILD_DIR)/commands.o $(BUILD_DIR)/client_list.o $(BUILD_DIR)/protocol.o

all: $(CLIENT) $(SERVER)

$(BUILD_DIR):
	mkdir -p $@

$(CLIENT): $(CLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERVER): $(SERVER_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/protocol.o: ../src/common/protocol.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CLIENT) $(SERVER) $(CLIENT_OBJECTS) $(SERVER_OBJECTS)
	rmdir $(BUILD_DIR) 2>/dev/null || true

.PHONY: all clean
