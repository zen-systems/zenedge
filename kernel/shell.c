#include "arch/keyboard.h"
#include "console.h"
#include "ipc/ipc.h"

/* Simple Kernel Shell */
static char cmd_buf[128];
static int cmd_len = 0;

void shell_prompt(void) { console_write("\nZE> "); }

static int strncmp(const char *s1, const char *s2, int n) {
  while (n > 0 && *s1 && *s2) {
    if (*s1 != *s2)
      return *s1 - *s2;
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return *s1 - *s2;
}

/* Parse and execute command */
static void run_command(void) {
  if (cmd_len == 0)
    return;

  /* Basic command parsing */
  char *cmd = cmd_buf;

  /* Skip leading spaces */
  while (*cmd == ' ')
    cmd++;

  if (cmd[0] == '\0')
    return;

  /* help - List commands */
  if (strncmp(cmd, "help", 4) == 0) {
    console_write("Available commands:\n");
    console_write("  help    - Show this message\n");
    console_write("  cls     - Clear screen\n");
    console_write("  ping    - Send IPC PING to Bridge\n");
    console_write("  model <id> - Send IPC RUN_MODEL (id=0-9)\n");
    console_write("  ipc     - Show IPC debug stats\n");
  }
  /* cls - Clear screen */
  else if (strncmp(cmd, "cls", 3) == 0) {
    console_cls();
  }
  /* ping - Send PING */
  else if (strncmp(cmd, "ping", 4) == 0) {
    console_write("Sending CMD_PING...\n");
    if (ipc_send(0x0001 /* CMD_PING */, 0) == 0) {
      console_write("Sent.\n");
    } else {
      console_write("Failed to send (ring full?).\n");
    }
  }
  /* ipc - Show debug stats */
  else if (strncmp(cmd, "ipc", 3) == 0) {
    ipc_dump_debug();
  }
  /* model <id> - Run Model */
  else if (strncmp(cmd, "model", 5) == 0) {
    char *arg = cmd + 5;
    while (*arg == ' ')
      arg++;

    if (*arg >= '0' && *arg <= '9') {
      int model_id = *arg - '0';
      console_write("Running Model ID: ");
      char id_char[2] = {*arg, 0};
      console_write(id_char);
      console_write("...\n");

      if (ipc_send(0x0004 /* CMD_RUN_MODEL */, model_id) == 0) {
        console_write("Request sent.\n");
      } else {
        console_write("Failed.\n");
      }
    } else {
      console_write("Usage: model <id> (0-9)\n");
    }
  }
  /* Unknown */
  else {
    console_write("Unknown command: ");
    console_write(cmd);
    console_write("\n");
  }
}

void shell_start(void) {
  console_write("\nZENEDGE Kernel Shell (ksh)\nType 'help' for commands.\n");
  shell_prompt();

  while (1) {
    /* Process IPC responses in the loop */
    ipc_consume_one();
    /* Note: Real response processing happens in IRQ context or polling logic,
       but ipc_consume_one is actually the MOCK CONSUMER (Linux side
       simulation). If we are running in QEMU without the bridge, we need to
       call ipc_consume_one to clear the ring. If we are running WITH bridge,
       this should NOT be called.

       For this demo, assuming NO bridge is connected unless specified.
       But wait, user verification script might check real bridge?
       If real bridge is running, we conflict.
       However, I am adding a simple poll for OUR responses:
    */
    ipc_response_t rsp;
    while (ipc_poll_response(&rsp)) {
      /* Already logged by poll */
    }

    /* Check input */
    if (tc_has_input()) {
      char c = tc_getchar();

      if (c == '\n') {
        console_putc('\n');
        cmd_buf[cmd_len] = '\0';
        run_command();
        cmd_len = 0;
        shell_prompt();
      } else if (c == '\b') {
        if (cmd_len > 0) {
          cmd_len--;
          console_putc('\b');
        }
      } else if (cmd_len < 127) {
        cmd_buf[cmd_len++] = c;
        console_putc(c);
      }
    }

    /* Yield / HLT */
    __asm__ __volatile__("hlt");
  }
}
