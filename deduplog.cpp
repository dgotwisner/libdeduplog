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

#include "config.h" // for definitions

/* Define to 1 if you have the <stdint.h> header file. */
#ifndef HAVE_STDLIB_H
    #error Missing required stdlib.h
#endif // HAVE_STDLIB_H
#ifndef HAVE_STRING_H
    #error Missing required string.h / cstring
#endif // HAVE_STRING_H
#ifndef HAVE_SYS_TYPES_H
    #error Missing required sys/types.h
#endif // HAVE_SYS_TYPES_H
#ifndef HAVE_UNISTD_H
    #error Missing required unistd.h
#endif // HAVE_UNISTD_H

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>

#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#ifdef HAVE_SYSLOG_H
    #include <syslog.h>
#endif // HAVE_SYSLOG_H
#ifdef HAVE_SYS_UIO_H
    #include <sys/uio.h>
#endif // HAVE_SYS_UIO_H
#include <sys/types.h>
//#include <sys/syscall.h> // only needed for gettid() which is used for debugging
#ifdef HAVE_SYSTEMD
    #include <systemd/sd-journal.h>
#endif // HAVE_SYSTEMD
#include <unistd.h>

#include "deduplog.h"

static std::atomic<int> s_logLevel(LOG_DEBUG); // default logs everything
static std::atomic<uint64_t> s_timeThreshold(60); // default is one minute
static std::atomic<LoggerInterface_t> s_interface(LI_STDOUT);

// This set of objects is used very rarely - when creating a new thread,
// and when destroying a thread.
class TlsObj;
static std::mutex
s_mtx; // used to lock thread creation / destruction with TLS
static std::unordered_set<TlsObj *>
s_tlsSet; // set of thread-local objects

static std::mutex s_syslogMtx; // used to lock syslog for opensyslog()
static bool isSyslogOpened = false;

// cppcheck-suppress misra-c2012-12.3
typedef std::unordered_map<int, const char *> LTLSMap_t;

// cppcheck-suppress misra-c2012-12.3
static const LTLSMap_t levelToLevelString = {
    { LOG_EMERG, "<0> " },
    { LOG_ALERT, "<1> " },
    { LOG_CRIT, "<2> " },
    { LOG_ERR, "<3> " },
    { LOG_WARNING, "<4> " },
    { LOG_NOTICE, "<5> " },
    { LOG_INFO, "<6> " },
    { LOG_DEBUG, "<7> " },
};

// cppcheck-suppress misra-c2012-12.3
typedef std::unordered_map<int, const char *> LTLTMap_t;

// cppcheck-suppress misra-c2012-12.3
static const LTLTMap_t levelToLevelText = {
    { LOG_EMERG, "Emergency " },
    { LOG_ALERT, "Alert " },
    { LOG_CRIT, "Critical " },
    { LOG_ERR, "Error " },
    { LOG_WARNING, "Warning " },
    { LOG_NOTICE, "Notice " },
    { LOG_INFO, "Info " },
    { LOG_DEBUG, "Debug " },
};

//static pid_t gettid(void)
//{
//return syscall(SYS_gettid);
//}

// Each item is a unique message (unformatted)
class UniqueItem final {
    public:
        UniqueItem(void) = delete;
        explicit UniqueItem(const std::string &pattern):
            m_fmtPattern(pattern), m_lastOutputTime(time(NULL)),
            m_countSinceLastOutputTime(0) {
        }
        ~UniqueItem(void) {
        }
        void set(const char *pattern, time_t lastTime, uint64_t counter) {
            m_fmtPattern = pattern;
            m_lastOutputTime = lastTime;
            m_countSinceLastOutputTime = counter;
        }
        void incCount(void) {
            m_countSinceLastOutputTime++;
        }
        void setCount(uint64_t value) {
            m_countSinceLastOutputTime = value;
        }
        uint64_t getCount(void) const {
            return m_countSinceLastOutputTime;
        }
        time_t getTime(void) const {
            return m_lastOutputTime;
        }
        void setTime(time_t newTime) {
            m_lastOutputTime = newTime;
        }
        const char *getPattern(void) const {
            return m_fmtPattern.c_str();
        }
    private:
        std::string m_fmtPattern;
        time_t      m_lastOutputTime;
        uint64_t    m_countSinceLastOutputTime;
};
typedef std::unordered_map<const char *, UniqueItem> FileMap_t;
typedef std::unordered_map<int, FileMap_t> LineMap_t;
typedef std::unordered_map<int, LineMap_t> LevelMap_t;
// Note, we constrain by level, then my line, then by file

static void printSingleMsgMap(LevelMap_t &msgMap, bool isAtExit,
    std::string &countMsg, time_t now); // forward reference

// There is one of these per thread.  The constructor links the object to an
// unordered_set, which is simply the address of the object
class TlsObj final {
    public:
        TlsObj(void) {
            //printf("Constructing thread local object - tid = %d\n", gettid());
            std::lock_guard<std::mutex> lock(s_mtx);
            s_tlsSet.emplace(this);
        }
        ~TlsObj(void) {
            std::lock_guard<std::mutex> lock(s_mtx);
            auto i = s_tlsSet.find(this);

            if (i != s_tlsSet.end()) {
                std::string countMsg;
                countMsg.reserve(1024);
                // we need this to see if we need to output or when adding something for the first time.
                time_t now = time(NULL);
                printSingleMsgMap(m_msgMap, false, countMsg, now);
                s_tlsSet.erase(this);
            }

            //printf("Destroying thread local object - tid = %d\n", gettid());
        }
    public: // should be private, change later
        LevelMap_t m_msgMap;
};

static void printSingleMsgMap(LevelMap_t &msgMap, bool isAtExit,
    std::string &countMsg, time_t now)
{
    for (auto &msgMapIt : msgMap) {
        int level = msgMapIt.first;
        // Insert "<N>" and a string description.
        const char *ltoS = NULL;
        const auto iLevelString = levelToLevelString.find(level);

        if (iLevelString == levelToLevelString.end()) {
            ltoS = "<7>"; // assume debug, should never occur
        } else {
            ltoS = iLevelString->second;
        }

        // and deal with the human readable version of the above
        const char *ltoT = NULL;
        const auto iLevelText = levelToLevelText.find(level);

        if (iLevelText == levelToLevelText.end()) {
            ltoT = "Debug"; // assume debug, should never occur
        } else {
            ltoT = iLevelText->second;
        }

        for (auto &lineMapIt : msgMapIt.second) {
            for (auto &fileMapIt : lineMapIt.second) {
                // time elapsed, so dump "count message" and reset start time to now and reset count
                // to 0.
                countMsg.clear();

                if (s_interface == LI_STDERR || s_interface == LI_STDOUT) {
                    countMsg += ltoS;
                }

                countMsg += ltoT;

                if (isAtExit) {
                    countMsg += " At shutdown, the following message pattern (";
                } else {
                    countMsg += " At thread shutdown, the following message pattern (";
                }

                countMsg += fileMapIt.second.getPattern();
                countMsg += ") from file ";
                countMsg += fileMapIt.first;
                countMsg += ", line ";
                countMsg += std::to_string(lineMapIt.first);
                countMsg += ", occurred ";
                countMsg += std::to_string(fileMapIt.second.getCount());
                countMsg += " times in ";
                countMsg += std::to_string(now - fileMapIt.second.getTime());
                countMsg += " seconds\n";
                ssize_t ret;

                switch (s_interface) {
                    case LI_STDERR:
                        ret = write(2, countMsg.c_str(), countMsg.size());
                        break;

                    case LI_STDOUT:
                        ret = write(1, countMsg.c_str(), countMsg.size());
                        break;

                    case LI_JOURNAL:
#ifdef HAVE_SYSTEMD
                        sd_journal_print(level, countMsg.c_str());
#endif // HAVE_SYSTEMD
                        break;

                    case LI_SYSLOG: {
                            {
                                std::lock_guard<std::mutex> lock(s_syslogMtx);

                                if (!isSyslogOpened) {
                                    isSyslogOpened = true;
                                    openlog(NULL, LOG_PID, LOG_USER);
                                }
                            }
                            syslog(level, "%s", countMsg.c_str());
                        }
                        break;
                }

                (void)ret;
                // We will be deleting at the end of the loop, but reset here just in case.
                fileMapIt.second.setCount(0);
                fileMapIt.second.setTime(now);
            }
        }

        msgMapIt.second.clear();
    }
}

static thread_local TlsObj
t_tlsObj; // There is a single variable overlaying all
// threads worth of it.

// Atexit() handler to finish up all the TLS data for counters,
// and to clear the maps.
static std::atomic<int> s_shutdownRegistered(0);
static void shutdownCleanup(void)
{
    //printf("SHUTTING DOWN CLEANUP - tid %d\n", gettid());
    std::string countMsg;
    countMsg.reserve(1024);
    // we need this to see if we need to output or when adding something for the first time.
    time_t now = time(NULL);
    // Walk through all threads' TLS objects at shutdown and print
    // the counts, and then remove the items.
    std::lock_guard<std::mutex> lock(s_mtx);

    //printf("--- s_tlsSet.size() == %lu\n", s_tlsSet.size());
    for (auto &tlsIt : s_tlsSet) {
        //printf("--- s_tlsSet entrie's m_msgMap.size() == %lu\n", tlsIt->m_msgMap.size());
        printSingleMsgMap(tlsIt->m_msgMap, true, countMsg, now);
        tlsIt->m_msgMap.clear();  // clear the map, but don't delete the actual TLS objects.
    }

    //printf("FINISHED WITH SHUTTING DOWN CLEANUP\n");
}

// Note, we don't allow collapsing messages by more than an hour

int getLogLevel(void)
{
    return s_logLevel;
}

int setLogLevel(int level)
{
    if (level < LOG_EMERG || level > LOG_DEBUG) {
        return 0;
    }

    s_logLevel = level;
    return 1;
}

uint64_t getTimeThreshold(void)
{
    return s_timeThreshold;
}

int setTimeThreshold(uint64_t threshold)
{
    if (threshold == 0 || threshold > 60 * 60) {
        return 0;
    }

    s_timeThreshold = threshold;
    return 1;
}

LoggerInterface_t getLoggerInterface(void)
{
    return s_interface;
}

int setLoggerInterface(LoggerInterface_t interface)
{
    switch (interface) {
#ifdef HAVE_SYSLOG_H

        case LI_JOURNAL:
#endif // HAVE_SYSLOG_H
#ifdef HAVE_SYSTEMD
        case LI_SYSLOG:
#endif // HAVE_SYSTEMD
        case LI_STDERR:
        case LI_STDOUT:
            s_interface = interface;
            return 1;

        default:
            return 0;
    }
}

void LogSafe(int level, const struct iovec *iov, int iovcnt)
{
    // If the log level is lower priority than we are set to, simply
    // return - Highest priority (EMERG) is lowest, numerically
    if (level < 0 || level > s_logLevel) {
        // also return if we have a negative level, coming in
        return;
    }

    // Insert "<N>" and a string description.
    const char *ltoS = NULL;
    const auto iLevelString = levelToLevelString.find(level);

    if (iLevelString == levelToLevelString.end()) {
        ltoS = "<7>"; // assume debug, should never occur
    } else {
        ltoS = iLevelString->second;
    }

    // and deal with the human readable version of the above
    const char *ltoT = NULL;
    const auto iLevelText = levelToLevelText.find(level);

    if (iLevelText == levelToLevelText.end()) {
        ltoT = "Debug"; // assume debug, should never occur
    } else {
        ltoT = iLevelText->second;
    }

    if (s_interface == LI_SYSLOG) {
        return; // we can't safely report to syslog through a signal
        // handler
    }

    int srcIx = 0;
    int dstIx = 0;
    struct iovec vec[iovcnt + 2];
    vec[dstIx].iov_base = const_cast<char *>(ltoS);
    vec[dstIx].iov_len = strlen(ltoS);
    dstIx++;
    vec[dstIx].iov_base = const_cast<char *>(ltoT);
    vec[dstIx].iov_len = strlen(ltoT);
    dstIx++;

    while (srcIx < iovcnt) {
        vec[dstIx].iov_base = iov[srcIx].iov_base;
        vec[dstIx].iov_len = iov[srcIx].iov_len;
        srcIx++;
	dstIx++;
    }

    ssize_t ret;

    switch (s_interface) {
        case LI_STDERR:
        case LI_JOURNAL:
            // Note, there is a hook for journal output
            // that if we write to stderr, it does the right
            // thing.  From experience, the <N> gets converted
            // into the log level part of the journal message.
#ifdef HAVE_WRITEV
            ret = writev(2, vec, iovcnt + 2);
#else

            for (int i = 0; i < iovcnt; i++) {
                ret = write(2, vec[i].iov_base, vec[i].iov_len);
            }

#endif // HAVE_WRITEV
            break;

        case LI_STDOUT:
#ifdef HAVE_WRITEV
            ret = writev(1, vec, iovcnt + 2);
#else

            for (int i = 0; i < iovcnt; i++) {
                ret = write(1, vec[i].iov_base, vec[i].iov_len);
            }

#endif // HAVE_WRITEV
            break;

        case LI_SYSLOG:
            // We can't get here - we returned early, up above.
            /*NOTREACHED*/
            break;
    }

    (void)ret; // eliminate compiler warning
}

void LogImmediate(int level, const char *format, ...)
{
    // If the log level is lower priority than we are set to, simply
    // return - Highest priority (EMERG) is lowest, numerically
    if (level < 0 || level > s_logLevel) {
        // also return if we have a negative level, coming in
        return;
    }

    // Insert "<N>" and a string description.
    const char *ltoS = NULL;
    const auto iLevelString = levelToLevelString.find(level);

    if (iLevelString == levelToLevelString.end()) {
        ltoS = "<7>"; // assume debug, should never occur
    } else {
        ltoS = iLevelString->second;
    }

    // and deal with the human readable version of the above
    const char *ltoT = NULL;
    const auto iLevelText = levelToLevelText.find(level);

    if (iLevelText == levelToLevelText.end()) {
        ltoT = "Debug"; // assume debug, should never occur
    } else {
        ltoT = iLevelText->second;
    }

    switch (s_interface) {
        case LI_STDERR:
        case LI_STDOUT: {
                va_list ap;
                va_start(ap, format);
                std::string s = ltoS;
                s += ltoT;
                s += format;
                vfprintf(s_interface == LI_STDERR ? stderr : stdout,
                    s.c_str(), ap);
                va_end(ap);
                // flush the output, since we want the messages going out on-time
                fflush(s_interface == LI_STDERR ? stderr : stdout);
            }
            break;

        case LI_JOURNAL: {
#ifdef HAVE_SYSTEMD
                va_list ap;
                va_start(ap, format);
                std::string s = ltoS;
                s += ltoT;
                s += format;
                sd_journal_printv(level, s.c_str(), ap);
                va_end(ap);
#endif // HAVE_SYSTEMD
            }
            break;

        case LI_SYSLOG: {
                {
                    std::lock_guard<std::mutex> lock(s_syslogMtx);

                    if (!isSyslogOpened) {
                        isSyslogOpened = true;
                        openlog(NULL, LOG_PID, LOG_USER);
                    }
                }
                va_list ap;
                va_start(ap, format);
                std::string s = ltoS;
                s += ltoT;
                s += format;
                vsyslog(level, s.c_str(), ap);
                va_end(ap);
            }
            /*NOTREACHED*/
            break;
    }
}

void LogImmediateVA(int level, const char *format, va_list ap)
{
    // If the log level is lower priority than we are set to, simply
    // return - Highest priority (EMERG) is lowest, numerically
    if (level < 0 || level > s_logLevel) {
        // also return if we have a negative level, coming in
        return;
    }

    // Insert "<N>" and a string description.
    const char *ltoS = NULL;
    const auto iLevelString = levelToLevelString.find(level);

    if (iLevelString == levelToLevelString.end()) {
        ltoS = "<7>"; // assume debug, should never occur
    } else {
        ltoS = iLevelString->second;
    }

    // and deal with the human readable version of the above
    const char *ltoT = NULL;
    const auto iLevelText = levelToLevelText.find(level);

    if (iLevelText == levelToLevelText.end()) {
        ltoT = "Debug"; // assume debug, should never occur
    } else {
        ltoT = iLevelText->second;
    }

    switch (s_interface) {
        case LI_STDERR:
        case LI_STDOUT: {
                std::string s = ltoS;
                s += ltoT;
                s += format;
                vfprintf(s_interface == LI_STDERR ? stderr : stdout,
                    s.c_str(), ap);
                // flush the output, since we want the messages going out on-time
                fflush(s_interface == LI_STDERR ? stderr : stdout);
            }
            break;

        case LI_JOURNAL: {
#ifdef HAVE_SYSTEMD
                std::string s = ltoS;
                s += ltoT;
                s += format;
                sd_journal_printv(level, s.c_str(), ap);
#endif // HAVE_SYSTEMD
            }
            break;

        case LI_SYSLOG: {
                {
                    std::lock_guard<std::mutex> lock(s_syslogMtx);

                    if (!isSyslogOpened) {
                        isSyslogOpened = true;
                        openlog(NULL, LOG_PID, LOG_USER);
                    }
                }
                std::string s = ltoS;
                s += ltoT;
                s += format;
                vsyslog(level, s.c_str(), ap);
            }
            /*NOTREACHED*/
            break;
    }
}

void LogDedupped(int level, int lineNumber, const char *filename,
    const char *pattern, ...)
{
    if (s_shutdownRegistered == 0) {
        // There is a small timing window here if Log() is called
        // from multiple threads at startup the first time, we can
        // live with it, because the calls are executed serially.
        s_shutdownRegistered = 1;
        atexit(shutdownCleanup);
    }

    // If the log level is lower priority than we are set to, simply
    // return - Highest priority (EMERG) is lowest, numerically
    if (level < 0 || level > s_logLevel) {
        // also return if we have a negative level, coming in
        return;
    }

    // Insert "<N>" and a string description.
    const char *ltoS = NULL;
    const auto iLevelString = levelToLevelString.find(level);

    if (iLevelString == levelToLevelString.end()) {
        ltoS = "<7>"; // assume debug, should never occur
    } else {
        ltoS = iLevelString->second;
    }

    // and deal with the human readable version of the above
    const char *ltoT = NULL;
    const auto iLevelText = levelToLevelText.find(level);

    if (iLevelText == levelToLevelText.end()) {
        ltoT = "Debug"; // assume debug, should never occur
    } else {
        ltoT = iLevelText->second;
    }

    // we need this to see if we need to output or when adding something for the first time.
    time_t now = time(NULL);
    // First, see if the existing entry is in the TlsObj's m_msgMap, first for log level, then for
    // line number, and finally for filename.  If it's in all of them, simply bump the counter
    bool outputRecord = false; // default - we aren't outputting
    auto levelIt = t_tlsObj.m_msgMap.find(level);

    if (levelIt == t_tlsObj.m_msgMap.end()) {
        LineMap_t lineMap;
        FileMap_t fileMap;
        // not found, so build up the entire hierarchy
        UniqueItem item(pattern);
        item.set(pattern, now, 1);
        //printf("__ tid=%d_ 1/adding new item to tlsObj's msgmap (no entry for log level %d): pattern '%s'\n", gettid(), level, pattern);
        fileMap.emplace(filename, std::move(item));
        lineMap.emplace(lineNumber, std::move(fileMap));
        t_tlsObj.m_msgMap.emplace(level, std::move(lineMap));
        outputRecord = true;
    } else {
        // level is found, now see if this linenumber has been processed yet
        LineMap_t &lineMapRef = levelIt->second;
        auto lineIt = lineMapRef.find(lineNumber);

        if (lineIt == lineMapRef.end()) {
            FileMap_t fileMap;
            UniqueItem item(pattern);
            // not found, so build up the hierarchy
            item.set(pattern, now, 1);
            fileMap.emplace(filename, std::move(item));
            lineMapRef.emplace(lineNumber, std::move(fileMap));
            outputRecord = true;
            //printf("__ tid=%d_ 2/adding new item to tlsObj's msgmap (no entry for log level %d's line number %d): pattern '%s'\n", gettid(), level, lineNumber, pattern);
        } else {
            // level and line number are found, now look at the filename.
            FileMap_t &fileMapRef = lineIt->second;
            auto fileIt = lineIt->second.find(filename);

            if (fileIt == lineIt->second.end()) {
                // not found, so build up the hierarchy
                UniqueItem item(pattern);
                item.set(pattern, now, 1);
                fileMapRef.emplace(filename, std::move(item));
                outputRecord = true;
                //printf("__ tid=%d_ 3/adding new item to tlsObj's msgmap (no entry for log level %d's line number %d for filename %s): pattern '%s'\n", gettid(), level, lineNumber, filename, pattern);
            } else {
                // level line number, and filename are found, simply bump counters and, if appropriate,
                // reset counter and flag for output
                //printf("__ tid=%d_ 4/found in map already for log level %d, line number %d, filename %s: pattern '%s'\n", gettid(), level, lineNumber, filename, pattern);
                UniqueItem &existing = fileIt->second;
                existing.incCount();

                if ((existing.getCount() > 1) &&
                    ((time_t)(existing.getTime() + s_timeThreshold) < now)) {
                    //printf("__ tid=%d_ 5/time elapsed, outputing \"count\" message\n", gettid());
                    // time elapsed, so dump "count message" and reset start time to now and reset count
                    // to 0.
                    std::string countMsg;
                    countMsg.reserve(1024);

                    if (s_interface == LI_STDERR || s_interface == LI_STDOUT) {
                        countMsg += ltoS;
                    }

                    countMsg += ltoT;
                    countMsg += " The following message pattern (";
                    countMsg += existing.getPattern();
                    countMsg += ") from file ";
                    countMsg += fileIt->first;
                    countMsg += ", line ";
                    countMsg += std::to_string(lineIt->first);
                    countMsg += ", occurred ";
                    countMsg += std::to_string(existing.getCount());
                    countMsg += " times in ";
                    countMsg += std::to_string(now - existing.getTime());
                    countMsg += " seconds\n";
                    ssize_t ret;

                    switch (s_interface) {
                        case LI_STDERR:
                            ret = write(2, countMsg.c_str(), countMsg.size());
                            break;

                        case LI_STDOUT:
                            ret = write(1, countMsg.c_str(), countMsg.size());
                            break;

                        case LI_JOURNAL:
#ifdef HAVE_SYSTEMD
                            sd_journal_print(level, countMsg.c_str());
#endif // HAVE_SYSTEMD
                            break;

                        case LI_SYSLOG: {
                                {
                                    std::lock_guard<std::mutex> lock(s_syslogMtx);

                                    if (!isSyslogOpened) {
                                        isSyslogOpened = true;
                                        openlog(NULL, LOG_PID, LOG_USER);
                                    }
                                }
                                syslog(level, "%s", countMsg.c_str());
                            }
                            break;
                    }

                    (void)ret;
                    existing.setCount(0);
                    existing.setTime(now);
                }
            }
        }
    }

    if (outputRecord) {
        // And now, if appropriate, output the record
        switch (s_interface) {
            case LI_STDERR:
            case LI_STDOUT: {
                    va_list ap;
                    va_start(ap, pattern);
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    vfprintf(s_interface == LI_STDERR ? stderr : stdout,
                        s.c_str(), ap);
                    va_end(ap);
                    // flush the output, since we want the messages going out on-time
                    fflush(s_interface == LI_STDERR ? stderr : stdout);
                }
                break;

            case LI_JOURNAL: {
#ifdef HAVE_SYSTEMD
                    va_list ap;
                    va_start(ap, pattern);
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    sd_journal_printv(level, s.c_str(), ap);
                    va_end(ap);
#endif // HAVE_SYSTEMD
                }
                break;

            case LI_SYSLOG: {
                    {
                        std::lock_guard<std::mutex> lock(s_syslogMtx);

                        if (!isSyslogOpened) {
                            isSyslogOpened = true;
                            openlog(NULL, LOG_PID, LOG_USER);
                        }
                    }
                    va_list ap;
                    va_start(ap, pattern);
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    vsyslog(level, s.c_str(), ap);
                    va_end(ap);
                }
                /*NOTREACHED*/
                break;
        }
    }
}

void LogDeduppedVA(int level, int lineNumber, const char *filename,
    const char *pattern, va_list ap)
{
    if (s_shutdownRegistered == 0) {
        // There is a small timing window here if Log() is called
        // from multiple threads at startup the first time, we can
        // live with it, because the calls are executed serially.
        s_shutdownRegistered = 1;
        atexit(shutdownCleanup);
    }

    // If the log level is lower priority than we are set to, simply
    // return - Highest priority (EMERG) is lowest, numerically
    if (level < 0 || level > s_logLevel) {
        // also return if we have a negative level, coming in
        return;
    }

    // Insert "<N>" and a string description.
    const char *ltoS = NULL;
    const auto iLevelString = levelToLevelString.find(level);

    if (iLevelString == levelToLevelString.end()) {
        ltoS = "<7>"; // assume debug, should never occur
    } else {
        ltoS = iLevelString->second;
    }

    // and deal with the human readable version of the above
    const char *ltoT = NULL;
    const auto iLevelText = levelToLevelText.find(level);

    if (iLevelText == levelToLevelText.end()) {
        ltoT = "Debug"; // assume debug, should never occur
    } else {
        ltoT = iLevelText->second;
    }

    // we need this to see if we need to output or when adding something for the first time.
    time_t now = time(NULL);
    // First, see if the existing entry is in the TlsObj's m_msgMap, first for log level, then for
    // line number, and finally for filename.  If it's in all of them, simply bump the counter
    bool outputRecord = false; // default - we aren't outputting
    auto levelIt = t_tlsObj.m_msgMap.find(level);

    if (levelIt == t_tlsObj.m_msgMap.end()) {
        LineMap_t lineMap;
        FileMap_t fileMap;
        // not found, so build up the entire hierarchy
        UniqueItem item(pattern);
        item.set(pattern, now, 1);
        //printf("__ tid=%d_ 1/adding new item to tlsObj's msgmap (no entry for log level %d): pattern '%s'\n", gettid(), level, pattern);
        fileMap.emplace(filename, std::move(item));
        lineMap.emplace(lineNumber, std::move(fileMap));
        t_tlsObj.m_msgMap.emplace(level, std::move(lineMap));
        outputRecord = true;
    } else {
        // level is found, now see if this linenumber has been processed yet
        LineMap_t &lineMapRef = levelIt->second;
        auto lineIt = lineMapRef.find(lineNumber);

        if (lineIt == lineMapRef.end()) {
            FileMap_t fileMap;
            UniqueItem item(pattern);
            // not found, so build up the hierarchy
            item.set(pattern, now, 1);
            fileMap.emplace(filename, std::move(item));
            lineMapRef.emplace(lineNumber, std::move(fileMap));
            outputRecord = true;
            //printf("__ tid=%d_ 2/adding new item to tlsObj's msgmap (no entry for log level %d's line number %d): pattern '%s'\n", gettid(), level, lineNumber, pattern);
        } else {
            // level and line number are found, now look at the filename.
            FileMap_t &fileMapRef = lineIt->second;
            auto fileIt = lineIt->second.find(filename);

            if (fileIt == lineIt->second.end()) {
                // not found, so build up the hierarchy
                UniqueItem item(pattern);
                item.set(pattern, now, 1);
                fileMapRef.emplace(filename, std::move(item));
                outputRecord = true;
                //printf("__ tid=%d_ 3/adding new item to tlsObj's msgmap (no entry for log level %d's line number %d for filename %s): pattern '%s'\n", gettid(), level, lineNumber, filename, pattern);
            } else {
                // level line number, and filename are found, simply bump counters and, if appropriate,
                // reset counter and flag for output
                //printf("__ tid=%d_ 4/found in map already for log level %d, line number %d, filename %s: pattern '%s'\n", gettid(), level, lineNumber, filename, pattern);
                UniqueItem &existing = fileIt->second;
                existing.incCount();

                if ((existing.getCount() > 1) &&
                    ((time_t)(existing.getTime() + s_timeThreshold) < now)) {
                    //printf("__ tid=%d_ 5/time elapsed, outputing \"count\" message\n", gettid());
                    // time elapsed, so dump "count message" and reset start time to now and reset count
                    // to 0.
                    std::string countMsg;
                    countMsg.reserve(1024);

                    if (s_interface == LI_STDERR || s_interface == LI_STDOUT) {
                        countMsg += ltoS;
                    }

                    countMsg += ltoT;
                    countMsg += " The following message pattern (";
                    countMsg += existing.getPattern();
                    countMsg += ") from file ";
                    countMsg += fileIt->first;
                    countMsg += ", line ";
                    countMsg += std::to_string(lineIt->first);
                    countMsg += ", occurred ";
                    countMsg += std::to_string(existing.getCount());
                    countMsg += " times in ";
                    countMsg += std::to_string(now - existing.getTime());
                    countMsg += " seconds\n";
                    ssize_t ret;

                    switch (s_interface) {
                        case LI_STDERR:
                            ret = write(2, countMsg.c_str(), countMsg.size());
                            break;

                        case LI_STDOUT:
                            ret = write(1, countMsg.c_str(), countMsg.size());
                            break;

                        case LI_JOURNAL:
#ifdef HAVE_SYSTEMD
                            sd_journal_print(level, countMsg.c_str());
#endif // HAVE_SYSTEMD
                            break;

                        case LI_SYSLOG: {
                                {
                                    std::lock_guard<std::mutex> lock(s_syslogMtx);

                                    if (!isSyslogOpened) {
                                        isSyslogOpened = true;
                                        openlog(NULL, LOG_PID, LOG_USER);
                                    }
                                }
                                syslog(level, "%s", countMsg.c_str());
                            }
                            break;
                    }

                    (void) ret;
                    existing.setCount(0);
                    existing.setTime(now);
                }
            }
        }
    }

    if (outputRecord) {
        // And now, if appropriate, output the record
        switch (s_interface) {
            case LI_STDERR:
            case LI_STDOUT: {
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    vfprintf(s_interface == LI_STDERR ? stderr : stdout,
                        s.c_str(), ap);
                    // flush the output, since we want the messages going out on-time
                    fflush(s_interface == LI_STDERR ? stderr : stdout);
                }
                break;

            case LI_JOURNAL: {
#ifdef HAVE_SYSTEMD
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    sd_journal_printv(level, s.c_str(), ap);
#endif // HAVE_SYSTEMD
                }
                break;

            case LI_SYSLOG: {
                    {
                        std::lock_guard<std::mutex> lock(s_syslogMtx);

                        if (!isSyslogOpened) {
                            isSyslogOpened = true;
                            openlog(NULL, LOG_PID, LOG_USER);
                        }
                    }
                    std::string s = ltoS;
                    s += ltoT;
                    s += pattern;
                    vsyslog(level, s.c_str(), ap);
                }
                /*NOTREACHED*/
                break;
        }
    }
}
