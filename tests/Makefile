# Standalone gtest/gmock build for libdeduplog.
# Run from repo root: make -C tests
# Or after configure: make check (if autotools detected gtest)

TOP_srcdir := ..
TOP_builddir := ..

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -Wall -Wextra -g -O2
CPPFLAGS  += -I$(TOP_srcdir) -I$(TOP_builddir) -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64
LDFLAGS   += -pthread \
	-Wl,--wrap=time \
	-Wl,--wrap=write \
	-Wl,--wrap=writev \
	-Wl,--wrap=vfprintf \
	-Wl,--wrap=__vfprintf_chk \
	-Wl,--wrap=fflush \
	-Wl,--wrap=openlog \
	-Wl,--wrap=vsyslog
LDLIBS    += -lgtest -lgmock -lpthread -ldl

LIBDEDUPLOG := $(TOP_builddir)/.libs/libdeduplog.so
ifeq ("$(wildcard $(LIBDEDUPLOG))","")
  LIBDEDUPLOG := $(TOP_builddir)/.libs/libdeduplog.la
endif

TEST_SRCS := deduplog_test.cpp syscall_shim.cpp ../deduplog.cpp
TEST_OBJS := deduplog_test.o syscall_shim.o deduplog.o
TEST_BIN  := deduplog_test

.PHONY: all check clean

all: $(TEST_BIN)

$(TEST_BIN): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TEST_OBJS) $(LDLIBS)

deduplog.o: ../deduplog.cpp ../deduplog.h ../config.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -std=c++11 -c -o $@ ../deduplog.cpp

%.o: %.cpp syscall_shim.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

check: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_OBJS) $(TEST_BIN)
