# FPSpy Tool

Copyright (c) 2017-2025 Peter A. Dinda  Please see `LICENSE` file.

This is a tool for floating point exception interception and
statistics gathering that can run underneath existing, unmodified
binaries.   It can operate at the whole program level (or ROI), or at the level of individual machine instructions.

The initial version of FPSpy is documented in

P. Dinda, A. Bernat, C. Hetland, Spying on the Floating Point Behavior
of Existing, Unmodifed Scientific Applications, Proceedings of the
29th ACM Symposium on High-performance Parallel and Distributed
Computing (HPDC 2020), June, 2020.

You can also see the comments in `src/fpspy.c` for some details of how
this works and what it illustrates.

FPSpy has evolved considerably since the above paper.  It currently includes architecture independence, with support for
x64, arm64 (for machines with floating point traps, which are
optional on ARM), and riscv64 (for machines with our specialized
support for floating point traps and pipelined precise exceptions).

### Architecture Selection

Source the relevant environment file for your architecture:
```
$ source ./ENV.x64
```

### Configuration

Create a default configuration:
```
$ make defconfig
```
Update your configuration:
```
$ make menuconfig
```
You can now select features from the menu, including the specific architecture to target and toolchain to use.  Note that FPSpy can be configured to have no stdout or stderr output, which is useful for deployment scenarios where such noise would be unacceptable.   There are also a range of optimizations to reduce FPSpy's individual mode overhead (aggregate mode overhead is zero).

### Kernel Module (optional x64 feature)

If you wish to use the kernel module on x64 (which considerably lowers
the cost of floating point trap delivery to FPSpy), you will need to
compile that next and insert it.  To do so, consult
`kernel/x64/README.md`. 


### Building and Testing

Make sure you have sourced the relevant ENV file and modified the
configuration as appropriate.

To build:
```
$ make
```

To test:
```
$ make test
```
You should now see a number of files with the suffix ".fpemon".   These
are output traces captured from the test_fpspy.c program under various
configurations.  See the Output and Analysis Scripts section for more
information on how to decode these.

### Running (Simple)

If you've built FPSpy successfully, and sourced the relevant `ENV` file,
you should now have a script on your path that provides a simple way to
use FPSpy.   Suppose the program you want to run FPSpy under normally
run as `./PROGRAM`.  Then:
```
$ fpspy --aggregate ./PROGRAM
```
will run FPSpy under it in aggregate mode, and
```
$ fpspy --individual ./PROGRAM
```
will run FPSpy under it in individual mode.   Aggregate mode
will capture whether any monitored FP event occurs at least once during
the run, while individual mode will capture each instruction
that causes a monitored FP event.


### Running (Details)

The `fpspy` script is a thin veneer on top of an `LD_PRELOAD` library
model with configuration by environment variables.

You generally want your environment configured as follows:
```
$ export PATH=$FPSPY_DIR/bin/$ARCH:$FPSPY_DIR/scripts:$PATH
```
The FPSpy code has two modes of operation:

- *Aggregate mode* simply captures the floating point exception state
  at the beginning and end of the program.  Since the exception state
  is sticky, this will let us know if the program had 1 or more
  occurances of each of the possible exceptions
- *Individual mode* captures individual floating point exceptions,
  emulating the instructions that cause them.

The code can be run against a dynamically linked binary which crosses
the shared library boundary for the fe* library calls, which
manipulate the FPU behavior, and for the signal and sigaction system
calls.

To run against a binary:
```
$ LD_PRELOAD=fpspy.so [FPSPY_MODE=<mode>] [FPSPY_AGGRESSIVE=<yes|no>] exec.exe
```
The modes are `aggregate` and `individual` as noted above.   If no
mode is given, aggregate mode is assumed.

Generally, FPSpy gets out of the way if the executable itself
attempts to manipulate the FPU signaling state via the fe* and
signal/sigaction system calls.  By default, it is very sensitive to
this. If `FPSPY_AGGRESSIVE` is set, then it is less sensitive, which means
that more can be captured, but the execution is more likely to be
broken.

#### Additional environment variables

- `FPSPY_DISABLE_PTHREADS=yes`    (or `DISABLE_PTHREADS=yes`)
   Do not trace newly created pthreads
   You will also want to set this for any application which
   does not dynamically link the pthread library.  Otherwise startup
   will fail when attempting to shim non-existent pthread functions.

- `FPSPY_MAXCOUNT=k`
   means that only the first `k` exceptions will be recorded
   this only affects individual mode
   `k=-1` means that there is no limit to how many exceptions
   will be recorded.  By default, `k` is about 64,000.

- `FPSPY_SAMPLE=k`
   means that only every `k`th exception will be recorded
   this only affects individual mode

- `FPSPY_EXCEPT_LIST=list`
   means that only the listed exceptions will be intercepted
   this only affects individual mode
   the comma-delimited `list` can include:

    - `invalid` (NAN)
    - `denorm`
	- `divide` (divide by zero)
	- `overflow`
	- `underflow`
	- `precision` (rounding)

- `FPSPY_POISSON=A:B`
   means that Poisson sampling will be used with the ON period
   chosen from an exponential random distro with mean `A` usec
   and OFF period chosen from an exponential distro with mean
   `B` seconds.

- `FPSPY_SEED=n`
   means the internal random number generator used for Poisson sampling is seeded with value `n`

- `FPSPY_TIMER=real|virtual|prof`  (default `real`) selects the underlying timer that will be used for Poisson sampling

    - `virtual` timer essentially means user time (time the program spends actually executing user-level instructions without being blocked).  `FPSPY_POISSON=A:B`, and `FPSPY_TIMER=virtual`, `A` and `B` are interpretted as time spent awake.    This is probably what you want if you use the Poisson sampler.
    - `real` timer means elapsed real time (wallclock time). 
    - `prof` timer is virtual time in both kernel and user space, and using a signal the application is unlikely to be using.  

- `FPSPY_KICKSTART=y|n`  (default `n`) If set to `y`, then FPSpy does not start on the initial process
until a `SIGTRAP` is delivered externally.   Otherwise, it starts immediately.
This is useful under certain scenarios such as fuzzing where
an external tool can determine a region of interest.

- `FPSPY_ABORT=y|n`  (default `n`)  
 If enabled, FPSPY will crash the program with `SIGABRT` on the first floating point trap. This is especially useful for fuzzing.

- `FPSPY_LOG_LEVEL=0|1|2` (default `2`)
If set to 0, `DEBUG` statements will be disabled, and no monitor file (*.fpemon)
will be created. If set to 1, DEBUG statements will be disabled, and a
monitor file will be created. If set to 2, all DEBUG statements will be
enabled, and a monitor file will be created.   Note that you can also configure the codebase (`make menuconfig`) to force-enable or force-disable all `DEBUG` output.

- `FPSPY_KERNEL=y|n`  (default `n`)
Attempt to use kernel support to make FP traps faster.
This is the same support as in FPVM and uses the same kernel module

- `FPSPY_FORCE_ROUNDING=positive|negative|zero|nearest[;daz][;ftz]`
This forces rounding to operate in the noted way (IEEE default is nearest).
If `daz` is included, this means all denorms are treated as zeros [Intel specific]
if `ftz` is included, this means all denorms are rounded to zeros [Intel specific]

#### Further examples

For getting a sense of how `FPSPY_POISSON` operates, you can
run:
```
$ make test_sleepy  (real timer)
```
or

```
$ make test_dopey (virtual or profile timer)
```
These test programs don't do much, but when combined with debug output enabled in FPSpy, you will be able to see the ON and OFF cycles of the Poisson sampler in operation.

To get a sense of how `FPSPY_FORCE_ROUNDING` operates, you can
run:
```
$ make test_rounding
```
This will show the effects of different forced rounding modes on a simple test program that rounds.

### Output and Analysis Scripts


FPSpy produces a trace for each thread.

In aggregate mode, a trace is short, simple, user-readable file which
is self-explanatory.

In individual mode, a trace is a binary format file which may be huge.
We provide tools to display and analyze such traces.

in `include/` and `src/`: 

 - `libtrace.h` and `libtrace.c` is a library for  trace access from C via memory mapping.  The trace shows up as a giant array of structs. 
 - `trace_print.c` gives an example use of the library, simply printing the file in human-readable format.

In `scripts/`:

 - `parse_individual.pl` is `trace_print` in Perl

 - `analyze_individual.pl` creates a detailed report from a trace.

 - `extrace_fp_event_timestamps.pl` creates a time series from a trace.
 - `disassem_instr.pl` disassembles instructions for x64, arm64, and riscv64
