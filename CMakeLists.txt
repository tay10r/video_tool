cmake_minimum_required(VERSION 3.14.7)

project(video_tool
  DESCRIPTION "A tool for opening video files and quickly filtering the frames of interest."
  VERSION 0.1.0)

option(VIDEO_TOOL_INSTALL "Whether or not to define the install rules for the program." ON)

include(FetchContent)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(uikit URL "https://github.com/tay10r/uikit/archive/refs/tags/v0.1.3.zip")
FetchContent_MakeAvailable(uikit)

find_package(OpenCV REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_executable(video_tool WIN32 main.cpp)

target_link_libraries(video_tool
  PUBLIC
    uikit::uikit
    uikit::main
    spdlog::spdlog
    ${OpenCV_LIBS})

if(LABEL_TOOL_INSTALL)

  install(TARGETS video_tool
    DESTINATION bin)

  set (CPACK_PACKAGE_CONTACT "Taylor Holberton")
  set (CPACK_PACKAGE_DESCRIPTION_SUMMARY "${video_tool_DESCRIPTION}")
  set (CPACK_DEBIAN_PACKAGE_DEPENDS libgl1 libc6 libx11-6)

  include(CPack)

endif()
