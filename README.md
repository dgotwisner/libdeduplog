# Deduplicating Logger with Signal Handler safe logging API

## Overview

Deduplog is a logger capable of using any of systemd-journald,
syslog, or fprintf(stderr or stdout) to log data to the output.

It operates with syslog log levels, since that's what systemd-journald uses
as well.

It allows for disabling log output for data under a certain level (configurable
 via an API), and to throw out duplicate messages on a per-thread level (at the
pattern level, not the actual formatted string), based upon function, line,
and file.  The maximum time range to discard is also configurable via an
API.

This is written in C++, but with a C compatible API, so any program that can
bind to C programs and allows varargs functions can use it, they must simply
link in the libstdc++ library to use it.  For C++, it uses C++11, so it
should work with almost any C++ program.  For C, a specific language
specification isn't required, so we support the minimal common version for C
code calling the C++ code.

The normal logger is not safe for using in a signal handler, but a more
limited function is available for use in the signal handler.

Log level threshold is changable at any point in time.

Refer to the doxygen documentation for more detailed documentation.

The build environment has currently been proven on CentOS 7 (I know it's old,
it's my main system), and on Ubuntu Desktop 26.04 LTS running on a Windows
 computer in an Oracle VirtualBox VM.

## Required Includes

* syslog.h for syslog output
* systemd/sd-journal.h for systemd-journald output
* stdio.h (or cstdio) for stdout/stderr support.
* sys/uio.h for the signal handler safe writev() API.

If syslog or systemd-journald aren't available, those output paths are disabled.

If sys/uio.h isn't available, the code calls write() in a loop, so it will be
slower and more likely to intersperse output from other running threads.

## APIs

Detailed documentation for the enums and APIs can be found by generating the
Doxygen output.

`
typedef enum LoggerInterface
{
    LI_JOURNAL, /*!< Output to systemd-journald */
    LI_SYSLOG, /*!< Output to syslog */
    LI_STDERR, /*!< Output to stderr, formatted in a way compatible with systemd-journald */
    LI_STDOUT, /*!< Output to stdout, formatted in a way compatible with systemd-journald */
} LoggerInterface_t;
`

* int getLogLevel(void);
* int setLogLevel(int level);
* uint64_t getTimeThreshold(void);
* int setTimeThreshold(uint64_t threshold);
* LoggerInterface_t getLoggerInterface(void);
* int setLoggerInterface(LoggerInterface_t interface);
* void LogSafe(int level, const struct iovec *iov, int iovcnt);
* void LogImmediate(int level, const char *pattern, ...);
* void LogImmediateVA(int level, const char *pattern, va_list ap);
* void LogDedupped(int level, int lineNumber, const char *filename, const char *pattern, ...);
* void LogDeduppedVA(int level, int lineNumber, const char *filename, const char *pattern,va_list ap);

Note, "bool" data type isn't used because of the lowest common denominator
of C, not supporting it.

## Building

This software builds with autoconf and automake.

To build, do the following:
* ./configure --prefix=/usr/local
* make
* sudo 4make install

## Running astyle target

* ./configure --prefix=/usr/local
* make astyle

## Running Doxygen target

* ./configure --prefix=/usr/local
* make doxygen

Output is placed in a documentation directory, so for the HTML output, you
can point your browser to documentation/html/index.html.

This requires doxygen, graphviz, and possibly mscgen to be installed.

## Running Cppcheck target

* ./configure --prefix=/usr/local
* make cppcheck

Output is placed in cppcheck.out.  Additional output will be in
cppcheck.checkers.  This will currently generate defects.  The resolvable
defects will be fixed in the future.

This does threadsafety, y2038 checks.  It does not do misra checking of the
C++ code because the misra addon can't correctly deal with commas inside of
templates, and the cppcheck-suppress comments can't disable it.

## Reworking after changing configure.ac or Makefile.am

Before rebuilding, you must do the following:

* autoreconf -fvi

This particular step has only been tested on CentOS 7.

## Building the examples

Building will also build the example programs.

To run the examples, change directory to the example directory
and then run either of the generated binaries - test is C++, ctest is C.

## Building a distribution tarball

After the autoreconf and ./configure stuff, if needed, run:

* make dist

And take the tar.gz file it produces.

# Building from the extracted tarball

* ./configure --prefix=/usr/local
* make
* cd example && make
