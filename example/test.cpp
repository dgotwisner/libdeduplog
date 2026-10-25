#include "config.h" // for definition

#ifdef HAVE_SYSLOG_H
    #include <syslog.h>
#endif // HAVE_SYSLOG_H
#ifdef HAVE_SYS_UIO_H
    #include <sys/uio.h>
#endif // HAVE_SYS_UIO_H

#include <cstdlib>
#include <cstdio>

#ifdef HAVE_STRING_H
    #include <cstring>
#endif // HAVE_STRING_H

#ifdef HAVE_UNISTD_H
    #include <unistd.h>
#endif // HAVE_UNISTD_H

#include <thread>
#include <vector>

#include "deduplog.h"

#define dim(x) (sizeof(x) / sizeof(x[0]))

void fcn(int threadNum)
{
    for (int i = 0; i < 100; i++) {
        LogDedupped(LOG_ERR, __LINE__, __FILE__,
            "This is thread %d with message count %d\n", threadNum, i);
    }

    printf("Fcn(%d) is returning\n", threadNum);
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

static void logImmediateVaHelper(int level, const char *pattern, ...)
{
    va_list ap;
    va_start(ap, pattern);
    LogImmediateVA(level, pattern, ap);
    va_end(ap);
}

// Note, this is the initial test program, and is single threaded
int main(int ac, char **av)
{
    (void) ac;
    (void) av;
    setLogLevel(LOG_ERR); // only report ERROR on up
    setLoggerInterface(LI_STDOUT);
    setTimeThreshold(10);
    {
        // LogSafe test
        struct iovec iov[4];
        iov[0].iov_base = const_cast<char *>("this is a ");
        iov[1].iov_base = const_cast<char *>("test of a ");
        iov[2].iov_base = const_cast<char *>("4 item ");
        iov[3].iov_base = const_cast<char *>("vector\n");

        for (unsigned i = 0; i < dim(iov); i++) {
            iov[i].iov_len = strlen(reinterpret_cast<const char *>(iov[i].iov_base));
        }

        for (int i = 0; i < LOG_DEBUG; i++) {
            LogSafe(i, iov, dim(iov));
        }
    }
    {
        // LogImmediate test
        for (int i = 0; i < LOG_DEBUG; i++) {
            LogImmediate(i, "This is a LogImmediate test, level %d\n", i);
        }
    }
    {
        // Log  test
        int i, j, k;

        for (i = 0; i < LOG_DEBUG; i++) {
            for (k = 0; k < 20; k++) {
                for (j = 0; j < 1000; j++) {
                    LogDedupped(i, __LINE__, __FILE__,
                        "This is a Log test, level %d - dedupped, iterator %d\n", i, j);
                }

                if (k != 0) {
                    sleep(1);
                }
            }
        }
    }
    {
        int i;

        for (i = 0; i < 3; i++) {
            LogDedupped(LOG_ERR, __LINE__, __FILE__,
                "This is a message that will be replicated 3x in a short period of time, so should trigger the atexit() handler\n");
        }
    }
    // Now do some thread stuff - 3 threads
    {
        std::thread *threads[3];

        for (int i = 0; i < 3; i++) {
            threads[i] = new std::thread(fcn, i);
        }

        sleep(30);

        for (int i = 0; i < 3; i++) {
            if (threads[i]->joinable()) {
                printf("Joining thread %d\n", i);
                threads[i]->join();
                printf("Deleting thread %d\n", i);
                delete threads[i];
                printf("Deleted thread %d\n", i);
                threads[i] = NULL;
            }
        }
    }
    {
        setLogLevel(LOG_DEBUG); // only report DEBUG on up
        std::vector<LoggerInterface_t> interfaces = { LI_STDOUT, LI_STDERR, LI_SYSLOG, LI_JOURNAL };
        int pseudoLineNumber = 0;

        for (LoggerInterface_t interface_val : interfaces) {
            if (setLoggerInterface(interface_val) == 1) {
                LogDedupped(LOG_DEBUG, pseudoLineNumber % 4, "file1.cpp",
                    "LogDedupped: Cycling interface matrix lines\n");
                logDeduppedVaHelper(LOG_DEBUG, (pseudoLineNumber % 4) + 10, "file2.cpp",
                    "LogDedupped: Cycling interface matrix lines\n");
                LogImmediate(LOG_DEBUG, "LogImmediate: Cycling interface matrix lines\n");
                logImmediateVaHelper(LOG_DEBUG,
                    "LogDedupped: Cycling interface matrix lines\n");
                {
                    struct iovec iov[2];
                    iov[0].iov_base = const_cast<char *>("LogSafe ");
                    iov[1].iov_base = const_cast<char *>("cycling interface matrix lines\n");

                    for (unsigned i = 0; i < dim(iov); i++) {
                        iov[i].iov_len = strlen(reinterpret_cast<const char *>(iov[i].iov_base));
                    }

                    LogSafe(LOG_DEBUG, iov, dim(iov));
                }
            }

            pseudoLineNumber++;
        }
    }
    return 0;
}
