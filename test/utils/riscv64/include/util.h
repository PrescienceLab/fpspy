// See LICENSE for license details.

#ifndef __UTIL_H
#define __UTIL_H

#include "encoding.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>

#define PIPELINED_DELEGATE_HELLO_WORLD 0x4630
#define PIPELINED_DELEGATE_INSTALL_HANDLER_TARGET 0x80084631
#define PIPELINED_DELEGATE_DELEGATE_TRAPS 0x80084632
#define PIPELINED_DELEGATE_CSR_STATUS 0x4633
#define PIPELINED_DELEGATE_FILE "/dev/pipelined-delegate"

#define REG_FMT "%016lX"

extern void trap_entry(void);

struct delegate_config_t {
  unsigned int en_flag;
  unsigned long trap_mask;
};

void enable_delegation(void) {
  int fd = open(PIPELINED_DELEGATE_FILE, O_RDWR);
  struct delegate_config_t config = {
      .en_flag = 1,
      .trap_mask = 1 << 0x18,
  };

  ioctl(fd, PIPELINED_DELEGATE_INSTALL_HANDLER_TARGET, trap_entry);
  ioctl(fd, PIPELINED_DELEGATE_DELEGATE_TRAPS, &config);
  close(fd);
}

uintptr_t handle_trap(uintptr_t cause, uintptr_t epc, uintptr_t regs[32]) {
  write_csr(0x880, 0x00);
  return epc + 4;
}

#endif  //__UTIL_H
