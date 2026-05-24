# Non-recursive build
lib_LTLIBRARIES = libdeduplog.la

libdeduplog_la_SOURCES = deduplog.cpp
nobase_include_HEADERS = deduplog.h

libdeduplog_la_CXXFLAGS = -Wall -Wextra
libdeduplog_la_LIBADD = $(SYSTEMD_LIBS)
libdeduplog_la_LDFLAGS = -version-info 1:0:0

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = deduplog.pc

# ====================== Examples ======================
noinst_PROGRAMS = example/ctest example/test

example_ctest_SOURCES = example/ctest.c
example_ctest_CFLAGS  = -Wall -Wextra -Werror -O3
example_ctest_LDADD   = libdeduplog.la

example_test_SOURCES  = example/test.cpp
example_test_CXXFLAGS = -Wall -Wextra -std=c++11 -Werror -O3
example_test_LDADD    = libdeduplog.la

# Force examples to be built with normal "make"
all-local: example/ctest example/test

# ====================== astyle ======================
ASTYLE_FILES = deduplog.cpp deduplog.h

astyle: $(ASTYLE_FILES)
if HAVE_ASTYLE
	astyle --options=astyle.cfg $(ASTYLE_FILES)
else
	@echo "astyle is not installed. Install it to format code."
endif

# ====================== doxygen ======================
doxygen:
if HAVE_DOXYGEN
	doxygen Doxyfile
else
	@echo "doxygen is not installed. Install it to generate documentation."
endif

# ====================== examples ======================
examples: all
	@echo "? Examples built: ./example/ctest  ./example/test"

check-examples: examples
	@echo "? Test programs are examples only (they take a long time). Skipping auto-run."

# ====================== Extra files ======================
EXTRA_DIST = \
    astyle.cfg \
    Doxyfile \
    README.md \
    LICENSE \
    AUTHORS \
    TODO \
    ChangeLog \
    .gitignore \
    example/.gitignore \
    example/Makefile.in \
    example/Makefile.am \
    example/test.cpp \
    example/ctest.c

.PHONY: astyle doxygen examples check-examples

clean-local:
	rm -rf documentation
