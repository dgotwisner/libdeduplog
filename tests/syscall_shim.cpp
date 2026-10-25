/*
 * Linker-wrapped libc functions for libdeduplog unit tests.
 */

#include "config.h"
#include "syscall_shim.h"

#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

extern "C" {
    time_t __real_time(time_t *tloc);
    ssize_t __real_write(int fd, const void *buf, size_t count);
    ssize_t __real_writev(int fd, const struct iovec *iov, int iovcnt);
    int __real_fflush(FILE *stream);
    void __real_openlog(const char *ident, int option, int facility);
    void __real_vsyslog(int priority, const char *format, va_list ap);
}

using VfprintfFn = int (*)(FILE *stream, const char *format, va_list ap);
using VfprintfChkFn = int (*)(FILE *stream, int flag, const char *format,
        va_list ap);

static VfprintfFn realVfprintf(void)
{
    static VfprintfFn fn = reinterpret_cast<VfprintfFn>(
            dlsym(RTLD_NEXT, "vfprintf"));
    return fn;
}

static VfprintfChkFn realVfprintfChk(void)
{
    static VfprintfChkFn fn = reinterpret_cast<VfprintfChkFn>(
            dlsym(RTLD_NEXT, "__vfprintf_chk"));
    return fn;
}

static int streamFd(FILE *stream)
{
    if (stream == NULL) {
        return -1;
    }

    return fileno(stream);
}

SyscallShim &SyscallShim::instance(void)
{
    static SyscallShim shim;
    return shim;
}

SyscallShim::SyscallShim(void):
    m_captureEnabled(false),
    m_fakeTime(1000000),
    m_openlogCallCount(0)
{
    installDefaultHooks();
}

void SyscallShim::installDefaultHooks(void)
{
    m_timeHook = [this](time_t *tloc) {
        return this->defaultTimeHook(tloc);
    };
    m_writeHook = [this](int fd, const void *buf, size_t count) {
        return this->defaultWriteHook(fd, buf, count);
    };
    m_writevHook = [this](int fd, const struct iovec * iov, int iovcnt) {
        return this->defaultWritevHook(fd, iov, iovcnt);
    };
    m_vfprintfHook = [this](FILE * stream, const char *format, va_list ap) {
        return this->defaultVfprintfHook(stream, format, ap);
    };
    m_vfprintfChkHook = [this](FILE * stream, int flag, const char *format,
    va_list ap) {
        return this->defaultVfprintfChkHook(stream, flag, format, ap);
    };
    m_fflushHook = [this](FILE * stream) {
        return this->defaultFflushHook(stream);
    };
    m_openlogHook = [this](const char *ident, int option, int facility) {
        this->defaultOpenlogHook(ident, option, facility);
    };
    m_vsyslogHook = [this](int priority, const char *format, va_list ap) {
        this->defaultVsyslogHook(priority, format, ap);
    };
}

void SyscallShim::reset(void)
{
    m_captureEnabled = false;
    m_fakeTime = 1000000;
    clearCaptures();
    installDefaultHooks();
}

void SyscallShim::setCaptureEnabled(bool enabled)
{
    m_captureEnabled = enabled;
}

bool SyscallShim::captureEnabled(void) const
{
    return m_captureEnabled;
}

void SyscallShim::setFakeTime(time_t now)
{
    m_fakeTime = now;
}

time_t SyscallShim::fakeTime(void) const
{
    return m_fakeTime;
}

void SyscallShim::advanceFakeTime(time_t delta)
{
    m_fakeTime += delta;
}

const std::string &SyscallShim::stdoutCapture(void) const
{
    return m_stdoutCapture;
}

const std::string &SyscallShim::stderrCapture(void) const
{
    return m_stderrCapture;
}

const std::vector<std::string> &SyscallShim::syslogCapture(void) const
{
    return m_syslogCapture;
}

int SyscallShim::openlogCallCount(void) const
{
    return m_openlogCallCount;
}

void SyscallShim::clearCaptures(void)
{
    m_stdoutCapture.clear();
    m_stderrCapture.clear();
    m_syslogCapture.clear();
    m_openlogCallCount = 0;
}

void SyscallShim::setTimeHook(TimeHookFn hook)
{
    m_timeHook = std::move(hook);
}

void SyscallShim::setWriteHook(WriteHookFn hook)
{
    m_writeHook = std::move(hook);
}

void SyscallShim::setWritevHook(WritevHookFn hook)
{
    m_writevHook = std::move(hook);
}

void SyscallShim::setVfprintfHook(VfprintfHookFn hook)
{
    m_vfprintfHook = std::move(hook);
}

void SyscallShim::setVfprintfChkHook(VfprintfChkHookFn hook)
{
    m_vfprintfChkHook = std::move(hook);
}

void SyscallShim::setFflushHook(FflushHookFn hook)
{
    m_fflushHook = std::move(hook);
}

void SyscallShim::setOpenlogHook(OpenlogHookFn hook)
{
    m_openlogHook = std::move(hook);
}

void SyscallShim::setVsyslogHook(VsyslogHookFn hook)
{
    m_vsyslogHook = std::move(hook);
}

void SyscallShim::recordWrite(int fd, const void *buf, size_t count)
{
    if (!m_captureEnabled || buf == NULL || count == 0) {
        return;
    }

    const std::string chunk(static_cast<const char *>(buf), count);

    if (fd == 1) {
        m_stdoutCapture += chunk;
    } else if (fd == 2) {
        m_stderrCapture += chunk;
    }
}

void SyscallShim::recordWritev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!m_captureEnabled || iov == NULL || iovcnt <= 0) {
        return;
    }

    for (int i = 0; i < iovcnt; i++) {
        recordWrite(fd, iov[i].iov_base, iov[i].iov_len);
    }
}

time_t SyscallShim::invokeTimeHook(time_t *tloc)
{
    return m_timeHook(tloc);
}

ssize_t SyscallShim::invokeWriteHook(int fd, const void *buf, size_t count)
{
    return m_writeHook(fd, buf, count);
}

ssize_t SyscallShim::invokeWritevHook(int fd, const struct iovec *iov,
    int iovcnt)
{
    return m_writevHook(fd, iov, iovcnt);
}

int SyscallShim::invokeVfprintfHook(FILE *stream, const char *format,
    va_list ap)
{
    return m_vfprintfHook(stream, format, ap);
}

int SyscallShim::invokeVfprintfChkHook(FILE *stream, int flag,
    const char *format, va_list ap)
{
    return m_vfprintfChkHook(stream, flag, format, ap);
}

int SyscallShim::invokeFflushHook(FILE *stream)
{
    return m_fflushHook(stream);
}

void SyscallShim::invokeOpenlogHook(const char *ident, int option,
    int facility)
{
    m_openlogHook(ident, option, facility);
}

void SyscallShim::invokeVsyslogHook(int priority, const char *format,
    va_list ap)
{
    m_vsyslogHook(priority, format, ap);
}

void SyscallShim::recordFormattedStream(FILE *stream, const char *format,
    va_list ap)
{
    if (!m_captureEnabled || format == NULL) {
        return;
    }

    va_list copy;
    va_copy(copy, ap);
    char buffer[8192];
    const int needed = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);

    if (needed <= 0) {
        return;
    }

    const size_t toCopy = static_cast<size_t>(needed) < sizeof(buffer) ?
        static_cast<size_t>(needed) : sizeof(buffer) - 1;
    const int fd = streamFd(stream);

    if (fd == 1) {
        m_stdoutCapture.append(buffer, toCopy);
    } else if (fd == 2) {
        m_stderrCapture.append(buffer, toCopy);
    }
}

time_t SyscallShim::defaultTimeHook(time_t *tloc)
{
    if (tloc != NULL) {
        *tloc = m_fakeTime;
    }

    return m_fakeTime;
}

ssize_t SyscallShim::defaultWriteHook(int fd, const void *buf,
    size_t count)
{
    recordWrite(fd, buf, count);

    if (m_captureEnabled && (fd == 1 || fd == 2)) {
        return static_cast<ssize_t>(count);
    }

    return __real_write(fd, buf, count);
}

ssize_t SyscallShim::defaultWritevHook(int fd, const struct iovec *iov,
    int iovcnt)
{
    recordWritev(fd, iov, iovcnt);

    if (m_captureEnabled && (fd == 1 || fd == 2)) {
        ssize_t total = 0;

        for (int i = 0; i < iovcnt; i++) {
            total += static_cast<ssize_t>(iov[i].iov_len);
        }

        return total;
    }

    return __real_writev(fd, iov, iovcnt);
}

int SyscallShim::defaultVfprintfHook(FILE *stream, const char *format,
    va_list ap)
{
    recordFormattedStream(stream, format, ap);

    if (m_captureEnabled) {
        va_list copy;
        va_copy(copy, ap);
        const int needed = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        return needed >= 0 ? needed : 0;
    }

    const VfprintfFn fn = realVfprintf();
    return fn != NULL ? fn(stream, format, ap) : 0;
}

int SyscallShim::defaultVfprintfChkHook(FILE *stream, int flag,
    const char *format, va_list ap)
{
    (void)flag;
    recordFormattedStream(stream, format, ap);

    if (m_captureEnabled) {
        va_list copy;
        va_copy(copy, ap);
        const int needed = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        return needed >= 0 ? needed : 0;
    }

    const VfprintfChkFn fn = realVfprintfChk();
    return fn != NULL ? fn(stream, flag, format, ap) : 0;
}

int SyscallShim::defaultFflushHook(FILE *stream)
{
    return __real_fflush(stream);
}

void SyscallShim::defaultOpenlogHook(const char *ident, int option,
    int facility)
{
    if (m_captureEnabled) {
        m_openlogCallCount++;
    }

    __real_openlog(ident, option, facility);
}

void SyscallShim::defaultVsyslogHook(int priority, const char *format,
    va_list ap)
{
    if (m_captureEnabled && format != NULL) {
        va_list copy;
        va_copy(copy, ap);
        char buffer[8192];
        const int needed = vsnprintf(buffer, sizeof(buffer), format, copy);
        va_end(copy);

        if (needed > 0) {
            const size_t toCopy = static_cast<size_t>(needed) < sizeof(buffer) ?
                static_cast<size_t>(needed) : sizeof(buffer) - 1;
            m_syslogCapture.emplace_back(buffer, toCopy);
        }
    }

    __real_vsyslog(priority, format, ap);
}

ScopedCapture::ScopedCapture(void)
{
    SyscallShim::instance().clearCaptures();
    SyscallShim::instance().setCaptureEnabled(true);
}

ScopedCapture::~ScopedCapture(void)
{
    SyscallShim::instance().setCaptureEnabled(false);
}

extern "C" time_t __wrap_time(time_t *tloc)
{
    return SyscallShim::instance().invokeTimeHook(tloc);
}

extern "C" ssize_t __wrap_write(int fd, const void *buf, size_t count)
{
    return SyscallShim::instance().invokeWriteHook(fd, buf, count);
}

extern "C" ssize_t __wrap_writev(int fd, const struct iovec *iov,
    int iovcnt)
{
    return SyscallShim::instance().invokeWritevHook(fd, iov, iovcnt);
}

extern "C" int __wrap___vfprintf_chk(FILE *stream, int flag,
    const char *format, va_list ap)
{
    return SyscallShim::instance().invokeVfprintfChkHook(stream, flag, format,
            ap);
}

extern "C" int __wrap_vfprintf(FILE *stream, const char *format,
    va_list ap)
{
    return SyscallShim::instance().invokeVfprintfHook(stream, format, ap);
}

extern "C" int __wrap_fflush(FILE *stream)
{
    return SyscallShim::instance().invokeFflushHook(stream);
}

extern "C" void __wrap_openlog(const char *ident, int option, int facility)
{
    SyscallShim::instance().invokeOpenlogHook(ident, option, facility);
}

extern "C" void __wrap_vsyslog(int priority, const char *format,
    va_list ap)
{
    SyscallShim::instance().invokeVsyslogHook(priority, format, ap);
}
extern "C" void __wrap___vsyslog_chk(int priority, int flag,
    const char *format, va_list ap)
{
    // Forward directly to your existing wrap hook logic or mock handler
    // e.g., if __wrap_vsyslog just invokes your Mock instance, do that here:
    (void)flag;
    __wrap_vsyslog(priority, format, ap);
}
