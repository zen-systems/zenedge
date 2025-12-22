/* kernel/mm/pmm.c
 *
 * Physical Memory Manager implementation
 *
 * Uses a simple bitmap allocator where each bit represents a 4KB page.
 * Parses the multiboot memory map to identify available regions.
 *
 * NUMA Simulation:
 * We split the physical memory in half to simulate 2 NUMA nodes:
 * - Node 0: Lower half (latency-sensitive, "local")
 * - Node 1: Upper half (background work, "remote")
 */
#include "pmm.h"
#include "../console.h"
#include "../trace/flightrec.h"

/* Bitmap for tracking page allocation
 * For 256MB max memory with 4KB pages = 65536 pages = 8KB bitmap
 */
#define BITMAP_SIZE (PMM_MAX_PAGES / 8)
static uint8_t page_bitmap[BITMAP_SIZE];

/* Memory regions from multiboot */
#define MAX_MEM_REGIONS 32
static mem_region_t mem_regions[MAX_MEM_REGIONS];
static uint32_t num_regions = 0;

/* NUMA nodes - simulated by splitting memory in half */
static numa_node_t numa_nodes[NUMA_MAX_NODES];
static uint8_t numa_node_count = 0;

/* Global stats */
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;
static uint32_t highest_page = 0;

/* The PFN boundary between node 0 and node 1 */
static uint32_t node_boundary_pfn = 0;

/* Kernel physical addresses (defined in linker script) */
extern char _kernel_phys_start[];
extern char _kernel_phys_end[];

/* Virtual kernel base (for address conversion) */
#define KERNEL_VBASE 0xC0000000

/* Helper: set a bit in the bitmap (mark page as used) */
static inline void bitmap_set(uint32_t pfn) {
  if (pfn < PMM_MAX_PAGES) {
    page_bitmap[pfn / 8] |= (1 << (pfn % 8));
  }
}

/* Helper: clear a bit in the bitmap (mark page as free) */
static inline void bitmap_clear(uint32_t pfn) {
  if (pfn < PMM_MAX_PAGES) {
    page_bitmap[pfn / 8] &= ~(1 << (pfn % 8));
  }
}

/* Helper: test if a bit is set */
static inline int bitmap_test(uint32_t pfn) {
  if (pfn < PMM_MAX_PAGES) {
    return (page_bitmap[pfn / 8] >> (pfn % 8)) & 1;
  }
  return 1; /* Out of range = used */
}

/* Print hex value */
static void print_hex(uint32_t val) {
  char buf[9];
  const char *hex = "0123456789ABCDEF";
  for (int i = 7; i >= 0; i--) {
    buf[i] = hex[val & 0xF];
    val >>= 4;
  }
  buf[8] = '\0';
  console_write("0x");
  console_write(buf);
}

/* Parse multiboot memory map */
static void parse_mmap(multiboot_info_t *mboot) {
  /* Debug: show multiboot pointer and flags */
  console_write("[pmm] mboot at ");
  print_hex((uint32_t)mboot);
  console_write(", flags=");
  print_hex(mboot->flags);
  console_write("\n");

  if (!(mboot->flags & MULTIBOOT_FLAG_MMAP)) {
    console_write("[pmm] WARNING: No memory map from bootloader!\n");
    /* Fallback: use mem_lower/mem_upper if available */
    if (mboot->flags & MULTIBOOT_FLAG_MEM) {
      console_write("[pmm] Using mem_lower/mem_upper: ");
      print_uint(mboot->mem_lower);
      console_write(" KB / ");
      print_uint(mboot->mem_upper);
      console_write(" KB\n");

      mem_regions[0].base = 0;
      mem_regions[0].length = mboot->mem_lower * 1024;
      mem_regions[0].type = MEM_REGION_AVAILABLE;

      mem_regions[1].base = 0x100000; /* 1MB */
      mem_regions[1].length = mboot->mem_upper * 1024;
      mem_regions[1].type = MEM_REGION_AVAILABLE;
      num_regions = 2;
    }
    return;
  }

  console_write("[pmm] parsing memory map...\n");

  multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)mboot->mmap_addr;
  multiboot_mmap_entry_t *end =
      (multiboot_mmap_entry_t *)(mboot->mmap_addr + mboot->mmap_length);

  while (entry < end && num_regions < MAX_MEM_REGIONS) {
    /* Only handle memory below 4GB (we're 32-bit) */
    if (entry->base_addr < 0x100000000ULL) {
      mem_regions[num_regions].base = (paddr_t)entry->base_addr;

      /* Clamp length to 32-bit range */
      uint64_t len = entry->length;
      if (entry->base_addr + len > 0x100000000ULL) {
        len = 0x100000000ULL - entry->base_addr;
      }
      mem_regions[num_regions].length = (uint32_t)len;
      mem_regions[num_regions].type = (mem_region_type_t)entry->type;
      num_regions++;
    }

    /* Move to next entry (size field doesn't include itself) */
    entry = (multiboot_mmap_entry_t *)((uint8_t *)entry + entry->size + 4);
  }

  console_write("[pmm] found ");
  print_uint(num_regions);
  console_write(" memory regions\n");
}

/* Initialize bitmap based on memory regions */
static void init_bitmap(void) {
  /* Start with all pages marked as used */
  for (uint32_t i = 0; i < BITMAP_SIZE; i++) {
    page_bitmap[i] = 0xFF;
  }

  /* Mark available regions as free */
  for (uint32_t r = 0; r < num_regions; r++) {
    if (mem_regions[r].type != MEM_REGION_AVAILABLE) {
      continue;
    }

    paddr_t base = page_align_up(mem_regions[r].base);
    paddr_t end = page_align_down(mem_regions[r].base + mem_regions[r].length);

    if (end <= base)
      continue;

    uint32_t start_pfn = addr_to_pfn(base);
    uint32_t end_pfn = addr_to_pfn(end);

    /* Clamp to our maximum */
    if (end_pfn > PMM_MAX_PAGES) {
      end_pfn = PMM_MAX_PAGES;
    }

    for (uint32_t pfn = start_pfn; pfn < end_pfn; pfn++) {
      bitmap_clear(pfn);
      free_pages++;
      if (pfn > highest_page) {
        highest_page = pfn;
      }
    }
  }

  total_pages = highest_page + 1;
}

/* Reserve low memory and kernel region */
static void reserve_kernel(void) {
  /* Reserve first 1MB (BIOS, VGA, etc.) */
  pmm_reserve_range(0, 0x100000);

  /* Reserve kernel itself
   * Kernel physical addresses come from linker script
   * Note: _kernel_phys_start/_end are defined as physical addresses
   */
  paddr_t kernel_start = (paddr_t)_kernel_phys_start;
  paddr_t kernel_end = (paddr_t)_kernel_phys_end;

  console_write("[pmm] kernel: phys ");
  print_hex(kernel_start);
  console_write(" - ");
  print_hex(kernel_end);
  console_write("\n");

  pmm_reserve_range(kernel_start, kernel_end - kernel_start);
}

/* Set up simulated NUMA nodes by splitting memory in half */
static void setup_numa_nodes(void) {
  /* Split usable memory (above 1MB) into two nodes */
  uint32_t usable_start_pfn = 256; /* 1MB / 4KB */
  uint32_t usable_pages = highest_page - usable_start_pfn + 1;

  node_boundary_pfn = usable_start_pfn + (usable_pages / 2);

  /* Node 0: Lower half (local/latency-sensitive) */
  numa_nodes[0].node_id = 0;
  numa_nodes[0].base_addr = pfn_to_addr(usable_start_pfn);
  numa_nodes[0].start_pfn = usable_start_pfn;
  numa_nodes[0].end_pfn = node_boundary_pfn;
  numa_nodes[0].total_pages = node_boundary_pfn - usable_start_pfn;
  numa_nodes[0].free_pages = 0;
  numa_nodes[0].used_pages = 0;

  /* Node 1: Upper half (remote/background) */
  numa_nodes[1].node_id = 1;
  numa_nodes[1].base_addr = pfn_to_addr(node_boundary_pfn);
  numa_nodes[1].start_pfn = node_boundary_pfn;
  numa_nodes[1].end_pfn = highest_page + 1;
  numa_nodes[1].total_pages = (highest_page + 1) - node_boundary_pfn;
  numa_nodes[1].free_pages = 0;
  numa_nodes[1].used_pages = 0;

  numa_node_count = 2;

  /* Count free pages per node */
  for (uint32_t pfn = usable_start_pfn; pfn <= highest_page; pfn++) {
    if (!bitmap_test(pfn)) {
      if (pfn < node_boundary_pfn) {
        numa_nodes[0].free_pages++;
      } else {
        numa_nodes[1].free_pages++;
      }
    }
  }

  /* Calculate used pages */
  numa_nodes[0].used_pages =
      numa_nodes[0].total_pages - numa_nodes[0].free_pages;
  numa_nodes[1].used_pages =
      numa_nodes[1].total_pages - numa_nodes[1].free_pages;

  console_write("[pmm] NUMA node 0: PFN ");
  print_uint(numa_nodes[0].start_pfn);
  console_write("-");
  print_uint(numa_nodes[0].end_pfn);
  console_write(" (");
  print_uint(numa_nodes[0].free_pages);
  console_write(" free)\n");

  console_write("[pmm] NUMA node 1: PFN ");
  print_uint(numa_nodes[1].start_pfn);
  console_write("-");
  print_uint(numa_nodes[1].end_pfn);
  console_write(" (");
  print_uint(numa_nodes[1].free_pages);
  console_write(" free)\n");
}

void pmm_init(multiboot_info_t *mboot_info) {
  console_write("[pmm] initializing physical memory manager\n");

  /* Parse memory map from bootloader */
  if (mboot_info) {
    parse_mmap(mboot_info);
  } else {
    /* Manual fallback for manual init (e.g. x86_64 stub) */
    console_write("[pmm] No multiboot info, assuming 128MB RAM\n");
    mem_regions[0].base = 0;
    mem_regions[0].length = 640 * 1024;
    mem_regions[0].type = MEM_REGION_AVAILABLE;

    mem_regions[1].base = 0x100000;
    mem_regions[1].length = (128 * 1024 * 1024) - 0x100000;
    mem_regions[1].type = MEM_REGION_AVAILABLE;
    num_regions = 2;
  }

  /* Initialize page bitmap */
  init_bitmap();

  /* Reserve kernel and low memory */
  reserve_kernel();

  /* Set up simulated NUMA topology */
  setup_numa_nodes();

  /* Log initialization */
  flightrec_log(TRACE_EVT_BOOT, 0, 0, free_pages);

  console_write("[pmm] total memory: ");
  print_uint((highest_page + 1) * 4);
  console_write(" KB (");
  print_uint(highest_page + 1);
  console_write(" pages)\n");

  console_write("[pmm] free memory: ");
  print_uint(free_pages * 4);
  console_write(" KB (");
  print_uint(free_pages);
  console_write(" pages)\n");

  console_write("[pmm] init complete\n");
}

/* Internal: allocate from a specific node's range */
static paddr_t alloc_from_node(uint8_t node) {
  if (node >= numa_node_count) {
    return 0;
  }

  numa_node_t *n = &numa_nodes[node];

  for (uint32_t pfn = n->start_pfn; pfn < n->end_pfn; pfn++) {
    if (!bitmap_test(pfn)) {
      bitmap_set(pfn);
      free_pages--;
      n->free_pages--;
      n->used_pages++;
      return pfn_to_addr(pfn);
    }
  }

  return 0; /* Node exhausted */
}

paddr_t pmm_alloc_page(uint8_t node) {
  paddr_t addr;

  /* Handle special node values */
  if (node == NUMA_NODE_ANY) {
    /* Try node 0 first, then node 1 */
    addr = alloc_from_node(0);
    if (addr)
      return addr;
    addr = alloc_from_node(1);
    if (addr) {
      /* Log locality miss - allocated from non-preferred node */
      flightrec_log(TRACE_EVT_MEM_LOCALITY_MISS, 0, 0, 1);
    }
    return addr;
  }

  /* Validate node ID */
  if (node >= numa_node_count) {
    /* Log unsupported node request */
    flightrec_log(TRACE_EVT_MEM_NODE_UNSUPPORTED, 0, 0, node);
    console_write("[pmm] WARNING: unsupported NUMA node ");
    print_uint(node);
    console_write(", falling back to node 0\n");
    node = 0;
  }

  /* Try preferred node first */
  addr = alloc_from_node(node);
  if (addr) {
    return addr;
  }

  /* Preferred node exhausted - try other nodes */
  for (uint8_t i = 0; i < numa_node_count; i++) {
    if (i == node)
      continue;
    addr = alloc_from_node(i);
    if (addr) {
      /* Log locality miss */
      flightrec_log(TRACE_EVT_MEM_LOCALITY_MISS, 0, node, i);
      return addr;
    }
  }

  /* All nodes exhausted */
  flightrec_log(TRACE_EVT_MEM_ALLOC_FAIL, 0, node, 1);
  console_write("[pmm] ERROR: out of memory!\n");
  return 0;
}

paddr_t pmm_alloc_pages(uint32_t count, uint8_t node) {
  if (count == 0)
    return 0;
  if (count == 1)
    return pmm_alloc_page(node);

  /* Validate node ID */
  if (node != NUMA_NODE_ANY && node >= numa_node_count) {
    flightrec_log(TRACE_EVT_MEM_NODE_UNSUPPORTED, 0, 0, node);
    node = 0;
  }

  /* Determine search range based on node */
  uint32_t search_start, search_end;
  uint8_t target_node;

  if (node == NUMA_NODE_ANY) {
    /* Search all usable memory */
    search_start = 256;
    search_end = highest_page + 1;
    target_node = 0; /* Will be updated based on where we find pages */
  } else {
    search_start = numa_nodes[node].start_pfn;
    search_end = numa_nodes[node].end_pfn;
    target_node = node;
  }

  /* Search for contiguous free pages */
  uint32_t start_pfn = search_start;

  while (start_pfn + count <= search_end) {
    uint32_t found = 0;

    for (uint32_t i = 0; i < count; i++) {
      if (bitmap_test(start_pfn + i)) {
        start_pfn = start_pfn + i + 1;
        found = 0;
        break;
      }
      found++;
    }

    if (found == count) {
      /* Found contiguous region, allocate it */
      for (uint32_t i = 0; i < count; i++) {
        bitmap_set(start_pfn + i);
      }
      free_pages -= count;

      /* Update per-node stats */
      if (node == NUMA_NODE_ANY) {
        target_node = pmm_addr_to_node(pfn_to_addr(start_pfn));
      }
      numa_nodes[target_node].free_pages -= count;
      numa_nodes[target_node].used_pages += count;

      return pfn_to_addr(start_pfn);
    }
  }

  /* If we were searching a specific node and failed, try fallback */
  if (node != NUMA_NODE_ANY) {
    for (uint8_t i = 0; i < numa_node_count; i++) {
      if (i == node)
        continue;
      paddr_t addr = pmm_alloc_pages(count, i);
      if (addr) {
        flightrec_log(TRACE_EVT_MEM_LOCALITY_MISS, 0, node, i);
        return addr;
      }
    }
  }

  flightrec_log(TRACE_EVT_MEM_ALLOC_FAIL, 0, node, count);
  console_write("[pmm] ERROR: cannot allocate ");
  print_uint(count);
  console_write(" contiguous pages!\n");
  return 0;
}

void pmm_free_page(paddr_t addr) {
  uint32_t pfn = addr_to_pfn(addr);

  if (pfn > highest_page) {
    console_write("[pmm] WARNING: freeing invalid page!\n");
    return;
  }

  if (!bitmap_test(pfn)) {
    console_write("[pmm] WARNING: double free detected!\n");
    return;
  }

  bitmap_clear(pfn);
  free_pages++;

  /* Update per-node stats */
  uint8_t node = pmm_addr_to_node(addr);
  if (node < numa_node_count) {
    numa_nodes[node].free_pages++;
    numa_nodes[node].used_pages--;
  }
}

void pmm_free_pages(paddr_t addr, uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    pmm_free_page(addr + i * PAGE_SIZE);
  }
}

void pmm_reserve_range(paddr_t base, uint32_t length) {
  paddr_t start = page_align_down(base);
  paddr_t end = page_align_up(base + length);

  uint32_t start_pfn = addr_to_pfn(start);
  uint32_t end_pfn = addr_to_pfn(end);

  if (end_pfn > PMM_MAX_PAGES) {
    end_pfn = PMM_MAX_PAGES;
  }

  for (uint32_t pfn = start_pfn; pfn < end_pfn; pfn++) {
    if (!bitmap_test(pfn)) {
      bitmap_set(pfn);
      free_pages--;
    }
  }
}

void pmm_get_stats(pmm_stats_t *stats) {
  stats->total_memory_kb = (highest_page + 1) * 4;
  stats->free_memory_kb = free_pages * 4;
  stats->used_memory_kb = stats->total_memory_kb - stats->free_memory_kb;
  stats->reserved_memory_kb = 0; /* TODO: track separately */
  stats->total_pages = total_pages;
  stats->free_pages = free_pages;
  stats->num_regions = num_regions;
  stats->num_nodes = numa_node_count;
}

numa_node_t *pmm_get_node(uint8_t node_id) {
  if (node_id < numa_node_count) {
    return &numa_nodes[node_id];
  }
  return (numa_node_t *)0;
}

uint8_t pmm_get_node_count(void) { return numa_node_count; }

uint8_t pmm_addr_to_node(paddr_t addr) {
  uint32_t pfn = addr_to_pfn(addr);

  for (uint8_t i = 0; i < numa_node_count; i++) {
    if (pfn >= numa_nodes[i].start_pfn && pfn < numa_nodes[i].end_pfn) {
      return i;
    }
  }

  return 0; /* Default to node 0 if not found */
}

void pmm_dump_map(void) {
  console_write("\n=== MEMORY MAP ===\n");
  console_write("REGION | BASE       | LENGTH     | TYPE\n");
  console_write("-------|------------|------------|------------\n");

  for (uint32_t i = 0; i < num_regions; i++) {
    print_uint(i);
    console_write("      | ");
    print_hex(mem_regions[i].base);
    console_write(" | ");
    print_hex(mem_regions[i].length);
    console_write(" | ");

    switch (mem_regions[i].type) {
    case MEM_REGION_AVAILABLE:
      console_write("Available");
      break;
    case MEM_REGION_RESERVED:
      console_write("Reserved");
      break;
    case MEM_REGION_ACPI_RECLAIMABLE:
      console_write("ACPI Reclaim");
      break;
    case MEM_REGION_ACPI_NVS:
      console_write("ACPI NVS");
      break;
    case MEM_REGION_BAD:
      console_write("Bad Memory");
      break;
    default:
      console_write("Unknown");
      break;
    }
    console_write("\n");
  }

  console_write("\n=== NUMA TOPOLOGY ===\n");
  for (uint8_t i = 0; i < numa_node_count; i++) {
    console_write("Node ");
    print_uint(i);
    console_write(": ");
    print_uint(numa_nodes[i].free_pages * 4);
    console_write(" KB free / ");
    print_uint(numa_nodes[i].total_pages * 4);
    console_write(" KB total\n");
  }
  console_write("=== END MAP ===\n\n");
}
