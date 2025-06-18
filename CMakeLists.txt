cmake_minimum_required(VERSION 3.10)
project(rbus-tr181)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-typedef-redefinition -Wno-unused-value -fno-asynchronous-unwind-tables -ffunction-sections -I ${CMAKE_SOURCE_DIR} -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-typedef-redefinition -Wno-unused-value -fno-asynchronous-unwind-tables -ffunction-sections -I ${CMAKE_SOURCE_DIR}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lm -ldl")
#add_definitions(-D_POSIX_C_SOURCE=200809L)
#add_definitions(-D_XOPEN_SOURCE=500)
#add_definitions(-D__USE_GNU)

if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -framework IOKit -framework CoreFoundation -Wno-unused-command-line-argument")
    # Adding debug flags to MAC builds by default
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fno-inline -O0 -g3 -ggdb3")
endif()


find_library(RBUS_LIBRARY NAMES rbus HINTS "/usr/lib" "/usr/local/lib" "/opt/homebrew/lib")
find_library(RBUS_CORE_LIBRARY NAMES rbuscore HINTS "/usr/lib" "/usr/local/lib" "/opt/homebrew/lib")
find_path(RBUS_INCLUDE_DIR NAMES rbus.h HINTS "/usr/include/rbus" "/usr/local/include/rbus" "/opt/homebrew/include/rbus")
find_path(RTMSG_INCLUDE_DIR NAMES rtMessage.h HINTS "/usr/include/rtmessage" "/usr/local/include/rtmessage" "/opt/homebrew/include/rtmessage")

find_library(CJSON_LIBRARY NAMES cjson)
find_path(CJSON_INCLUDE_DIR
  NAMES cJSON.h
  PATHS
    /opt/homebrew/include
    /usr/include
    /usr/local/include    
  PATH_SUFFIXES
    include
    include/cjson
    cjson
  NO_DEFAULT_PATH
)

if(NOT CJSON_LIBRARY OR NOT CJSON_INCLUDE_DIR)
   message(FATAL_ERROR "cjson library not found")
endif()

add_executable(rbus_tr181 ${CMAKE_SOURCE_DIR}/tr181-datamodels.c)
target_include_directories(rbus_tr181 PRIVATE ${RBUS_INCLUDE_DIR} ${RTMSG_INCLUDE_DIR} ${CJSON_INCLUDE_DIR})
target_link_libraries(rbus_tr181 PRIVATE ${RBUS_LIBRARY} ${CJSON_LIBRARY})
file(COPY ${CMAKE_SOURCE_DIR}/datamodels.json DESTINATION ${CMAKE_BINARY_DIR})
