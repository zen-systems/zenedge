/* tools/bridge/bridge.c
 *
 * Linux Bridge Daemon for ZENEDGE Sidecar Model
 *
 * This daemon runs on Linux and:
 * - Consumes command packets from ZENEDGE via command ring
 * - Sends response packets back to ZENEDGE via response ring
 *
 * Usage:
 *   ./bridge [--file <path>]    Use file-backed shared memory (default: /tmp/zenedge_ipc)
 *   ./bridge --devmem           Use /dev/mem at 0x02000000 (requires root)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "ipc_proto.h"

/* Shared memory configuration */
#define IPC_SHARED_MEM_PHYS  0x02000000
#define IPC_SHARED_MEM_SIZE  (1024 * 1024)  /* 1MB total */

/* Default file path for file-backed shared memory */
#define DEFAULT_SHM_PATH "/tmp/zenedge_ipc"

static volatile bool running = true;
static volatile ipc_ring_t *cmd_ring = NULL;
static volatile ipc_rsp_ring_t *rsp_ring = NULL;
static volatile doorbell_ctl_t *doorbell = NULL;
static void *shm_base = NULL;

/* Statistics */
static struct {
    uint64_t packets_received;
    uint64_t packets_processed;
    uint64_t responses_sent;
    uint64_t doorbell_rings;
    uint64_t errors;
} stats = {0};

/* Signal handler for graceful shutdown */
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[bridge] Shutting down...\n");
    running = false;
}

/* Get current time in microseconds */
static uint64_t time_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* Initialize shared memory from file */
static int init_shm_file(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror("[bridge] Failed to open shared memory file");
        return -1;
    }

    /* Ensure file is large enough */
    if (ftruncate(fd, IPC_SHARED_MEM_SIZE) < 0) {
        perror("[bridge] Failed to resize shared memory file");
        close(fd);
        return -1;
    }

    shm_base = mmap(NULL, IPC_SHARED_MEM_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
    close(fd);

    if (shm_base == MAP_FAILED) {
        perror("[bridge] Failed to mmap shared memory");
        return -1;
    }

    /* Set up ring and doorbell pointers */
    cmd_ring = (volatile ipc_ring_t *)((char *)shm_base + IPC_CMD_RING_OFFSET);
    rsp_ring = (volatile ipc_rsp_ring_t *)((char *)shm_base + IPC_RSP_RING_OFFSET);
    doorbell = (volatile doorbell_ctl_t *)((char *)shm_base + IPC_DOORBELL_OFFSET);

    printf("[bridge] Mapped file-backed shared memory: %s\n", path);
    printf("[bridge] CMD ring at offset 0x%X\n", IPC_CMD_RING_OFFSET);
    printf("[bridge] RSP ring at offset 0x%X\n", IPC_RSP_RING_OFFSET);
    printf("[bridge] Doorbell at offset 0x%X\n", IPC_DOORBELL_OFFSET);
    return 0;
}

/* Initialize shared memory from /dev/mem */
static int init_shm_devmem(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("[bridge] Failed to open /dev/mem (need root?)");
        return -1;
    }

    shm_base = mmap(NULL, IPC_SHARED_MEM_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, IPC_SHARED_MEM_PHYS);
    close(fd);

    if (shm_base == MAP_FAILED) {
        perror("[bridge] Failed to mmap /dev/mem");
        return -1;
    }

    cmd_ring = (volatile ipc_ring_t *)((char *)shm_base + IPC_CMD_RING_OFFSET);
    rsp_ring = (volatile ipc_rsp_ring_t *)((char *)shm_base + IPC_RSP_RING_OFFSET);
    doorbell = (volatile doorbell_ctl_t *)((char *)shm_base + IPC_DOORBELL_OFFSET);

    printf("[bridge] Mapped /dev/mem at 0x%08X\n", IPC_SHARED_MEM_PHYS);
    return 0;
}

/* Ring the response doorbell to notify ZENEDGE */
static void ring_rsp_doorbell(uint32_t head) {
    if (!doorbell || doorbell->magic != IPC_DOORBELL_MAGIC)
        return;

    /* Memory barrier before doorbell write */
    __sync_synchronize();

    /* Write doorbell value */
    doorbell->rsp_doorbell = head;
    doorbell->rsp_writes++;
    stats.doorbell_rings++;

    /* Set pending flag if IRQ enabled on ZENEDGE side */
    if (doorbell->rsp_flags & DOORBELL_FLAG_IRQ_ENABLED) {
        doorbell->rsp_flags |= DOORBELL_FLAG_PENDING;
        doorbell->rsp_irq_count++;
    }
}

/* Send a response to ZENEDGE */
static int send_response(uint16_t status, uint16_t orig_cmd, uint32_t result) {
    if (!rsp_ring || rsp_ring->magic != IPC_RSP_MAGIC) {
        fprintf(stderr, "[bridge] Response ring not initialized\n");
        return -1;
    }

    uint32_t head = rsp_ring->head;
    uint32_t next_head = (head + 1) % rsp_ring->size;

    if (next_head == rsp_ring->tail) {
        fprintf(stderr, "[bridge] Response ring full!\n");
        return -1;
    }

    volatile ipc_response_t *rsp = &rsp_ring->data[head];
    rsp->status = status;
    rsp->orig_cmd = orig_cmd;
    rsp->result = result;
    rsp->timestamp = time_usec();

    /* Memory barrier before publishing */
    __sync_synchronize();

    rsp_ring->head = next_head;
    stats.responses_sent++;

    /* Ring doorbell to notify ZENEDGE */
    ring_rsp_doorbell(next_head);

    printf("[bridge]   <- Sent response: status=%s result=0x%08X (doorbell rang)\n",
           rsp_name(status), result);

    return 0;
}

/* Process a single packet */
static void process_packet(const ipc_packet_t *pkt) {
    printf("[bridge] Received: cmd=%s(0x%04X) payload=0x%08X ts=%llu\n",
           cmd_name(pkt->cmd), pkt->cmd, pkt->payload_id,
           (unsigned long long)pkt->timestamp);

    stats.packets_received++;

    uint16_t status = RSP_OK;
    uint32_t result = 0;

    switch (pkt->cmd) {
        case CMD_PING:
            printf("[bridge]   -> PONG\n");
            result = 0x504F4E47; /* "PONG" */
            break;

        case CMD_PRINT:
            printf("[bridge]   -> PRINT request (payload_id=%u)\n", pkt->payload_id);
            result = pkt->payload_id;
            break;

        case CMD_RUN_MODEL:
            printf("[bridge]   -> RUN_MODEL request (model_id=%u)\n", pkt->payload_id);
            /* TODO: Dispatch to CUDA/OneAPI runtime */
            printf("[bridge]   -> [SIMULATED] Model execution complete\n");
            result = 0x12345678; /* Mock inference result */
            break;

        default:
            printf("[bridge]   -> Unknown command, sending error\n");
            status = RSP_ERROR;
            result = pkt->cmd; /* Echo back unknown command */
            stats.errors++;
            break;
    }

    stats.packets_processed++;

    /* Send response */
    send_response(status, pkt->cmd, result);
}

/* Main polling loop */
static void poll_loop(void) {
    printf("[bridge] Entering poll loop (Ctrl+C to stop)...\n\n");

    uint32_t last_doorbell = 0;

    while (running) {
        /* Check command ring magic */
        if (cmd_ring->magic != IPC_MAGIC) {
            usleep(100000);  /* 100ms */
            continue;
        }

        /* Check for doorbell ring (fast path) */
        if (doorbell && doorbell->magic == IPC_DOORBELL_MAGIC) {
            uint32_t db_val = doorbell->cmd_doorbell;
            if (db_val != last_doorbell) {
                /* Doorbell was rung - clear pending flag */
                doorbell->cmd_flags &= ~DOORBELL_FLAG_PENDING;
                last_doorbell = db_val;
            }
        }

        uint32_t head = cmd_ring->head;
        uint32_t tail = cmd_ring->tail;

        if (head == tail) {
            usleep(1000);  /* 1ms */
            continue;
        }

        /* Process packet */
        volatile ipc_packet_t *pkt = &cmd_ring->data[tail];

        /* Copy to local to avoid torn reads */
        ipc_packet_t local_pkt;
        local_pkt.cmd = pkt->cmd;
        local_pkt.flags = pkt->flags;
        local_pkt.payload_id = pkt->payload_id;
        local_pkt.timestamp = pkt->timestamp;

        process_packet(&local_pkt);

        /* Memory barrier before updating tail */
        __sync_synchronize();

        /* Update tail (consumer index) */
        cmd_ring->tail = (tail + 1) % cmd_ring->size;
    }
}

/* Print final statistics */
static void print_stats(void) {
    printf("\n[bridge] === Statistics ===\n");
    printf("[bridge] Packets received:  %llu\n", (unsigned long long)stats.packets_received);
    printf("[bridge] Packets processed: %llu\n", (unsigned long long)stats.packets_processed);
    printf("[bridge] Responses sent:    %llu\n", (unsigned long long)stats.responses_sent);
    printf("[bridge] Doorbell rings:    %llu\n", (unsigned long long)stats.doorbell_rings);
    printf("[bridge] Errors:            %llu\n", (unsigned long long)stats.errors);
}

/* Print ring buffer status */
static void print_ring_status(void) {
    printf("[bridge] Ring Status:\n");

    if (cmd_ring) {
        printf("[bridge] CMD Ring:\n");
        printf("[bridge]   Magic: 0x%08X %s\n", cmd_ring->magic,
               cmd_ring->magic == IPC_MAGIC ? "(valid)" : "(INVALID)");
        printf("[bridge]   Head:  %u\n", cmd_ring->head);
        printf("[bridge]   Tail:  %u\n", cmd_ring->tail);
        printf("[bridge]   Size:  %u\n", cmd_ring->size);
        uint32_t pending = (cmd_ring->head >= cmd_ring->tail)
                           ? (cmd_ring->head - cmd_ring->tail)
                           : (cmd_ring->size - cmd_ring->tail + cmd_ring->head);
        printf("[bridge]   Pending: %u packets\n", pending);
    }

    if (rsp_ring) {
        printf("[bridge] RSP Ring:\n");
        printf("[bridge]   Magic: 0x%08X %s\n", rsp_ring->magic,
               rsp_ring->magic == IPC_RSP_MAGIC ? "(valid)" : "(INVALID)");
        printf("[bridge]   Head:  %u\n", rsp_ring->head);
        printf("[bridge]   Tail:  %u\n", rsp_ring->tail);
        printf("[bridge]   Size:  %u\n", rsp_ring->size);
        uint32_t pending = (rsp_ring->head >= rsp_ring->tail)
                           ? (rsp_ring->head - rsp_ring->tail)
                           : (rsp_ring->size - rsp_ring->tail + rsp_ring->head);
        printf("[bridge]   Pending: %u responses\n", pending);
    }

    if (doorbell) {
        printf("[bridge] Doorbell:\n");
        printf("[bridge]   Magic: 0x%08X %s\n", doorbell->magic,
               doorbell->magic == IPC_DOORBELL_MAGIC ? "(valid)" : "(INVALID)");
        printf("[bridge]   CMD doorbell: %u (writes: %u, irqs: %u)\n",
               doorbell->cmd_doorbell, doorbell->cmd_writes, doorbell->cmd_irq_count);
        printf("[bridge]   RSP doorbell: %u (writes: %u, irqs: %u)\n",
               doorbell->rsp_doorbell, doorbell->rsp_writes, doorbell->rsp_irq_count);
        printf("[bridge]   RSP IRQ enabled: %s\n",
               (doorbell->rsp_flags & DOORBELL_FLAG_IRQ_ENABLED) ? "yes" : "no");
    }

    printf("\n");
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --file <path>   Use file-backed shared memory (default: %s)\n", DEFAULT_SHM_PATH);
    fprintf(stderr, "  --devmem        Use /dev/mem at 0x%08X (requires root)\n", IPC_SHARED_MEM_PHYS);
    fprintf(stderr, "  --help          Show this help\n");
}

int main(int argc, char *argv[]) {
    bool use_devmem = false;
    const char *shm_path = DEFAULT_SHM_PATH;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--devmem") == 0) {
            use_devmem = true;
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            shm_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("===========================================\n");
    printf("  ZENEDGE Linux Bridge Daemon v0.3\n");
    printf("  (with Doorbell support)\n");
    printf("===========================================\n\n");

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize shared memory */
    int ret;
    if (use_devmem) {
        ret = init_shm_devmem();
    } else {
        ret = init_shm_file(shm_path);
    }

    if (ret < 0) {
        return 1;
    }

    print_ring_status();

    /* Main loop */
    poll_loop();

    /* Cleanup */
    print_stats();

    if (shm_base && shm_base != MAP_FAILED) {
        munmap(shm_base, IPC_SHARED_MEM_SIZE);
    }

    printf("[bridge] Goodbye.\n");
    return 0;
}
