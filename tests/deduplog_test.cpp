/*
 * Google Test / Google Mock coverage for libdeduplog.
 */

#include "config.h"

#include <cstdarg>
#include <cstring>
#include <string>
#include <thread>

#ifdef HAVE_SYSLOG_H
    #include <syslog.h>
#endif // HAVE_SYSLOG_H
#ifdef HAVE_SYS_UIO_H
    #include <sys/uio.h>
#endif // HAVE_SYS_UIO_H

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "deduplog.h"
#include "syscall_shim.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::Return;

namespace {

    constexpr const char *kTestFileA = "tests/file_a.cpp";
    constexpr const char *kTestFileB = "tests/file_b.cpp";

    void resetPublicConfig(void)
    {
        ASSERT_EQ(1, setLogLevel(LOG_DEBUG));
        ASSERT_EQ(1, setTimeThreshold(60));
        ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
        SyscallShim::instance().setFakeTime(1000000);
    }

    bool canUseSyslogInterface(void)
    {
        return setLoggerInterface(LI_SYSLOG) == 1;
    }

    bool canUseJournalInterface(void)
    {
        return setLoggerInterface(LI_JOURNAL) == 1;
    }

    class DedupLogTest : public ::testing::Test {
        protected:
            void SetUp(void) override {
                SyscallShim::instance().reset();
                resetPublicConfig();
            }
    };

    class DedupLogCaptureTest : public DedupLogTest {
        protected:
            void SetUp(void) override {
                DedupLogTest::SetUp();
                SyscallShim::instance().clearCaptures();
                SyscallShim::instance().setCaptureEnabled(true);
            }

            void TearDown(void) override {
                SyscallShim::instance().reset();
            }
    };

} // namespace

// ====================== Configuration API ======================

TEST_F(DedupLogTest, GetSetLogLevelAcceptsValidRange)
{
    EXPECT_EQ(LOG_DEBUG, getLogLevel());

    for (int level = LOG_EMERG; level <= LOG_DEBUG; level++) {
        EXPECT_EQ(1, setLogLevel(level));
        EXPECT_EQ(level, getLogLevel());
    }
}

TEST_F(DedupLogTest, SetLogLevelRejectsOutOfRangeValues)
{
    EXPECT_EQ(0, setLogLevel(LOG_EMERG - 1));
    EXPECT_EQ(LOG_DEBUG, getLogLevel());
    EXPECT_EQ(0, setLogLevel(LOG_DEBUG + 1));
    EXPECT_EQ(LOG_DEBUG, getLogLevel());
}

TEST_F(DedupLogTest, GetSetTimeThresholdEnforcesBounds)
{
    EXPECT_EQ(60U, getTimeThreshold());
    EXPECT_EQ(0, setTimeThreshold(0));
    EXPECT_EQ(60U, getTimeThreshold());
    EXPECT_EQ(0, setTimeThreshold(60U * 60U + 1U));
    EXPECT_EQ(60U, getTimeThreshold());
    EXPECT_EQ(1, setTimeThreshold(1));
    EXPECT_EQ(1U, getTimeThreshold());
    EXPECT_EQ(1, setTimeThreshold(60U * 60U));
    EXPECT_EQ(60U * 60U, getTimeThreshold());
}

TEST_F(DedupLogTest, GetSetLoggerInterfaceSupportsStdoutAndStderr)
{
    EXPECT_EQ(LI_STDOUT, getLoggerInterface());
    EXPECT_EQ(1, setLoggerInterface(LI_STDERR));
    EXPECT_EQ(LI_STDERR, getLoggerInterface());
    EXPECT_EQ(1, setLoggerInterface(LI_STDOUT));
    EXPECT_EQ(LI_STDOUT, getLoggerInterface());
}

TEST_F(DedupLogTest, SetLoggerInterfaceRejectsInvalidValue)
{
    const LoggerInterface_t invalid = static_cast<LoggerInterface_t>(999);
    EXPECT_EQ(0, setLoggerInterface(invalid));
    EXPECT_EQ(LI_STDOUT, getLoggerInterface());
}

TEST_F(DedupLogTest, SetLoggerInterfaceOptionalBackends)
{
    if (canUseJournalInterface()) {
        EXPECT_EQ(LI_JOURNAL, getLoggerInterface());
    } else {
        EXPECT_EQ(0, setLoggerInterface(LI_JOURNAL));
    }

    resetPublicConfig();

    if (canUseSyslogInterface()) {
        EXPECT_EQ(LI_SYSLOG, getLoggerInterface());
    } else {
        EXPECT_EQ(0, setLoggerInterface(LI_SYSLOG));
        EXPECT_EQ(LI_STDOUT, getLoggerInterface());
    }
}

// ====================== LogImmediate ======================

TEST_F(DedupLogCaptureTest, LogImmediateWritesFormattedStdoutMessage)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogImmediate(LOG_ERR, "widget failed: %s (%d)\n", "disk", 42);
    EXPECT_THAT(SyscallShim::instance().stdoutCapture(),
        ::testing::HasSubstr("<3> Error widget failed: disk (42)"));
}

static void logImmediateVaHelper(int level, const char *pattern, ...)
{
    va_list ap;
    va_start(ap, pattern);
    LogImmediateVA(level, pattern, ap);
    va_end(ap);
}

TEST_F(DedupLogCaptureTest, LogImmediateVARoutesThroughSamePath)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    logImmediateVaHelper(LOG_INFO, "value=%d\n", 7);
    EXPECT_THAT(SyscallShim::instance().stdoutCapture(),
        ::testing::HasSubstr("<6> Info value=7"));
}

TEST_F(DedupLogCaptureTest, LogImmediateFiltersByConfiguredLogLevel)
{
    ASSERT_EQ(1, setLogLevel(LOG_WARNING));
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogImmediate(LOG_INFO, "should not appear\n");
    LogImmediate(LOG_WARNING, "should appear\n");
    EXPECT_EQ(std::string::npos,
        SyscallShim::instance().stdoutCapture().find("should not appear"));
    EXPECT_THAT(SyscallShim::instance().stdoutCapture(),
        ::testing::HasSubstr("should appear"));
}

TEST_F(DedupLogCaptureTest, LogImmediateDropsNegativeLevels)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogImmediate(-1, "negative level\n");
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
}

TEST_F(DedupLogCaptureTest, LogImmediateUsesStderrInterface)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDERR));
    LogImmediate(LOG_CRIT, "critical path\n");
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
    EXPECT_THAT(SyscallShim::instance().stderrCapture(),
        ::testing::HasSubstr("<2> Critical critical path"));
}

TEST_F(DedupLogCaptureTest, LogImmediateUsesMockedVfprintfHook)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    testing::MockFunction<int(FILE *, int, const char *, va_list)>
    vfprintfChkMock;
    shim.setVfprintfChkHook([&shim, &vfprintfChkMock](FILE * stream, int flag,
    const char *format, va_list ap) {
        return vfprintfChkMock.Call(stream, flag, format, ap);
    });
    EXPECT_CALL(vfprintfChkMock, Call(testing::NotNull(), testing::_,
        testing::NotNull(), testing::_))
    .Times(1)
    .WillOnce(testing::Invoke(
    [&shim](FILE * stream, int flag, const char *format, va_list ap) {
        return shim.defaultVfprintfChkHook(stream, flag, format, ap);
    }));
    LogImmediate(LOG_NOTICE, "mocked fprintf path\n");
    EXPECT_THAT(shim.stdoutCapture(),
        ::testing::HasSubstr("mocked fprintf path"));
}

#ifdef HAVE_SYSLOG_H
TEST_F(DedupLogCaptureTest, LogImmediateUsesSyslogWhenConfigured)
{
    if (!canUseSyslogInterface()) {
        GTEST_SKIP() << "LI_SYSLOG is not accepted by this build configuration";
    }

    auto &shim = SyscallShim::instance();
    testing::MockFunction<void(const char *, int, int)> openlogMock;
    testing::MockFunction<void(int, const char *, va_list)> vsyslogMock;
    shim.setOpenlogHook([&openlogMock](const char *ident, int option,
    int facility) {
        openlogMock.Call(ident, option, facility);
    });
    shim.setVsyslogHook([&shim, &vsyslogMock](int priority, const char *format,
    va_list ap) {
        return vsyslogMock.Call(priority, format, ap);
    });
    EXPECT_CALL(openlogMock, Call(_, _, _)).Times(AtLeast(1));
    EXPECT_CALL(vsyslogMock, Call(LOG_ERR, testing::NotNull(), _))
    .Times(1)
    .WillOnce(testing::Invoke(&shim, &SyscallShim::defaultVsyslogHook));
    LogImmediate(LOG_ERR, "syslog immediate %d\n", 9);
    ASSERT_EQ(1U, shim.syslogCapture().size());
    EXPECT_THAT(shim.syslogCapture().front(),
        ::testing::HasSubstr("<3> Error syslog immediate 9"));
}
#endif // HAVE_SYSLOG_H

// ====================== LogSafe ======================

TEST_F(DedupLogCaptureTest, LogSafeConcatenatesIovecFragmentsToStdout)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    struct iovec iov[3];
    iov[0].iov_base = const_cast<char *>("part-a ");
    iov[0].iov_len = strlen("part-a ");
    iov[1].iov_base = const_cast<char *>("part-b ");
    iov[1].iov_len = strlen("part-b ");
    iov[2].iov_base = const_cast<char *>("part-c\n");
    iov[2].iov_len = strlen("part-c\n");
    LogSafe(LOG_WARNING, iov, 3);
    EXPECT_THAT(SyscallShim::instance().stdoutCapture(),
        ::testing::HasSubstr("<4> Warning part-a part-b part-c"));
}

TEST_F(DedupLogCaptureTest, LogSafeJournalAliasWritesToStderrFd)
{
    if (!canUseJournalInterface()) {
        GTEST_SKIP() << "LI_JOURNAL is not accepted by this build configuration";
    }

    struct iovec iov[1];

    iov[0].iov_base = const_cast<char *>("journal alias\n");
    iov[0].iov_len = strlen("journal alias\n");
    LogSafe(LOG_INFO, iov, 1);
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
    EXPECT_THAT(SyscallShim::instance().stderrCapture(),
        ::testing::HasSubstr("<6> Info journal alias"));
}

TEST_F(DedupLogCaptureTest, LogSafeFiltersByLogLevel)
{
    ASSERT_EQ(1, setLogLevel(LOG_ERR));
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    struct iovec iov[1];
    iov[0].iov_base = const_cast<char *>("filtered\n");
    iov[0].iov_len = strlen("filtered\n");
    LogSafe(LOG_INFO, iov, 1);
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
}

TEST_F(DedupLogCaptureTest, LogSafeReturnsWithoutOutputForSyslogInterface)
{
    if (!canUseSyslogInterface()) {
        GTEST_SKIP() << "LI_SYSLOG is not accepted by this build configuration";
    }

    auto &shim = SyscallShim::instance();
    testing::MockFunction<ssize_t(int, const void *, size_t)> writeMock;
    testing::MockFunction<ssize_t(int, const struct iovec *, int)> writevMock;
    shim.setWriteHook([&writeMock](int fd, const void *buf, size_t count) {
        return writeMock.Call(fd, buf, count);
    });
    shim.setWritevHook([&writevMock](int fd, const struct iovec * iov,
    int iovcnt) {
        return writevMock.Call(fd, iov, iovcnt);
    });
    EXPECT_CALL(writeMock, Call(_, _, _)).Times(0);
    EXPECT_CALL(writevMock, Call(_, _, _)).Times(0);
    struct iovec iov[1];
    iov[0].iov_base = const_cast<char *>("must not emit\n");
    iov[0].iov_len = strlen("must not emit\n");
    LogSafe(LOG_ERR, iov, 1);
    EXPECT_TRUE(shim.stdoutCapture().empty());
    EXPECT_TRUE(shim.stderrCapture().empty());
}

TEST_F(DedupLogCaptureTest, LogSafeUsesMockedWritevHook)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setLoggerInterface(LI_STDERR));
    struct iovec iov[1];
    iov[0].iov_base = const_cast<char *>("writev mock\n");
    iov[0].iov_len = strlen("writev mock\n");
    testing::MockFunction<ssize_t(int, const struct iovec *, int)> writevMock;
    shim.setWritevHook([&shim, &writevMock](int fd, const struct iovec * vec,
    int iovcnt) {
        return writevMock.Call(fd, vec, iovcnt);
    });
    EXPECT_CALL(writevMock, Call(2, testing::NotNull(), 3))
    .Times(1)
    .WillOnce(testing::Invoke(&shim, &SyscallShim::defaultWritevHook));
    LogSafe(LOG_DEBUG, iov, 1);
    EXPECT_THAT(shim.stderrCapture(), ::testing::HasSubstr("writev mock"));
}

// ====================== LogDedupped ======================

TEST_F(DedupLogCaptureTest, LogDeduppedEmitsFirstOccurrenceOnly)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_ERR, 101, kTestFileA, "duplicate probe %d\n", 1);
    LogDedupped(LOG_ERR, 101, kTestFileA, "duplicate probe %d\n", 2);
    const std::string &out = SyscallShim::instance().stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("duplicate probe 1"));
    EXPECT_EQ(std::string::npos, out.find("duplicate probe 2"));
}

TEST_F(DedupLogCaptureTest, LogDeduppedTreatsDifferentLinesSeparately)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_ERR, 201, kTestFileA, "line-a\n");
    LogDedupped(LOG_ERR, 202, kTestFileA, "line-b\n");
    const std::string &out = SyscallShim::instance().stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("line-a"));
    EXPECT_THAT(out, ::testing::HasSubstr("line-b"));
}

TEST_F(DedupLogCaptureTest, LogDeduppedTreatsDifferentFilesSeparately)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_ERR, 301, kTestFileA, "file-a\n");
    LogDedupped(LOG_ERR, 301, kTestFileB, "file-b\n");
    const std::string &out = SyscallShim::instance().stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("file-a"));
    EXPECT_THAT(out, ::testing::HasSubstr("file-b"));
}

TEST_F(DedupLogCaptureTest, LogDeduppedTreatsDifferentLevelsSeparately)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_ERR, 401, kTestFileA, "level-err\n");
    LogDedupped(LOG_WARNING, 401, kTestFileA, "level-warn\n");
    const std::string &out = SyscallShim::instance().stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("level-err"));
    EXPECT_THAT(out, ::testing::HasSubstr("level-warn"));
}

static void logDeduppedVaHelper(int level, int lineNumber,
    const char *filename,
    const char *pattern, ...)
{
    va_list ap;
    va_start(ap, pattern);
    LogDeduppedVA(level, lineNumber, filename, pattern, ap);
    va_end(ap);
}

TEST_F(DedupLogCaptureTest, LogDeduppedVARoutesThroughSameDedupLogic)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    logDeduppedVaHelper(LOG_ERR, 501, kTestFileA, "va-path %s\n", "first");
    LogDedupped(LOG_ERR, 501, kTestFileA, "va-path %s\n", "second");
    const std::string &out = SyscallShim::instance().stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("va-path first"));
    EXPECT_EQ(std::string::npos, out.find("va-path second"));
}

TEST_F(DedupLogCaptureTest, LogDeduppedFiltersByConfiguredLogLevel)
{
    ASSERT_EQ(1, setLogLevel(LOG_ERR));
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_INFO, 601, kTestFileA, "filtered dedup\n");
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
}

TEST_F(DedupLogCaptureTest, LogDeduppedEmitsCountSummaryAfterThreshold)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setTimeThreshold(10));
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    LogDedupped(LOG_ERR, 701, kTestFileA, "thresholded %d\n", 0);
    LogDedupped(LOG_ERR, 701, kTestFileA, "thresholded %d\n", 1);
    shim.advanceFakeTime(11);
    LogDedupped(LOG_ERR, 701, kTestFileA, "thresholded %d\n", 2);
    const std::string &out = shim.stdoutCapture();
    EXPECT_THAT(out, ::testing::HasSubstr("thresholded 0"));
    EXPECT_EQ(std::string::npos, out.find("thresholded 1"));
    EXPECT_EQ(std::string::npos, out.find("thresholded 2"));
    EXPECT_THAT(out, ::testing::HasSubstr("occurred 3 times in 11 seconds"));
    EXPECT_THAT(out, ::testing::HasSubstr("tests/file_a.cpp, line 701"));
}

TEST_F(DedupLogCaptureTest, LogSafeDropsNegativeLevels)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    struct iovec iov[1];
    iov[0].iov_base = const_cast<char *>("negative\n");
    iov[0].iov_len = strlen("negative\n");
    LogSafe(-1, iov, 1);
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
}

TEST_F(DedupLogCaptureTest, LogDeduppedDropsNegativeLevels)
{
    LogDedupped(-1, 651, kTestFileA, "negative dedup\n");
    EXPECT_TRUE(SyscallShim::instance().stdoutCapture().empty());
}

TEST_F(DedupLogCaptureTest,
    LogDeduppedCountSummaryUsesStderrWithLevelPrefix)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setTimeThreshold(5));
    ASSERT_EQ(1, setLoggerInterface(LI_STDERR));
    LogDedupped(LOG_WARNING, 801, kTestFileA, "stderr summary\n");
    LogDedupped(LOG_WARNING, 801, kTestFileA, "stderr summary\n");
    shim.advanceFakeTime(6);
    LogDedupped(LOG_WARNING, 801, kTestFileA, "stderr summary\n");
    const std::string &err = shim.stderrCapture();
    EXPECT_THAT(err, ::testing::HasSubstr("<4> Warning"));
    EXPECT_THAT(err, ::testing::HasSubstr("The following message pattern"));
    EXPECT_THAT(err, ::testing::HasSubstr("occurred 3 times in 6 seconds"));
}

#ifdef HAVE_SYSLOG_H
TEST_F(DedupLogCaptureTest, LogDeduppedUsesSyslogForFirstMessage)
{
    if (!canUseSyslogInterface()) {
        GTEST_SKIP() << "LI_SYSLOG is not accepted by this build configuration";
    }

    auto &shim = SyscallShim::instance();
    testing::MockFunction<void(int, const char *, va_list)> vsyslogMock;
    shim.setVsyslogHook([&shim, &vsyslogMock](int priority, const char *format,
    va_list ap) {
        return vsyslogMock.Call(priority, format, ap);
    });
    EXPECT_CALL(vsyslogMock, Call(LOG_ERR, testing::NotNull(), _))
    .Times(AtLeast(1))
    .WillRepeatedly(testing::Invoke(&shim, &SyscallShim::defaultVsyslogHook));
    LogDedupped(LOG_ERR, 901, kTestFileA, "syslog dedup %d\n", 5);
    ASSERT_FALSE(shim.syslogCapture().empty());
    EXPECT_THAT(shim.syslogCapture().front(),
        ::testing::HasSubstr("syslog dedup 5"));
}
#endif // HAVE_SYSLOG_H

TEST_F(DedupLogCaptureTest, LogDeduppedUsesMockedTimeHookForThreshold)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setTimeThreshold(2));
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    testing::MockFunction<time_t(time_t *)> timeMock;
    shim.setTimeHook([&shim, &timeMock](time_t *tloc) {
        return timeMock.Call(tloc);
    });
    EXPECT_CALL(timeMock, Call(_))
    .Times(testing::AtLeast(3))
    .WillRepeatedly(testing::Invoke(&shim, &SyscallShim::defaultTimeHook));
    LogDedupped(LOG_ERR, 1001, kTestFileA, "timed %d\n", 1);
    LogDedupped(LOG_ERR, 1001, kTestFileA, "timed %d\n", 2);
    shim.advanceFakeTime(3);
    LogDedupped(LOG_ERR, 1001, kTestFileA, "timed %d\n", 3);
    EXPECT_THAT(shim.stdoutCapture(),
        ::testing::HasSubstr("occurred 3 times in 3 seconds"));
}

// ====================== Thread shutdown path ======================

TEST_F(DedupLogCaptureTest, ThreadShutdownFlushesPendingDuplicateCounts)
{
    auto &shim = SyscallShim::instance();
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    std::thread worker([]() {
        LogDedupped(LOG_ERR, 1101, kTestFileA, "worker duplicate\n");
        LogDedupped(LOG_ERR, 1101, kTestFileA, "worker duplicate\n");
    });
    worker.join();
    EXPECT_THAT(shim.stdoutCapture(),
        ::testing::HasSubstr("At thread shutdown, the following message pattern"));
    EXPECT_THAT(shim.stdoutCapture(),
        ::testing::HasSubstr("worker duplicate"));
    EXPECT_THAT(shim.stdoutCapture(),
        ::testing::HasSubstr("occurred 2 times"));
}

// ====================== All standard log levels ======================

TEST_F(DedupLogCaptureTest, LogImmediateFormatsEveryStandardLevel)
{
    ASSERT_EQ(1, setLoggerInterface(LI_STDOUT));
    struct LevelExpectation {
        int level;
        const char *token;
    };
    const LevelExpectation levels[] = {
        { LOG_EMERG, "<0> Emergency" },
        { LOG_ALERT, "<1> Alert" },
        { LOG_CRIT, "<2> Critical" },
        { LOG_ERR, "<3> Error" },
        { LOG_WARNING, "<4> Warning" },
        { LOG_NOTICE, "<5> Notice" },
        { LOG_INFO, "<6> Info" },
        { LOG_DEBUG, "<7> Debug" },
    };

    for (const LevelExpectation &entry : levels) {
        SyscallShim::instance().clearCaptures();
        LogImmediate(entry.level, "level smoke\n");
        EXPECT_THAT(SyscallShim::instance().stdoutCapture(),
            ::testing::HasSubstr(entry.token))
                << "missing token for level " << entry.level;
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
