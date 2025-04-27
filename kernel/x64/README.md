# FPSpy on x64 with  with Kernel Module

This is a variation on kernel module support for the
companion FPVM project, which is why "fpvm" appears in
this subdirectory.  The majority of changes are in how
the interrupt return process works, and the code for
that is in fpspy itself, in particular
fpspy/src/x64/user_fpspy_entry.S

As with all kernel modules, this requires root to install, and it can
be sensitive to particular kernel versions.  We test on an AMD EPYC
7443P 24-Core Processor with kernel 5.15.0-134-generic on Ubuntu
22.04.4. LTS.

### Build kernel module:
```
./kmod_build.sh
```
If successful, this will leave you with fpvm-kmod/fpvm-dev.ko

### Insert kernel module:
```
sudo ./kmod_setup.sh
```
If successful, an lsmod will show fpvm-dev as an available
module.    Inserting this module is more complicated than a
simple insmod because we must make it aware of various symbols
in the core kernel that are a challenge to discover within the
module itself.

### Use with FPSpy

To use with FPSpy, you must enable the *CONFIG_TRAP_SHORT_CIRCUITING* feature
in fpspy/include/config.h.   It is safe to enable this feature even if the
kernel module is not available, since FPSpy will fall back on regular
SIGFPE signalling.

### To measure latencies

The subdirectory signal_latency contains code for measuring floating point
trap costs when delivered using SIGFPE and via the kernel module.
