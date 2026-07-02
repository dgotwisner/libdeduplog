/*
 * Copyright (c) 2026
 *    Dave Gotwisner.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

/* Because this is included from C code, use the C header file not the C++
 * one.
 */
#ifndef HAVE_STDINT_H
    #error Missing required stdint.h
#endif // HAVE_STDINT_H
#include <stdint.h>
#ifdef HAVE_SYS_UIO_H
    #include <sys/uio.h>
#endif // HAVE_SYS_UIO_H

#include <stdarg.h>

#if defined(c_plusplus) || defined(__cplusplus)
extern "C" {
#endif

    /** Supported logging output interfaces
     */
    typedef enum LoggerInterface {
        LI_JOURNAL, /*!< Output to systemd-journald */
        LI_SYSLOG, /*!< Output to syslog */
        LI_STDERR, /*!< Output to stderr, formatted in a way compatible with systemd-journald */
        LI_STDOUT, /*!< Output to stdout, formatted in a way compatible with systemd-journald */
    } LoggerInterface_t;

    /** Return the current log level threshold to the caller
     *
     * This function will return the current configured log level (LOG_DEBUG, by
     * default, and otherwise what the last call to setLogLevel() passed.
     *
     * Log messages at a lower priority than this (a larger number) will be
     * filtered out and not sent upstream.
     *
     * @returns The log level, based upon /usr/include/sys/syslog.h's
     * definitions.
     */
    extern int getLogLevel(void);

    /** Set the new maximum log level to report.
     *
     * This function allows the log level to change for all threads, and can
     * be queried via getLogLevel().  Any log messages at a lower priority (a
     * higher number) will be dropped on output.
     *
     * @param [in] level The requested log level to use.
     *
     * @retval 1 Call succeded
     * @retval 0 Call failed, \b level wasn't a valid log level.
     */
    extern int setLogLevel(int level);

    /** Get the number of seconds of data to collapse for duplicate removal
     *
     * This function returns the current value for this setting, as configured
     * by default (1 hour) or via setTimeThreshold().
     *
     * @returns The current time threshold in place.
     */
    extern uint64_t getTimeThreshold(void);

    /** Set the new time threshold for duplicate filtering, in seconds.
     *
     * This function will change the time threshold used.
     *
     * @param [in] threshold The number of seconds to use for combining/dropping
     *                       duplicates.
     *
     * @retval 1  Call succeeded
     * @retval 0 Call failed.  Typically, this is either because \b
     *                             threshold was zero, or it was more than an
     *                             hour.
     */
    extern int setTimeThreshold(uint64_t threshold);

    /** Get which supported output interface is used.
     *
     * This returns the current configured interface for output.  Default is
     * stdout, but can be changed via setLoggerInterface().
     *
     * @returns  The current interface in place.
     */
    extern LoggerInterface_t getLoggerInterface(void);

    /** Set the new logger interface.
     *
     * This allows the caller to change which interface path is used for output.
     *
     * @param [in] interface Interface to use
     *
     * @retval 1  Operation succeeded.
     * @retval 0 Operation failed.  This usually is caused by an invalid
     *                                  value for the \b interface parameter.
     */
    extern int setLoggerInterface(LoggerInterface_t interface);

    /** Log output from signal handlers,where various safety rools apply
     *
     * This function should only be used in a signal handler, and is very
     * limited in what it does.  It basically combines the pre-formatted strings
     * into a single message, prepending <N> and Level, where N is the log level
     * and Level is the textual representation.  This works for STDERR, STDOUT,
     * and JOURNAL.  It does not work for Syslog, as there is no easy safe way
     * to support it - although these days, native syslog support should be
     * failry rare.
     *
     * Any log level where \b level is greater than the max log level currently
     * in place will be silently dropped.  This function does NOT use duplicate
     * dropping.
     *
     * @param [in] level  Syslog log level for the message
     * @param [in] iov    A vector (read writev(2) documentation) of output
     *                    fragments.
     * @param [in] iovcnt The number of fragments in \b iov.
     */
    extern void LogSafe(int level, const struct iovec *iov, int iovcnt);

    /** Log output immediately, bypassing the dedup logic
     *
     * This function should be called if you always want to see the message
     * (other than log level constraints).  It is simply output when called.
     * All logging interfaces are supported.
     *
     * The log level string is inserted in the message, and if stdout or stderr,
     * the \b <N> is also displayed.
     *
     * Most of the time, you will want to dedup, but for critical messages, or
     * for one-shot messages (such as output mapping thread id's to thread
     * names), you will want to use this function.
     *
     * @param [in] level    Log level used
     * @param [in] pattern  The format pattern used for output
     * @param [in] ...      Any arguments to pattern.
     *
     */
    extern void LogImmediate(int level, const char *pattern,
        ...) __attribute__((format(printf, 2, 3)));

    /** Log output immediately, bypassing the dedup logic with va_list input
     *
     * This function should be called if you always want to see the message
     * (other than log level constraints).  It is simply output when called.
     * All logging interfaces are supported.
     *
     * The log level string is inserted in the message, and if stdout or stderr,
     * the \b <N> is also displayed.
     *
     * Most of the time, you will want to dedup, but for critical messages, or
     * for one-shot messages (such as output mapping thread id's to thread
     * names), you will want to use this function.
     *
     * @param [in] level    Log level used
     * @param [in] pattern  The format pattern used for output
     * @param [in] ap       Va_list for the arguments to format
     *
     */
    extern void LogImmediateVA(int level, const char *pattern, va_list ap);

    /** Log output, throwing out (and couunting) duplicates, based upon a pattern.
     *
     * This function should be used for most logging.  It is designed to prevent
     * log flooding of messages based upon threads, log level, source line,
     * file, and based upon the pattern (without any of the data going into that
     * pattern), so that a LogDedupped(LOG_ERR, 10, "foo.cpp", "Unable to open
     * file %s: %s\n", filename, strerror(errno)) will constrain all failures,
     * independent of the file being opened or the errorno value.
     *
     * @param [in] level      Log level used
     * @param [in] lineNumber Line number from the source code.  In C++, this
     *                        should be __LINE__.
     * @param [in] filename   Filename from the source code.  In C++, this
     *                        should be __FILE__.
     * @param [in] pattern    The printf() style format pattern to use.
     * @param [in] ...      Any arguments to pattern.
     *
     */
    extern void LogDedupped(int level, int lineNumber, const char *filename,
        const char *pattern, ...) __attribute__((format(printf, 4, 5)));

    /** Log output, throwing out (and couunting) duplicates, based upon a pattern with va_list input
     *
     * This function should be used for most logging.  It is designed to prevent
     * log flooding of messages based upon threads, log level, source line,
     * file, and based upon the pattern (without any of the data going into that
     * pattern), so that a LogDedupped(LOG_ERR, 10, "foo.cpp", "Unable to open
     * file %s: %s\n", filename, strerror(errno)) will constrain all failures,
     * independent of the file being opened or the errorno value.
     *
     * @param [in] level      Log level used
     * @param [in] lineNumber Line number from the source code.  In C++, this
     *                        should be __LINE__.
     * @param [in] filename   Filename from the source code.  In C++, this
     *                        should be __FILE__.
     * @param [in] pattern    The printf() style format pattern to use.
     * @param [in] ap       Va_list for the arguments to format
     *
     */
    extern void LogDeduppedVA(int level, int lineNumber, const char *filename,
        const char *pattern, va_list ap);

#if defined(c_plusplus) || defined(__cplusplus)
}
#endif
