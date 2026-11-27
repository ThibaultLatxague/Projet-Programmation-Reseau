CFLAGS=-Wall
CFLAGS+=-I. -I../include
#LDFLAGS=-lpthread
BUILD_DIR=build
CLIENT=$(BUILD_DIR)/client
SERVER=$(BUILD_DIR)/server
OBJECTS=$(BUILD_DIR)/client.o $(BUILD_DIR)/server.o $(BUILD_DIR)/commands.o $(BUILD_DIR)/protocol.o

all: $(CLIENT) $(SERVER)

$(BUILD_DIR):
	mkdir -p $@

$(CLIENT): $(BUILD_DIR)/client.o $(BUILD_DIR)/protocol.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SERVER): $(BUILD_DIR)/server.o $(BUILD_DIR)/commands.o $(BUILD_DIR)/protocol.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/client.o: client.c client.h common.h msg_struct.h ../include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/server.o: server.c server.h common.h commands.h msg_struct.h ../include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/commands.o: commands.c common.h commands.h msg_struct.h ../include/protocol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/protocol.o: ../src/common/protocol.c ../include/protocol.h common.h msg_struct.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CLIENT) $(SERVER) $(OBJECTS)
	rmdir $(BUILD_DIR) 2>/dev/null || true
