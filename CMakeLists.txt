cmake_minimum_required(VERSION 3.25)
project(PlatformUtils VERSION 1.0.0.0 LANGUAGES CXX)

set(SRC_DIR "${PROJECT_SOURCE_DIR}/Source")

add_library(${PROJECT_NAME}
   "${SRC_DIR}/PlatformUtils/IOUtils.cpp"
   "${SRC_DIR}/PlatformUtils/IOUtils.h"
   "${SRC_DIR}/PlatformUtils/OSUtils_Common.cpp"
   "${SRC_DIR}/PlatformUtils/OSUtils.h"
)

if(WIN32)
   target_sources(${PROJECT_NAME} PRIVATE
      "${SRC_DIR}/PlatformUtils/OSUtils_Windows.cpp"
   )
elseif(APPLE)
   target_sources(${PROJECT_NAME} PRIVATE
      "${SRC_DIR}/PlatformUtils/OSUtils_macOS.mm"
      "${SRC_DIR}/PlatformUtils/OSUtils_POSIX.cpp"
   )
   target_compile_options(${PROJECT_NAME} PUBLIC "-xobjective-c++")
   target_link_libraries(${PROJECT_NAME} PUBLIC "-framework CoreServices -framework Foundation")
elseif(LINUX)
   target_sources(${PROJECT_NAME} PRIVATE
      "${SRC_DIR}/PlatformUtils/OSUtils_Linux.cpp"
      "${SRC_DIR}/PlatformUtils/OSUtils_POSIX.cpp"
   )
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PUBLIC ${SRC_DIR})
set_property(DIRECTORY ${CMAKE_PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

get_target_property(SOURCE_FILES ${PROJECT_NAME} SOURCES)
source_group(TREE ${SRC_DIR} PREFIX Source FILES ${SOURCE_FILES})
