# CivicFix Pakistan — C++ REST API (zero-dependency except SQLite).
#
# Build with CMake (3.16+):
#   cmake -S backend-cpp -B backend-cpp/build
#   cmake --build backend-cpp/build
# The binary is backend-cpp/build/civicfix_api. Run it as:
#   ./backend-cpp/build/civicfix_api 8080 database/api_test.db
#
# SQLite: this file first tries find_package(SQLite3), which works
# where the SQLite development package is installed
# (e.g. `sudo apt install libsqlite3-dev`, or a vcpkg install).
# If that fails, point CMake at your SQLite install explicitly:
#   cmake -S backend-cpp -B backend-cpp/build \
#     -DSQLite3_INCLUDE_DIR=<dir with sqlite3.h> \
#     -DSQLite3_LIBRARY=<path to sqlite3.lib / libsqlite3.so>
# On Windows you can also keep using the VS Code task
# "Build CivicFix API", which links -lsqlite3 -lws2_32 directly.

cmake_minimum_required(VERSION 3.16)
project(CivicFixPakistan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(civicfix_api
  src/server.cpp
  src/models.cpp
  src/ComplaintQueue.cpp
  src/ActionStack.cpp
  src/StatusHistory.cpp
  src/Graph.cpp
  src/Sorting.cpp
  src/Database.cpp
)

target_include_directories(civicfix_api PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  find_path(SQLite3_INCLUDE_DIR NAMES sqlite3.h)
  find_library(SQLite3_LIBRARY NAMES sqlite3)
  if(SQLite3_INCLUDE_DIR AND SQLite3_LIBRARY)
    add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
      IMPORTED_LOCATION "${SQLite3_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}")
    set(SQLite3_FOUND TRUE)
  endif()
endif()
if(NOT SQLite3_FOUND)
  message(FATAL_ERROR
    "SQLite3 not found. Install the SQLite development package, or point "
    "CMake at your install with -DSQLite3_INCLUDE_DIR=<dir> "
    "-DSQLite3_LIBRARY=<file>.")
endif()
target_link_libraries(civicfix_api PRIVATE SQLite::SQLite3)

if(WIN32)
  target_link_libraries(civicfix_api PRIVATE ws2_32)
else()
  target_link_libraries(civicfix_api PRIVATE pthread)
endif()
