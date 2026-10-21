/*
 * Test-only syscall shim with gmock hooks for libdeduplog unit tests.
 */

#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifdef HAVE_SYS_UIO_H
    #include <sys/uio.h>
#endif // HAVE_SYS_UIO_H

using TimeHookFn = std::function<time_t(time_t *tloc)>;
using WriteHookFn =
    std::function<ssize_t(int fd, const void *buf, size_t count)>;
using WritevHookFn =
    std::function<ssize_t(int fd, const struct iovec *iov, int iovcnt)>;
using VfprintfHookFn =
    std::function<int(FILE *stream, const char *format, va_list ap)>;
using VfprintfChkHookFn =
    std::function<int(FILE *stream, int flag, const char *format, va_list ap)>;
using FflushHookFn = std::function<int(FILE *stream)>;
using OpenlogHookFn =
    std::function<void(const char *ident, int option, int facility)>;
using VsyslogHookFn =
    std::function<void(int priority, const char *format, va_list ap)>;

/** Controls wrapped libc calls used by deduplog.cpp during tests. */
class SyscallShim {
    public:
        static SyscallShim &instance(void);

        void reset(void);
        void setCaptureEnabled(bool enabled);
        bool captureEnabled(void) const;

        void setFakeTime(time_t now);
        time_t fakeTime(void) const;
        void advanceFakeTime(time_t delta);

        const std::string &stdoutCapture(void) const;
        const std::string &stderrCapture(void) const;
        const std::vector<std::string> &syslogCapture(void) const;
        int openlogCallCount(void) const;

        void clearCaptures(void);

        void setTimeHook(TimeHookFn hook);
        void setWriteHook(WriteHookFn hook);
        void setWritevHook(WritevHookFn hook);
        void setVfprintfHook(VfprintfHookFn hook);
        void setVfprintfChkHook(VfprintfChkHookFn hook);
        void setFflushHook(FflushHookFn hook);
        void setOpenlogHook(OpenlogHookFn hook);
        void setVsyslogHook(VsyslogHookFn hook);

        time_t defaultTimeHook(time_t *tloc);
        ssize_t defaultWriteHook(int fd, const void *buf, size_t count);
        ssize_t defaultWritevHook(int fd, const struct iovec *iov, int iovcnt);
        int defaultVfprintfHook(FILE *stream, const char *format, va_list ap);
        int defaultVfprintfChkHook(FILE *stream, int flag, const char *format,
            va_list ap);
        int defaultFflushHook(FILE *stream);
        void defaultOpenlogHook(const char *ident, int option, int facility);
        void defaultVsyslogHook(int priority, const char *format, va_list ap);

        time_t invokeTimeHook(time_t *tloc);
        ssize_t invokeWriteHook(int fd, const void *buf, size_t count);
        ssize_t invokeWritevHook(int fd, const struct iovec *iov, int iovcnt);
        int invokeVfprintfHook(FILE *stream, const char *format, va_list ap);
        int invokeVfprintfChkHook(FILE *stream, int flag, const char *format,
            va_list ap);
        int invokeFflushHook(FILE *stream);
        void invokeOpenlogHook(const char *ident, int option, int facility);
        void invokeVsyslogHook(int priority, const char *format, va_list ap);

    private:
        SyscallShim(void);
        void installDefaultHooks(void);
        void recordWrite(int fd, const void *buf, size_t count);
        void recordWritev(int fd, const struct iovec *iov, int iovcnt);
        void recordFormattedStream(FILE *stream, const char *format, va_list ap);

        bool m_captureEnabled;
        time_t m_fakeTime;
        std::string m_stdoutCapture;
        std::string m_stderrCapture;
        std::vector<std::string> m_syslogCapture;
        int m_openlogCallCount;

        TimeHookFn m_timeHook;
        WriteHookFn m_writeHook;
        WritevHookFn m_writevHook;
        VfprintfHookFn m_vfprintfHook;
        VfprintfChkHookFn m_vfprintfChkHook;
        FflushHookFn m_fflushHook;
        OpenlogHookFn m_openlogHook;
        VsyslogHookFn m_vsyslogHook;
};

/** Enable output capture for the duration of a test statement block. */
class ScopedCapture {
    public:
        ScopedCapture(void);
        ~ScopedCapture(void);
};
