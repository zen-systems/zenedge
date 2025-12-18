/* tools/bridge/inject.c
 *
 * Test utility to inject IPC packets into shared memory.
 * Simulates ZENEDGE kernel for testing the bridge daemon.
 *
 * Usage: ./inject <cmd> [payload]
 *   ./inject ping
 *   ./inject print 12345
 *   ./inject model 42
 *   ./inject status
 *   ./inject reset
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>

#include "ipc_proto.h"

#define IPC_SHARED_MEM_SIZE (1024 * 1024)
#define DEFAULT_SHM_PATH "/tmp/zenedge_ipc"

static void *shm_base = NULL;
static volatile ipc_ring_t *cmd_ring = NULL;
static volatile ipc_rsp_ring_t *rsp_ring = NULL;
static volatile doorbell_ctl_t *doorbell = NULL;

/* Get current time in microseconds (simulating ZENEDGE time) */
static uint64_t time_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int init_rings(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("Failed to open shared memory file");
        return -1;
    }

    if (ftruncate(fd, IPC_SHARED_MEM_SIZE) < 0) {
        perror("Failed to resize file");
        close(fd);
        return -1;
    }

    shm_base = mmap(NULL, IPC_SHARED_MEM_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
    close(fd);

    if (shm_base == MAP_FAILED) {
        perror("Failed to mmap");
        return -1;
    }

    cmd_ring = (volatile ipc_ring_t *)((char *)shm_base + IPC_CMD_RING_OFFSET);
    rsp_ring = (volatile ipc_rsp_ring_t *)((char *)shm_base + IPC_RSP_RING_OFFSET);
    doorbell = (volatile doorbell_ctl_t *)((char *)shm_base + IPC_DOORBELL_OFFSET);

    /* Initialize rings if needed */
    if (cmd_ring->magic != IPC_MAGIC) {
        printf("[inject] Initializing command ring...\n");
        ((ipc_ring_t *)cmd_ring)->magic = IPC_MAGIC;
        ((ipc_ring_t *)cmd_ring)->head = 0;
        ((ipc_ring_t *)cmd_ring)->tail = 0;
        ((ipc_ring_t *)cmd_ring)->size = IPC_RING_SIZE;
    }

    if (rsp_ring->magic != IPC_RSP_MAGIC) {
        printf("[inject] Initializing response ring...\n");
        ((ipc_rsp_ring_t *)rsp_ring)->magic = IPC_RSP_MAGIC;
        ((ipc_rsp_ring_t *)rsp_ring)->head = 0;
        ((ipc_rsp_ring_t *)rsp_ring)->tail = 0;
        ((ipc_rsp_ring_t *)rsp_ring)->size = IPC_RING_SIZE;
    }

    if (doorbell->magic != IPC_DOORBELL_MAGIC) {
        printf("[inject] Initializing doorbell...\n");
        ((doorbell_ctl_t *)doorbell)->magic = IPC_DOORBELL_MAGIC;
        ((doorbell_ctl_t *)doorbell)->version = 1;
        ((doorbell_ctl_t *)doorbell)->cmd_doorbell = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_flags = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_irq_count = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_doorbell = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_flags = DOORBELL_FLAG_IRQ_ENABLED;
        ((doorbell_ctl_t *)doorbell)->rsp_irq_count = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_writes = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_writes = 0;
    }

    return 0;
}

static int send_packet(uint16_t cmd, uint32_t payload) {
    uint32_t head = cmd_ring->head;
    uint32_t next_head = (head + 1) % cmd_ring->size;

    if (next_head == cmd_ring->tail) {
        fprintf(stderr, "[inject] Command ring full!\n");
        return -1;
    }

    volatile ipc_packet_t *pkt = &cmd_ring->data[head];
    pkt->cmd = cmd;
    pkt->flags = 0;
    pkt->payload_id = payload;
    pkt->timestamp = time_usec();

    /* Memory barrier */
    __sync_synchronize();

    ((ipc_ring_t *)cmd_ring)->head = next_head;

    /* Ring doorbell */
    if (doorbell && doorbell->magic == IPC_DOORBELL_MAGIC) {
        doorbell->cmd_doorbell = next_head;
        ((doorbell_ctl_t *)doorbell)->cmd_writes++;
        if (doorbell->cmd_flags & DOORBELL_FLAG_IRQ_ENABLED) {
            ((doorbell_ctl_t *)doorbell)->cmd_flags |= DOORBELL_FLAG_PENDING;
            ((doorbell_ctl_t *)doorbell)->cmd_irq_count++;
        }
    }

    printf("[inject] Sent: cmd=%s(0x%04X) payload=0x%08X ts=%llu (doorbell rang)\n",
           cmd_name(cmd), cmd, payload, (unsigned long long)pkt->timestamp);

    return 0;
}

static void poll_response(void) {
    if (rsp_ring->head == rsp_ring->tail) {
        printf("[inject] No responses pending.\n");
        return;
    }

    uint32_t tail = rsp_ring->tail;
    volatile ipc_response_t *rsp = &rsp_ring->data[tail];

    printf("[inject] Response received:\n");
    printf("  Status: %s (0x%04X)\n", rsp_name(rsp->status), rsp->status);
    printf("  Orig Cmd: %s (0x%04X)\n", cmd_name(rsp->orig_cmd), rsp->orig_cmd);
    printf("  Result: 0x%08X\n", rsp->result);
    printf("  Timestamp: %llu\n", (unsigned long long)rsp->timestamp);

    /* Consume response */
    __sync_synchronize();
    ((ipc_rsp_ring_t *)rsp_ring)->tail = (tail + 1) % rsp_ring->size;
}

static void print_status(void) {
    printf("[inject] === Ring Status ===\n");

    printf("[inject] CMD Ring:\n");
    printf("  Magic: 0x%08X %s\n", cmd_ring->magic,
           cmd_ring->magic == IPC_MAGIC ? "(valid)" : "(INVALID)");
    printf("  Head:  %u\n", cmd_ring->head);
    printf("  Tail:  %u\n", cmd_ring->tail);
    printf("  Size:  %u\n", cmd_ring->size);
    uint32_t cmd_pending = (cmd_ring->head >= cmd_ring->tail)
                           ? (cmd_ring->head - cmd_ring->tail)
                           : (cmd_ring->size - cmd_ring->tail + cmd_ring->head);
    printf("  Pending: %u commands\n", cmd_pending);

    printf("[inject] RSP Ring:\n");
    printf("  Magic: 0x%08X %s\n", rsp_ring->magic,
           rsp_ring->magic == IPC_RSP_MAGIC ? "(valid)" : "(INVALID)");
    printf("  Head:  %u\n", rsp_ring->head);
    printf("  Tail:  %u\n", rsp_ring->tail);
    printf("  Size:  %u\n", rsp_ring->size);
    uint32_t rsp_pending = (rsp_ring->head >= rsp_ring->tail)
                           ? (rsp_ring->head - rsp_ring->tail)
                           : (rsp_ring->size - rsp_ring->tail + rsp_ring->head);
    printf("  Pending: %u responses\n", rsp_pending);

    printf("[inject] Doorbell:\n");
    printf("  Magic: 0x%08X %s\n", doorbell->magic,
           doorbell->magic == IPC_DOORBELL_MAGIC ? "(valid)" : "(INVALID)");
    printf("  CMD doorbell: %u (writes: %u, irqs: %u)\n",
           doorbell->cmd_doorbell, doorbell->cmd_writes, doorbell->cmd_irq_count);
    printf("  RSP doorbell: %u (writes: %u, irqs: %u)\n",
           doorbell->rsp_doorbell, doorbell->rsp_writes, doorbell->rsp_irq_count);
    printf("  RSP IRQ enabled: %s\n",
           (doorbell->rsp_flags & DOORBELL_FLAG_IRQ_ENABLED) ? "yes" : "no");
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <command> [payload]\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  ping           Send PING command\n");
    fprintf(stderr, "  print <id>     Send PRINT command with payload\n");
    fprintf(stderr, "  model <id>     Send RUN_MODEL command with model ID\n");
    fprintf(stderr, "  status         Show ring buffer status\n");
    fprintf(stderr, "  reset          Reset ring buffers\n");
    fprintf(stderr, "  poll           Poll for and consume one response\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (init_rings(DEFAULT_SHM_PATH) < 0) {
        return 1;
    }

    const char *cmd = argv[1];
    uint32_t payload = (argc > 2) ? (uint32_t)strtoul(argv[2], NULL, 0) : 0;

    if (strcmp(cmd, "ping") == 0) {
        send_packet(CMD_PING, payload);
    } else if (strcmp(cmd, "print") == 0) {
        send_packet(CMD_PRINT, payload);
    } else if (strcmp(cmd, "model") == 0) {
        send_packet(CMD_RUN_MODEL, payload);
    } else if (strcmp(cmd, "status") == 0) {
        print_status();
    } else if (strcmp(cmd, "poll") == 0) {
        poll_response();
    } else if (strcmp(cmd, "reset") == 0) {
        printf("[inject] Resetting ring buffers and doorbell...\n");
        ((ipc_ring_t *)cmd_ring)->magic = IPC_MAGIC;
        ((ipc_ring_t *)cmd_ring)->head = 0;
        ((ipc_ring_t *)cmd_ring)->tail = 0;
        ((ipc_ring_t *)cmd_ring)->size = IPC_RING_SIZE;
        ((ipc_rsp_ring_t *)rsp_ring)->magic = IPC_RSP_MAGIC;
        ((ipc_rsp_ring_t *)rsp_ring)->head = 0;
        ((ipc_rsp_ring_t *)rsp_ring)->tail = 0;
        ((ipc_rsp_ring_t *)rsp_ring)->size = IPC_RING_SIZE;
        ((doorbell_ctl_t *)doorbell)->magic = IPC_DOORBELL_MAGIC;
        ((doorbell_ctl_t *)doorbell)->version = 1;
        ((doorbell_ctl_t *)doorbell)->cmd_doorbell = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_flags = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_irq_count = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_doorbell = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_flags = DOORBELL_FLAG_IRQ_ENABLED;
        ((doorbell_ctl_t *)doorbell)->rsp_irq_count = 0;
        ((doorbell_ctl_t *)doorbell)->cmd_writes = 0;
        ((doorbell_ctl_t *)doorbell)->rsp_writes = 0;
        printf("[inject] Done.\n");
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        munmap(shm_base, IPC_SHARED_MEM_SIZE);
        return 1;
    }

    munmap(shm_base, IPC_SHARED_MEM_SIZE);
    return 0;
}
