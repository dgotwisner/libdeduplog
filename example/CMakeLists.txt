cmake_minimum_required(VERSION 2.8.12.2...3.25)

# Ensure the examples can resolve headers correctly
include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")

# ==============================================================================
# C++ Test Binary
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test.cpp")
    add_executable(test test.cpp)
    target_link_libraries(test deduplog)
endif()

# ==============================================================================
# C Test Binary
# ==============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ctest.c")
    add_executable(ctest ctest.c)
    target_link_libraries(ctest deduplog)
    
    # Expose POSIX and standard string declarations natively (resolves strlen/sleep)
    if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_link_libraries(ctest stdc++)
        set_property(TARGET ctest APPEND_STRING PROPERTY COMPILE_FLAGS " -D_GNU_SOURCE")
    endif()
endif()

# ==============================================================================
# Optional: Install Example Executables
# ==============================================================================
if(TARGET test)
    install(TARGETS test RUNTIME DESTINATION bin)
endif()
if(TARGET ctest)
    install(TARGETS ctest RUNTIME DESTINATION bin)
endif()

