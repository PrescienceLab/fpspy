#!/bin/bash

pushd fpvm-kmod
  sudo insmod fpvm_dev.ko \
    fpvm_error_entry=0x$(sudo grep ' error_entry$' /proc/kallsyms | cut -d' ' -f1) \
    fpvm_error_return=0x$(sudo grep ' error_return$' /proc/kallsyms | cut -d' ' -f1) \
    fpvm_math_error=0x$(sudo grep ' math_error$' /proc/kallsyms | cut -d' ' -f1) \
    fpvm_irqentry_enter=0x$(sudo grep ' irqentry_enter$' /proc/kallsyms | cut -d' ' -f1) \
    fpvm_irqentry_exit=0x$(sudo grep ' irqentry_exit$' /proc/kallsyms | cut -d' ' -f1) 

  pushd /dev
    sudo mknod fpvm_dev c 17 0

    sudo chmod 777 /dev/fpvm_dev

  popd
popd

