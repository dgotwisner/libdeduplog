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

# ====================== cppcheck ======================
# Note, we are turning off misra check because it doesn't
# correctly work with C++ for Cppcheck - it's reporting comma
# violations on the std::unordered_map lookup tables.
#    --addon=misra
cppcheck:
if HAVE_CPPCHECK
	-rm -f cppcheck.checkers cppcheck.out
	cppcheck --enable=all --inconclusive --force --std=c++11 \
	    --language=c++ -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 \
	    --output-file=cppcheck.out --check-level=exhaustive -v \
	    --addon=y2038 --addon=threadsafety \
	    --library=std --checkers-report=cppcheck.checkers \
	    --suppress=failedToEvaluate --suppress=missingIncludeSystem \
	    --suppress=preprocessorErrorDirective \
	    -D__has_builtin\(x\)=0 -D__has_include\(x\)=0 \
	    -D__has_cpp_attribute\(x\)=0 -D__has_feature\(x\)=0 \
	    *.cpp
	@echo "cppcheck completed, output in cppcheck.out and cppcheck.checkers"
else
	@echo "cppcheck is not installed. Install it to do analysis."
endif

# ====================== clang-tidy ======================
# Clang-tidy requires a compile_commands.json to be set up.  We don't
# want to rely on other tools to do so though.  So we create one.
compile_commands.json:
	-rm -f compile_commands.json
	@echo '{' >> compile_commands.json
	@echo '    "directory": "'`pwd`'","' >> compile_commands.json
	@echo '    "file": "deduplog.cpp",' >> compile_commands.json
	@echo '    "output": "deduplog.o",' >> compile_commands.json
	@echo '    "arguments": [' >> compile_commands.json
	@echo '        "/usr/lib/llvm-21/bin/clang",' >> compile_commands.json
	@echo '        "-xc++",' >> compile_commands.json
	@echo '        "deduplog.cpp",' >> compile_commands.json
	@echo '        "-o",' >> compile_commands.json
	@echo '        "deduplog.o",' >> compile_commands.json
	@echo '        "--driver-mode=g++",' >> compile_commands.json
	@echo '        "-c",' >> compile_commands.json
	@echo '        "--target=x86_64-pc-linux-gnu"' >> compile_commands.json
	@echo '    ]' >> compile_commands.json
	@echo '}' >> compile_commands.json

clang-tidy: compile_commands.json
if HAVE_CLANGTIDY
	-rm -f clang-tidy.out
	clang-tidy -p . deduplog.cpp -- -std=c++11 > clang-tidy.out
else
	@echo "clang-tidy is not installed. Install it to do analysis."
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
