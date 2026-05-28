# Script to generate version information at build time
# This runs every time the project is built

# Get git version information
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_VERSION_RESULT
)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_HASH_RESULT
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_BRANCH_RESULT
)

# Get build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)

# Set defaults if git commands failed
if(GIT_VERSION_RESULT OR NOT GIT_VERSION)
    set(GIT_VERSION "unknown")
endif()

if(GIT_HASH_RESULT OR NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()

if(GIT_BRANCH_RESULT OR NOT GIT_BRANCH)
    set(GIT_BRANCH "unknown")
endif()

# Generate the header file content
set(VERSION_HEADER_CONTENT
"#pragma once

// Auto-generated version information
// This file is generated at build time - do not edit manually

#define GIT_VERSION \"${GIT_VERSION}\"
#define GIT_COMMIT_HASH \"${GIT_COMMIT_HASH}\"
#define GIT_BRANCH \"${GIT_BRANCH}\"
#define BUILD_TIMESTAMP \"${BUILD_TIMESTAMP}\"
"
)

# Write the header file
file(WRITE ${BINARY_DIR}/version_info.h "${VERSION_HEADER_CONTENT}")

# Optional: Print version info during build
message(STATUS "Generated version info: ${GIT_VERSION} (${GIT_COMMIT_HASH}) on ${GIT_BRANCH}") 