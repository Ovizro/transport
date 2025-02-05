cmake_minimum_required(VERSION 3.15)

project(transport CXX)

set(CMAKE_CXX_STANDARD 11)
if (NOT CMAKE_CROSSCOMPILING)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/bin)
    set(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/lib)
endif()

if (UNIX)
    set(CMAKE_CXX_FLAGS "-std=c++11 -Wall ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g ${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "-g -O2 ${CMAKE_CXX_FLAGS}")
elseif (WIN32)
    # windows platform
    #add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    #set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /EHsc")
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()

if (CMAKE_CROSSCOMPILING)
    message(STATUS "Cross compiling ...")
    message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
    message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

include_directories(./include)

aux_source_directory(${CMAKE_SOURCE_DIR}/src SRC_LIST)

link_libraries(pthread util)

add_library(transport_static STATIC ${SRC_LIST})
add_library(transport SHARED ${SRC_LIST})
set_target_properties(transport_static PROPERTIES OUTPUT_NAME transport)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(transport PRIVATE -Wno-nonnull)
endif()

add_subdirectory(tests)
