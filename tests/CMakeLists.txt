cmake_minimum_required(VERSION 2.8.12.2...3.25)

# 1. Define Search Paths matching your original CPPFLAGS
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

# Apply Year 2038 / 64-bit safety flags matching original CPPFLAGS
add_compile_definitions(_TIME_BITS=64 _FILE_OFFSET_BITS=64)

# Ensure proper include flags from PkgConfig variables if available
if(GTEST_INCLUDE_DIRS)
    include_directories(${GTEST_INCLUDE_DIRS} ${GMOCK_INCLUDE_DIRS})
endif()

# Define your unit test target binary (Include deduplog.cpp here!)
add_executable(deduplog_test 
    deduplog_test.cpp
    syscall_shim.cpp
    ${CMAKE_SOURCE_DIR}/deduplog.cpp
)

# 2. Extract global warning/optimization flags, stripping out standard constraints
string(REPLACE "-std=c++11" "" TEST_COMPILE_FLAGS "${CMAKE_CXX_FLAGS}")
set_target_properties(deduplog_test PROPERTIES
    COMPILE_FLAGS "${TEST_COMPILE_FLAGS}"
)

# 3. Enforce C++17 globally for the test runner framework
set_target_properties(deduplog_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Explicitly downgrade just deduplog.cpp back to C++11 matching your Makefile recipe
set_source_files_properties(${CMAKE_SOURCE_DIR}/deduplog.cpp PROPERTIES
    COMPILE_FLAGS "-std=c++11"
)

# 4. Apply the Dynamic Linker (LDFLAGS) wrapper flags
target_link_options(deduplog_test PRIVATE
    "-pthread"
    "LINKER:--wrap=time"
    "LINKER:--wrap=write"
    "LINKER:--wrap=writev"
    "LINKER:--wrap=vfprintf"
    "LINKER:--wrap=__vfprintf_chk"
    "LINKER:--wrap=fflush"
    "LINKER:--wrap=openlog"
    "LINKER:--wrap=vsyslog"
)

# 5. Bind GoogleTest, GoogleMock, and System library requirements (LDLIBS)
# NOTE: "deduplog" library target is intentionally removed here!
target_link_libraries(deduplog_test PRIVATE
    ${GTEST_LIBRARIES}
    ${GMOCK_LIBRARIES}
    ${SYSTEMD_LIB}   # Include this so deduplog.cpp source can resolve journald flags if present
    pthread
    dl
)

# Register the target package with CTest suite
add_test(NAME RunUnitTests COMMAND deduplog_test)

