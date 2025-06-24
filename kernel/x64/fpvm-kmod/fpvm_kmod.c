#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/entry-common.h>

#include <asm/io.h>
#include <asm/current.h>
#include <asm/desc.h>
#include <asm/desc_defs.h>
#include <asm/cpu_entry_area.h>
#include <asm/page.h>
#include <asm/apic.h>

#include <uapi/asm-generic/errno-base.h>

#include "fpvm_ioctl.h"

#define X86_TRAP_XF 19

unsigned long fpvm_error_entry;
module_param_named(fpvm_error_entry, fpvm_error_entry, ulong, 0);
MODULE_PARM_DESC(fpvm_error_entry, "Address of the `fpvm_error_entry` symbol");

unsigned long fpvm_error_return;
module_param_named(fpvm_error_return, fpvm_error_return, ulong, 0);
MODULE_PARM_DESC(fpvm_error_return, "Address of the `fpvm_error_return` symbol");

unsigned long fpvm_math_error;
module_param_named(fpvm_math_error, fpvm_math_error, ulong, 0);
MODULE_PARM_DESC(fpvm_math_error, "Address of the `fpvm_math_error` symbol");

unsigned long fpvm_irqentry_enter;
module_param_named(fpvm_irqentry_enter, fpvm_irqentry_enter, ulong, 0);
MODULE_PARM_DESC(fpvm_irqentry_enter, "Address of the `fpvm_irqentry_enter` symbol");

unsigned long fpvm_irqentry_exit;
module_param_named(fpvm_irqentry_exit, fpvm_irqentry_exit, ulong, 0);
MODULE_PARM_DESC(fpvm_irqentry_exit, "Address of the `fpvm_irqentry_exit` symbol");


unsigned long error_entry;
unsigned long error_return;

extern void *_fpvm_idt_entry;
extern void *_fpvm_hw_timing_idt_entry;

uint64_t original_xf_handler = 0;


/*
==================
Handler Stuff
==================
*/

struct user_proc_info {
  struct list_head node;
  void (*user_handler)(void);
  pid_t pid;
};

DEFINE_MUTEX(fpvm_dev_lock);

// TODO: use a red-black tree instead
static struct list_head upi_list = LIST_HEAD_INIT(upi_list);


static struct user_proc_info *upi_find(pid_t target_pid) {
  struct user_proc_info *cur = NULL;
  struct user_proc_info *n = NULL;
  int found = 0;

  // BUG?: deletion from list by another thread?
  // TODO: add read-write lock
  list_for_each_entry_safe(cur, n, &upi_list, node) {
    if (cur->pid == target_pid) {
      found = 1;
      break;
    }
  }
  if (found) {
    return cur;
  }
  return NULL;
}

static void upi_add(struct user_proc_info *upi) { list_add(&upi->node, &upi_list); }

static void upi_del(struct user_proc_info *upi) { list_del(&upi->node); }



static inline uint64_t my_rdtsc(void) {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return lo | ((uint64_t)(hi) << 32);
}


void the_fpvm_hook(struct pt_regs *regs) {
  /* if (regs->r15 == 0xFFEEFF) { */
  /*   regs->r15 = my_rdtsc(); // Record the hw->kernel time into r15 */
  /*   regs->ip += 4; // Skip the instruction which caused the fault */
  /* } */

  struct user_proc_info *upi = upi_find(current->pid);
  if (upi) {
    /* saving RIP to stack */
    uint64_t old_sp = regs->sp;
    uint64_t state[2] = {old_sp, regs->ip};
    regs->sp -= 152;
    // Because we need to do alignment, I need to give myself a little buffer
    // for copying the saved RIP

    regs->sp -= 0x8;
    regs->sp &= 0xFFFFFFFFFFFFFFF0;  // This alignment is why I need to copy the RIP 2x

    // Copy %rip here for our state save area
    regs->sp -= 0x10;
    if (copy_to_user((void *)regs->sp, &state, sizeof(state))) {
      printk("copy to user..\n");
    }

    /* setting RIP to user proc's handler */
    regs->ip = (long unsigned int)upi->user_handler;
    return;
  }

  irqentry_state_t state = ((irqentry_state_t(*)(struct pt_regs *))fpvm_irqentry_enter)(regs);
  ((void (*)(struct pt_regs *, int))fpvm_math_error)(regs, X86_TRAP_XF);
  ((void (*)(struct pt_regs *, irqentry_state_t))fpvm_irqentry_exit)(regs, state);
  return;
}

EXPORT_SYMBOL(the_fpvm_hook);

static void fpvm_remove_handlers(pid_t target) {
  struct user_proc_info *upi = upi_find(target);
  mutex_lock_interruptible(&fpvm_dev_lock);
  while (upi) {
    upi_del(upi);
    kfree(upi);
    upi = upi_find(target);
  }
  mutex_unlock(&fpvm_dev_lock);
  printk("Removed all handlers for %ld\n", (long int)target);
  return;
}

static void fpvm_add_handler(void *handle_func) {
  struct user_proc_info *new_upi;
  mutex_lock_interruptible(&fpvm_dev_lock);

  new_upi = kmalloc(sizeof(struct user_proc_info), GFP_KERNEL);
  new_upi->user_handler = handle_func;
  new_upi->pid = current->pid;
  upi_add(new_upi);

  mutex_unlock(&fpvm_dev_lock);
  printk("PID %ld registered handler at %px\n", (long int)new_upi->pid, new_upi->user_handler);
  return;
}

/*
=====================
Device Driver Stuff
=====================
*/

static struct cdev fpvm_cdev;
static struct class *fpvm_class;

static int fpvm_open(struct inode *inode, struct file *file) { return 0; }

static int fpvm_release(struct inode *inode, struct file *file) {
  fpvm_remove_handlers(current->pid);
  printk("Released FPVM Dev\n");
  return 0;
}

static long fpvm_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  switch (cmd) {
    case FPVM_IOCTL_REG:
      fpvm_add_handler((void *)arg);
      break;
    case FPVM_IOCTL_UNREG:
      fpvm_remove_handlers(current->pid);
      break;
    default:
      printk("Invalid FPVM IOCTL\n");
      return -EINVAL;
  }
  return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE, .open = fpvm_open, .unlocked_ioctl = fpvm_ioctl, .release = fpvm_release};

static int create_fpvm_dev(void) {
  int err = register_chrdev_region(MKDEV(MAJOR_NUM, 0), MAX_MINORS, "fpvm_device_driver");

  if (err != 0) {
    printk("[!] Unable to register FPVM Device Driver\n");
    return err;
  }

  cdev_init(&fpvm_cdev, &fops);
  fpvm_class = class_create(THIS_MODULE, "fpvm_class");
  device_create(fpvm_class, NULL, MKDEV(MAJOR_NUM, 0), NULL, "fpvm_dev");
  cdev_add(&fpvm_cdev, MKDEV(MAJOR_NUM, 0), 1);
  return 0;
}

static int destroy_fpvm_dev(void) {
  device_destroy(fpvm_class, MKDEV(MAJOR_NUM, 0));
  class_destroy(fpvm_class);
  cdev_del(&fpvm_cdev);
  unregister_chrdev_region(MKDEV(MAJOR_NUM, 0), MAX_MINORS);
  printk("[*] FPVM Device Driver unregistered\n");
  return 0;
}

/*
=====================
Helpers for IDT Manipulation
=====================
*/

#define CR0_WP (1u << 16)

static void force_write_cr0(unsigned long new_val) {
  asm __volatile__(
      "mov %0, %%rdi;"
      "mov %%rdi, %%cr0;" ::"m"(new_val));
}

static unsigned long force_read_cr0(void) {
  unsigned long cr0_val;
  asm __volatile__(
      "mov %%cr0, %%rdi;"
      "mov %%rdi, %0;"
      : "=m"(cr0_val));
  return cr0_val;
}

static struct desc_ptr get_idtr(void) {
  struct desc_ptr idtr;
  asm __volatile__("sidt %0" : "=m"(idtr));
  return idtr;
}

static uint64_t extract_handler_address(gate_desc *gd) {
  uint64_t handler;
  handler = (uint64_t)gd->offset_low;
  handler += (uint64_t)gd->offset_middle << 16;
  handler += (uint64_t)gd->offset_high << 32;
  return handler;
}

static void write_handler_address_to_gd(gate_desc *gd, unsigned long handler) {
  uint16_t low = (uint16_t)(handler);
  uint16_t middle = (uint16_t)(handler >> 16);
  uint32_t high = (uint32_t)(handler >> 32);

  gd->offset_low = low;
  gd->offset_middle = middle;
  gd->offset_high = high;
}

/*
==================
Modify & Restore IDT
==================
*/

static void modify_idt(void) {
  struct desc_ptr IDTR;
  gate_desc *idt;
  gate_desc *XF_gate_desc;
  uint64_t new_XF_handler;

  // Get IDTR to find IDT base
  IDTR = get_idtr();
  idt = (gate_desc *)IDTR.address;
  // Offset into IDT to #XF Gate Desc
  XF_gate_desc = idt + X86_TRAP_XF;

  // Save the old gate descriptor info in case
  original_xf_handler = extract_handler_address(XF_gate_desc);
  printk("Old XF handler: 0x%llx\n", original_xf_handler);

  // Disable write protections
  force_write_cr0(force_read_cr0() & ~(CR0_WP));

  // Put our fake gate descriptor in
#if 1
  write_handler_address_to_gd(XF_gate_desc, (unsigned long)&_fpvm_idt_entry);
#else
  write_handler_address_to_gd(XF_gate_desc, (unsigned long)&_fpvm_hw_timing_idt_entry);
#endif
  new_XF_handler = extract_handler_address(XF_gate_desc);
  printk("New XF handler: 0x%llx\n", new_XF_handler);

  // Enable write protections
  force_write_cr0(force_read_cr0() | CR0_WP);

  // Profit
  printk("[!] IDT Modification done!\n");
  return;
}

// Called during our cleanup. Restore the original xf handler.
static void restore_idt(void) {
  struct desc_ptr IDTR;
  gate_desc *idt;
  gate_desc *XF_gate_desc;

  if (original_xf_handler == 0) {
    return;
  }
  IDTR = get_idtr();
  idt = (gate_desc *)IDTR.address;
  XF_gate_desc = idt + X86_TRAP_XF;

  printk("[*] Restoring original XF handler\n");
  force_write_cr0(force_read_cr0() & ~(CR0_WP));
  write_handler_address_to_gd(XF_gate_desc, (unsigned long)original_xf_handler);
  force_write_cr0(force_read_cr0() | CR0_WP);
  return;
}


/*
===============
Kernel Mod init + exit
==============-
*/

static int __init test_init(void) {
  int err;
  unsigned long flags;

  local_irq_save(flags);

  if (!fpvm_error_entry || !fpvm_error_return || !fpvm_math_error || !fpvm_irqentry_enter ||
      !fpvm_irqentry_exit) {
    printk("[X] fpvm_dev: invalid params\n");
    return -1;
  }
  /* replace default xf handler with _fpvm_idt_entry */
  modify_idt();

  /* register the device and stuff */
  err = create_fpvm_dev();
  if (err) {
    return err;
  }

  local_irq_restore(flags);
  return 0;
}


static void __exit test_exit(void) {
  unsigned long flags;
  local_irq_save(flags);

  /* restore original xf handler into idt */
  restore_idt();

  /* unregister the device */
  destroy_fpvm_dev();

  local_irq_restore(flags);
}

module_init(test_init);
module_exit(test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nadharm");
MODULE_DESCRIPTION("Allow user processes to register faster exception handlers");
MODULE_VERSION("0.0");
