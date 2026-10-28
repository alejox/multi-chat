# Pruebas de los parsers de chat, sin OBS ni red:
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests
cmake_minimum_required(VERSION 3.22)
project(multichat-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

set(src "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_executable(
  test-chat
  test-chat.cpp
  fixtures.cpp
  net-stub.cpp
  ${src}/chat/message.cpp
  ${src}/chat/connector.cpp
  ${src}/chat/platforms.cpp
  ${src}/chat/twitch.cpp
  ${src}/chat/kick.cpp
  ${src}/chat/youtube.cpp
  ${src}/chat/tiktok.cpp
)
target_link_libraries(test-chat PRIVATE Qt6::Core Qt6::Test)
if(MSVC)
  target_compile_options(test-chat PRIVATE /utf-8 /W4)
else()
  target_compile_options(test-chat PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME test-chat COMMAND test-chat)
