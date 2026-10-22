cmake_minimum_required(VERSION 2.8.12.2...3.25)
project(libdeduplog CXX C)

# ==============================================================================
# Global Compiler Flags (-Wall -Wextra -Werror -O3)
# ==============================================================================
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Werror -O3")
endif()

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O3")
endif()

include(CheckIncludeFiles)
include(CheckLibraryExists)

# 1. Check for Required Headers
check_include_files("syslog.h" HAVE_SYSLOG_H)
check_include_files("systemd/sd-journal.h" HAVE_SYSTEMD_SD_JOURNAL_H)
check_include_files("stdio.h" HAVE_STDIO_H)
check_include_files("sys/uio.h" HAVE_SYS_UIO_H)
check_include_files("stdint.h" HAVE_STDINT_H)
check_include_files("unistd.h" HAVE_UNISTD_H)
check_include_files("string.h" HAVE_STRING_H)
check_include_files("stdlib.h" HAVE_STDLIB_H)
check_include_files("sys/types.h" HAVE_SYS_TYPES_H)

# 2. Check for systemd library if header is present
if(HAVE_SYSTEMD_SD_JOURNAL_H)
    find_library(SYSTEMD_LIB NAMES systemd)
    if(SYSTEMD_LIB)
        set(HAVE_LIBSYSTEMD 1)
    endif()
endif()

# 3. Generate config.h (mimicking config.h.in)
configure_file(
    "${PROJECT_SOURCE_DIR}/config.h.cmake.in"
    "${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")

# 4. Define the Main Library Target
set(LIB_SOURCES deduplog.cpp)
set(LIB_HEADERS deduplog.h)

add_library(deduplog SHARED ${LIB_SOURCES})

# Target-specific links based on detection
if(HAVE_LIBSYSTEMD)
    target_link_libraries(deduplog ${SYSTEMD_LIB})
endif()

# ==============================================================================
# pkg-config File Generation (.pc)
# ==============================================================================
set(VERSION "1.0.0")
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "\${prefix}")
set(libdir "\${prefix}/lib")
set(includedir "\${prefix}/include")

configure_file(
    "${PROJECT_SOURCE_DIR}/deduplog.pc.cmake.in"
    "${PROJECT_BINARY_DIR}/deduplog.pc"
    @ONLY
)

install(FILES "${PROJECT_BINARY_DIR}/deduplog.pc"
    DESTINATION "lib/pkgconfig"
)

# ==============================================================================
# Installation Rules
# ==============================================================================
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()

install(TARGETS deduplog
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES "${PROJECT_SOURCE_DIR}/deduplog.h"
    DESTINATION include
)

# 5. Descend into the example directory
add_subdirectory(example)

# ==============================================================================
# Custom Automation Targets (astyle, doxygen, cppcheck)
# ==============================================================================
find_program(ASTYLE_EXECUTABLE NAMES astyle)
if(ASTYLE_EXECUTABLE)
    file(GLOB_RECURSE ASTYLE_SOURCES "${PROJECT_SOURCE_DIR}/*.cpp" "${PROJECT_SOURCE_DIR}/*.h" "${PROJECT_SOURCE_DIR}/*.c")
    add_custom_target(astyle
        COMMAND ${ASTYLE_EXECUTABLE} --options=${PROJECT_SOURCE_DIR}/astyle.cfg ${ASTYLE_SOURCES}
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Running Artistic Style code formatter..."
    )
endif()

find_program(DOXYGEN_EXECUTABLE NAMES doxygen)
if(DOXYGEN_EXECUTABLE)
    add_custom_target(doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Generating API documentation via Doxygen..."
    )
endif()

find_program(CPPCHECK_EXECUTABLE NAMES cppcheck)
if(CPPCHECK_EXECUTABLE)
    # Uses universally accepted checker flags compatible across all versions
    add_custom_target(cppcheck
        COMMAND ${CPPCHECK_EXECUTABLE} --enable=all --inconclusive --force --std=c++11 --language=c++
                --output-file=${PROJECT_BINARY_DIR}/cppcheck.out --check-level=exhaustive -v --library=std
                --suppress=failedToEvaluate
                --suppress=preprocessorErrorDirective
                --checkers-report=${PROJECT_BINARY_DIR}/cppcheck.checkers
                -I${PROJECT_BINARY_DIR} -I${PROJECT_SOURCE_DIR}
                ${PROJECT_SOURCE_DIR}/deduplog.cpp 
                ${PROJECT_SOURCE_DIR}/example/
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMENT "Executing static analysis with Cppcheck (output redirected to cppcheck.out)..."
    )
else()
    message(STATUS "cppcheck executable not found - 'make cppcheck' target will be unavailable")
endif()

####################################
#
#  Gtest stuff
#
####################################

#cmake_minimum_required(VERSION 3.14)
#project(deduplog CXX)

#set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Main Library/Target Code ---
# (Keep your existing main library or executable targets here)
# e.g., add_library(deduplog src/deduplog.cpp)

# --- Unit Test Validation Setup ---
# Check if GoogleTest and GoogleMock are available on the system
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST gtest QUIET)
pkg_check_modules(GMOCK gmock QUIET)

# Fallback checking if PkgConfig isn't matching modern targets
if(NOT GTEST_FOUND)
    find_package(GTest QUIET)
endif()

# Only build tests if both gtest and gmock targets are located
if((GTEST_FOUND OR GTest_FOUND) AND GMOCK_FOUND)
    message(STATUS "GoogleTest and GoogleMock found! Enabling unit tests.")
    enable_testing()
    add_subdirectory(tests)
else()
    message(WARNING "gtest or gmock not found. Skipping unit tests configuration.")
endif()

