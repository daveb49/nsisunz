cmake_minimum_required(VERSION 3.20)
project(nsisunz_unicode LANGUAGES C CXX)

# Force UNICODE for NSIS
add_definitions(-DUNICODE -D_UNICODE)

# Set output directories for binaries and libraries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Add minizip-ng as a subdirectory ---
# Assumes minizip-ng source is in a subfolder
add_subdirectory(minizip-ng)

# --- Copy official NSIS plugin API lib into build tree ---
set(NSIS_PLUGIN_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../../nsis-3.11-src/build/urelease/api/nsis/pluginapi-amd64-unicode.lib")
set(NSIS_PLUGIN_LIB_DEST "${CMAKE_CURRENT_BINARY_DIR}/libs")

if(NOT EXISTS "${NSIS_PLUGIN_LIB_DEST}/pluginapi-amd64-unicode.lib")
    file(MAKE_DIRECTORY "${NSIS_PLUGIN_LIB_DEST}")
    file(COPY "${NSIS_PLUGIN_LIB}" DESTINATION "${NSIS_PLUGIN_LIB_DEST}")
endif()

# --- Define the nsisunz plugin target ---
add_library(nsisunz_unicode SHARED
    nsisunz.cpp
    # add other source files if needed
)

# --- Link libraries ---
target_link_libraries(nsisunz_unicode PRIVATE
    minizip
    zlibstatic
    "${NSIS_PLUGIN_LIB_DEST}/pluginapi-amd64-unicode.lib"
)

# Optional: set C++ standard
set_target_properties(nsisunz_unicode PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
