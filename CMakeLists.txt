cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# El dock, la ventana de chats y los eventos de emisión usan la API del frontend y Qt.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/chat-manager.cpp
    src/chat-manager.hpp
    src/overlay-source.cpp
    src/overlay-source.hpp
    src/chat/connector.cpp
    src/chat/connector.hpp
    src/chat/message.cpp
    src/chat/message.hpp
    src/chat/platforms.cpp
    src/chat/platforms.hpp
    src/chat/twitch.cpp
    src/chat/kick.cpp
    src/chat/youtube.cpp
    src/chat/tiktok.cpp
    src/net/net.hpp
    src/ui/chat-dock.cpp
    src/ui/chat-dock.hpp
    src/ui/flow-layout.cpp
    src/ui/flow-layout.hpp
    src/ui/image-cache.cpp
    src/ui/image-cache.hpp
    src/ui/settings-dialog.cpp
    src/ui/ui-common.cpp
    src/ui/ui-common.hpp
)

# Red nativa del sistema: OBS no instala los plugins TLS de Qt, así que QtNetwork no
# puede usar HTTPS dentro de OBS.
if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net/net-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /utf-8)
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/net/net-mac.mm)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
