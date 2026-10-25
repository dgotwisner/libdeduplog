# Deduplicating Logger with Signal Handler Safe Logging API

## Overview

Deduplog is a logger capable of using any of systemd-journald, syslog, or fprintf (stderr or stdout) to log data to the output.

It is a relatively straightforward drop-in for any logger you might already be using. It supports both `printf()` style formatting and `vprintf()` style formatting.

It supports a signal handler safe logger (slightly different parameterization).

It supports APIs that log immediately without deduplication.

It supports APIs that discard duplicates based upon the pre-formatted **format** argument (referencing the standard `printf` manual page) occurring within a configurable time period.

It also allows for APIs that are not deduplicated (the Immediate and Safe APIs) and that have duplicates thrown out (the Dedupped APIs) based upon an integer and a `const char *` string (usually, but not limited to `__LINE__` and `__FILE__`), on a per-thread basis. Because these identifier parameters are passed in explicitly rather than being hardcoded into the library internals, users can supply alternative metrics for tracking custom contexts in specialized use cases.

Because the API takes `printf()` style syntax, deduplication is evaluated against the raw format pattern itself rather than the final post-rendered string. The first unique message instance prints immediately with its variables intact. After a configurable period expires (recommended default is 30 seconds), the engine outputs the format pattern alongside a precise count of dropped duplicate frames. This tracking operates entirely on a per-thread basis to eliminate mutex contention on active output streams. With this approach, a mutex lock is only required on thread creation/destruction boundaries or when invoking the system `openlog()` setup for syslog output paths.

What this means in practice is that the invocation:
```c
LogDedupped(LOG_ERR, __LINE__, __FILE__, "This is message %d of %d", value, total);
```
will have:
```text
This is message 1 of 100
```
show up once (per thread). If 1,000 more matching frames hit that line within the subsequent 30-second window, you will then see a unified summary line emitted on thread or timeout shutdown:
```text
The following message pattern (This is message %d of %d) from file foo.cpp, line 100, occurred 1000 times in 30 seconds
```
assuming the source file is **foo.cpp** and the line number of the log message is **100**.

This library is written in C++ but exposes a clean, stable C-compatible ABI. Any programming language that can bind to native C programs and supports variadic functions can consume it, provided they link against the standard `libstdc++` runtime library. The underlying engine utilizes C++11 features, ensuring backward compatibility with almost any modern compiler ecosystem. For the native C boundary, no restrictive language specification is enforced, supporting the minimal common denominators for legacy code integrations.

Detailed enum and API listings are provided below, but generating the local Doxygen suite remains the recommended way to view deep documentation. Between the logical function names and the Quick Start guide, you should be fully productive within minutes.

## Required Includes

* `syslog.h` for syslog output
* `systemd/sd-journal.h` for systemd-journald output
* `stdio.h` (or `<cstdio>`) for stdout/stderr support
* `sys/uio.h` for the signal handler safe `writev()` API

If syslog or systemd-journald are not available on the host platform, those output paths are safely disabled at compile time.

If `sys/uio.h` is not available, the fallback engine executes `write()` in a loop. Note that this fallback path is slower and increases the likelihood of interspersed output from competing threads.

Building the codebase automatically generates a `config.h` file that runs these system checks, cleanly enabling or disabling capabilities as required.

## APIs

Detailed documentation for the enums and APIs can be found by generating the Doxygen output.

```c
typedef enum LoggerInterface
{
    LI_JOURNAL, /*!< Output to systemd-journald */
    LI_SYSLOG,  /*!< Output to syslog */
    LI_STDERR,  /*!< Output to stderr, formatted in a way compatible with systemd-journald */
    LI_STDOUT,  /*!< Output to stdout, formatted in a way compatible with systemd-journald */
} LoggerInterface_t;
```

* `int getLogLevel(void);`
* `int setLogLevel(int level);`
* `uint64_t getTimeThreshold(void);`
* `int setTimeThreshold(uint64_t threshold);`
* `LoggerInterface_t getLoggerInterface(void);`
* `int setLoggerInterface(LoggerInterface_t interface);`
* `void LogSafe(int level, const struct iovec *iov, int iovcnt);`
* `void LogImmediate(int level, const char *pattern, ...);`
* `void LogImmediateVA(int level, const char *pattern, va_list ap);`
* `void LogDedupped(int level, int lineNumber, const char *filename, const char *pattern, ...);`
* `void LogDeduppedVA(int level, int lineNumber, const char *filename, const char *pattern, va_list ap);`

*Note: The native `bool` data type is intentionally omitted to maintain compatibility with legacy strict C baselines.*

The `VA` functions are designed to ingest `va_list` arguments (matching `vprintf` mechanics), while the standard versions accept traditional variadic `printf` tokens.

`LogSafe` is explicitly safe for execution within an asynchronous signal handler context.

`LogImmediate` and `LogImmediateVA` bypass the deduplication tracking arrays entirely to output payload logs instantly.

`LogDedupped` and `LogDeduppedVA` route frames through the internal deduplication engine, filtering metrics according to active `setLogLevel()`, `setTimeThreshold()`, and `setLoggerInterface()` constraints.

## Quick Start

This section provides a simple example derived from the implementation test suites, demonstrating the use of both `LogImmediate` and `LogDedupped`.

```c
#include <sys/types.h>
#include <unistd.h>
#include <syslog.h>

#include "deduplog.h"

int main(int ac, char **av)
{
    (void) ac;

    // Only report LOG_ERR and more severe frames
    setLogLevel(LOG_ERR);

    // Output via printf(stdout)
    setLoggerInterface(LI_STDOUT);

    // Deduplicate identical occurrences within a 10 second window
    setTimeThreshold(10);

    // This message bypasses the deduplicator and always prints immediately
    LogImmediate(LOG_ERR, "Initializing program %s, pid %d\n", av[0], getpid());

    int max_iterations = 10;
    for (int i = 0; i < max_iterations; i++) {
        // Only the first message prints immediately. 
        // At thread shutdown or threshold window expiration, a single summary 
        // will log indicating the pattern occurred 10 times.
        LogDedupped(LOG_ERR, __LINE__, __FILE__, "Processing data loop entry index: %d\n", i);
    }
    return 0;
}
```

## Design Rationale

Throughout my career in systems engineering, log flooding has consistently proven to be a critical failure mode across multi-threaded applications. Excessively verbose logs degrade system performance and obscure the root cause of genuine operational emergencies. 

For instance, consider a carrier-grade quality-of-service engine mapping cellular tower IDs (eNodeBs) to geographic coordinates. During a major network upgrade, a provider might introduce thousands of unmapped elements without delivering an updated reference table. Under high traffic, this simple data delta triggers an un-throttled flood of:
```text
Network element %d not found.
```
This telemetry deluge quickly rolls over local disks and renders log streams unreadable. This library addresses that systemic issue by filtering duplicates directly at the generation source.

The architecture is built upon the following rigid systems constraints:

1. **Lock-Free Execution Paths:** To prevent logging calls from becoming multi-threaded synchronization bottlenecks, all message caching, counting, and timestamp validations are handled within `thread_local` allocation structures. The engine operates entirely lock-free during steady-state logging, reserving mutex utilization exclusively for rare boundaries like process-level thread registration, thread destruction, or system `openlog()` initialization.
2. **Strict Signal Safety Contexts:** Writing logs inside an asynchronous signal handler is heavily restricted; standard library targets like `printf()`, `fprintf()`, `syslog()`, or heap allocators are non-reentrant and will deadlock if a signal interrupts a thread holding an internal stream lock. `LogSafe` achieves reentrancy by utilizing the POSIX `writev()` system call to stitch pre-allocated vector string fragments together atomically, dropping the payload directly onto file descriptors formatted for compatibility with the `systemd` journal.
3. **Unified Interface Shifting:** The engine implements standard POSIX `syslog` severity hierarchies (`LOG_EMERG` down to `LOG_DEBUG`). It allows development teams to consume a singular, cohesive API while dynamically routing outputs across standard streams, legacy system log daemons, or structured modern systemd journals.
4. **Pattern-Level Aggregation:** Rather than consuming CPU cycles and memory allocating fully formatted strings, the deduplication engine computes tracking signatures using the static string literal pointer address (`const char *pattern`) alongside compilation attributes (`__FILE__` and `__LINE__`). This structure allows immediate, low-allocation cache lookups that eliminate string manipulation overhead entirely on repetitive duplicates.  Allocations are only done if the pattern has not currently been seen in that thread for that line and file.  If not found, there is an `std::unordered_map` insertion and a `std::string` to hold the pattern.

## Building

This software builds with Cmake.

To build, do the following:
* mkdir Build
* cd Build
* cmake ..
* make
* sudo make install

This will build the library, examples, and the master unit test suite.

## Special build targets
* Astyle
* Doxygen
* Cppcheck
* Asan
* Tsan
* Msan (CLang only)
* UBSan
* Coverage

### Astyle

Astyle is a code formatter.  Please use this style for all committed code.

* mkdir Build
* cd Build
* cmake ..
* make astyle

### Doxygen

Doxygen generates structural API source documentation, outputting assets into
a nested documentation subdirectory off the project root.

* mkdir Build
* cd Build
* cmake ..
* make doxygen

HTML pages (the recommended layout) can be accessed by pointing your viewer
at the documentation/html/index.html file. LaTeX and traditional manual pages
are also compiled. This target requires doxygen, graphviz, and optionally
mscgen installed on the host environment.

### Cppcheck

Cppcheck is a static analysis tool designed to flag compiler ambiguities and
deep code defects. Because it operates completely off source code analysis,
it does not execute any compiled binaries.

* mkdir Build
* cd Build
* cmake ..
* make cppcheck

Primary output is redirected to cppcheck.out in your working directory,
evaluating both core library code and example projects. Statistical analyzer
rule outputs map to cppcheck.checkers.

This step tracks thread safety bounds and Y2038 runtime compliance variables.
Note that MISRA checking is omitted for C++ files due to core framework
parsing issues handling template parameter commas inside explicit
suppressions.

### Asan

AddressSanitizer is a runtime code analyzer driven by heavy compiler
instrumentation executed during the unit testing phase. To prevent flag
pollution, you MUST clear out any legacy compilation state from the build
cache directory before initializing this pass.

* rm -rf Build
* mkdir Build
* cd Build
* cmake -DUSE_ASAN=on -B . ..
* make
* ./tests/deduplog_test

All allocation bounds infractions, leaks, and dangling pointer assertions
output directly to the active console terminal.

### Tsan

ThreadSanitizer tracks runtime data races and concurrent memory
synchronization infractions. To prevent flag pollution, you MUST clear out
any legacy compilation state from the build cache directory before
initializing this pass.

* rm -rf Build
* mkdir Build
* cd Build
* cmake -DUSE_TSAN=on -B . ..
* make
* ./tests/deduplog_test

All concurrency race detections print out directly over the active console
terminal.

### Msan (CLang only)

MemorySanitizer flags uninitialized memory reads. This checker requires a
Clang compiler toolchain. To prevent flag pollution, you MUST clear out any
legacy compilation state from the build cache directory before initializing
this pass.

* rm -rf Build
* mkdir Build
* cd Build
* cmake -DUSE_MSAN=on -B . ..
* make
* ./tests/deduplog_test

All tracking origins and uninitialized value accesses route straight to the
active console terminal.

### UBSan

UndefinedBehaviorSanitizer identifies runtime ambiguities like sign-bit
changes, array bounds slips, and integer overflows. To prevent flag
pollution, you MUST clear out any legacy compilation state from the build
cache directory before initializing this pass.

* rm -rf Build
* mkdir Build
* cd Build
* cmake -DUSE_UBSAN=on -B . ..
* make
* ./tests/deduplog_test

Violations drop diagnostic abort paths straight to the active console
terminal.

### Coverage

The coverage pipeline compiles an interactive graphical layout charting code execution metrics using `gcov` and `lcov`. Profiles reflect data harvested across the complete unit test suite and both example applications.

To guarantee perfect source line matching, optimizations are completely stripped from the build context (`-O0`). Note that while unit tests run instantly via mocked system dependencies, the standalone example binaries execute real-time loop rotations that intentionally run for a few minutes while generating high-volume logging footprints.

* rm -rf Build
* mkdir Build
* cd Build
* cmake -DUSE_COVERAGE=on ..
* make
* make coverage

Once the targets complete, load the compiled dashboard via a local web browser directed to `./coverage_html/index.html`.

