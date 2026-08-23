cmake_minimum_required(VERSION 3.16)
project(OpticTrigeminal VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE KERNEL_SOURCES "src/kernel/*.cpp")
file(GLOB_RECURSE CLINICAL_SOURCES "src/clinical/*.cpp")
file(GLOB_RECURSE SERVER_SOURCES "src/server/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h")

# Exclude CLI from server build
list(FILTER SERVER_SOURCES EXCLUDE REGEX ".*cli.*")

# The frontend build is embedded directly into the server binary (see
# include/embedded_web_assets.h) so it's a genuinely single, self-contained
# executable -- no web/dist/ directory has to travel with it. Excluded from
# the glob above and re-added explicitly below: it may not exist yet at
# configure time (glob would silently omit it), and this custom command
# regenerates it from web/dist/ (run `npm run build` in web/ first) before
# every build regardless.
list(REMOVE_ITEM SERVER_SOURCES "${PROJECT_SOURCE_DIR}/src/server/embedded_web_assets.cpp")
set(EMBEDDED_ASSETS_CPP "${PROJECT_SOURCE_DIR}/src/server/embedded_web_assets.cpp")
add_custom_command(
    OUTPUT ${EMBEDDED_ASSETS_CPP}
    COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/embed_web_assets.py"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    DEPENDS "${PROJECT_SOURCE_DIR}/scripts/embed_web_assets.py"
    COMMENT "Embedding web/dist/ into the server binary"
    VERBATIM
)
list(APPEND SERVER_SOURCES ${EMBEDDED_ASSETS_CPP})

add_executable(optic-trigeminal ${KERNEL_SOURCES} ${CLINICAL_SOURCES} ${SERVER_SOURCES} ${HEADERS})

target_include_directories(optic-trigeminal PRIVATE 
    ${PROJECT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(optic-trigeminal PRIVATE Threads::Threads)

message(STATUS "OpticTrigeminal C++ Build Configuration")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler Flags: ${CMAKE_CXX_FLAGS}")
