# tools/bridge/Makefile
# Linux Bridge Daemon for ZENEDGE

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS =

# macOS doesn't have -lrt, Linux does
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -lrt
endif

TARGETS = bridge inject
BRIDGE_SRCS = bridge.c
INJECT_SRCS = inject.c

.PHONY: all clean

all: $(TARGETS)

bridge: bridge.c ipc_proto.h
	$(CC) $(CFLAGS) -o $@ bridge.c $(LDFLAGS)

inject: inject.c ipc_proto.h
	$(CC) $(CFLAGS) -o $@ inject.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o

# For testing: create a mock shared memory file with initialized ring
test-init:
	@echo "Creating test shared memory file..."
	@dd if=/dev/zero of=/tmp/zenedge_ipc bs=64K count=1 2>/dev/null
	@echo "Done. Run ./bridge to start polling."

# Run with file-backed shared memory (for development)
run: $(TARGET)
	./$(TARGET) --file /tmp/zenedge_ipc

# Run with /dev/mem (requires root, for real hardware testing)
run-devmem: $(TARGET)
	sudo ./$(TARGET) --devmem
