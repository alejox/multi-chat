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
  ${src}/chat/starvios.cpp
  ${src}/chat/updates.cpp
)
target_link_libraries(test-chat PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(test-chat PRIVATE MULTICHAT_LOCALE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data/locale"
                                            MULTICHAT_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable(test-tts test-tts.cpp ${src}/chat/message.cpp ${src}/tts/notification-sound.cpp ${src}/tts/speech-text.cpp
                        ${src}/tts/voice-engine.cpp)
target_link_libraries(test-tts PRIVATE Qt6::Core Qt6::Test)

add_executable(test-overlay test-overlay.cpp ${src}/overlay-presets.cpp)
target_link_libraries(test-overlay PRIVATE Qt6::Core Qt6::Test)

# ChatManager without OBS: fake connectors, clock and config path (see test-manager.cpp).
add_executable(
  test-manager
  test-manager.cpp
  net-stub.cpp
  ${src}/chat-manager.cpp
  ${src}/config-store.cpp
  ${src}/chat/message.cpp
  ${src}/chat/connector.cpp
  ${src}/chat/platforms.cpp
  ${src}/chat/twitch.cpp
  ${src}/chat/kick.cpp
  ${src}/chat/youtube.cpp
  ${src}/chat/tiktok.cpp
  ${src}/chat/starvios.cpp
)
target_link_libraries(test-manager PRIVATE Qt6::Core Qt6::Test)

foreach(test_target test-chat test-tts test-overlay test-manager)
  if(MSVC)
    target_compile_options(${test_target} PRIVATE /utf-8 /W4)
  else()
    target_compile_options(${test_target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

enable_testing()
add_test(NAME test-chat COMMAND test-chat)
add_test(NAME test-tts COMMAND test-tts)
add_test(NAME test-overlay COMMAND test-overlay)
add_test(NAME test-manager COMMAND test-manager)
