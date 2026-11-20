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

# Network: solo para la página local del inicio de sesión de YouTube (TCP sin TLS; OBS ya trae QtNetwork).
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

# Cliente de Google del plugin (OAuth «App de escritorio»), para que el usuario solo pulse «Iniciar
# sesión con Google». Llega de las variables de entorno MULTICHAT_GOOGLE_CLIENT_ID y
# MULTICHAT_GOOGLE_CLIENT_SECRET (secretos del repositorio en GitHub Actions). Sin ellas, el plugin
# compila igual y cada usuario usa su propia app de Google Cloud. Google no considera confidencial el
# secreto de un cliente de escritorio, pero no se guarda en el repositorio.
set(_google_client_id "$ENV{MULTICHAT_GOOGLE_CLIENT_ID}")
set(_google_client_secret "$ENV{MULTICHAT_GOOGLE_CLIENT_SECRET}")
if(_google_client_id AND _google_client_secret)
  if(NOT _google_client_id MATCHES "^[A-Za-z0-9._-]+$" OR NOT _google_client_secret MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "MULTICHAT_GOOGLE_CLIENT_ID o MULTICHAT_GOOGLE_CLIENT_SECRET tienen caracteres no válidos")
  endif()
  target_compile_definitions(
    ${CMAKE_PROJECT_NAME}
    PRIVATE MULTICHAT_GOOGLE_CLIENT_ID="${_google_client_id}" MULTICHAT_GOOGLE_CLIENT_SECRET="${_google_client_secret}"
  )
  message(STATUS "obs-multichat: con el cliente de Google incluido")
else()
  message(STATUS "obs-multichat: sin cliente de Google incluido (cada usuario usa el suyo)")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/chat-manager.cpp
    src/chat-manager.hpp
    src/chat-history.cpp
    src/chat-history.hpp
    src/config-store.cpp
    src/config-store.hpp
    src/featured-state.cpp
    src/featured-state.hpp
    src/i18n.cpp
    src/i18n.hpp
    src/overlay-presets.cpp
    src/overlay-presets.hpp
    src/overlay/box-measure.cpp
    src/overlay/box-measure.hpp
    src/overlay/config.cpp
    src/overlay/overlay-source.hpp
    src/overlay/properties.cpp
    src/overlay/source-internal.hpp
    src/overlay/source.cpp
    src/overlay/floating-window.cpp
    src/overlay/floating-window.hpp
    src/overlay/viewers-layout.cpp
    src/overlay/wiring.cpp
    src/platforms-registry.hpp
    src/updater.cpp
    src/updater.hpp
    src/updater/install.hpp
    src/chat/connector.cpp
    src/chat/connector.hpp
    src/chat/message.cpp
    src/chat/message.hpp
    src/chat/platforms.cpp
    src/chat/platforms.hpp
    src/chat/twitch.cpp
    src/chat/kick.cpp
    src/chat/youtube.cpp
    src/chat/youtube-api.cpp
    src/chat/youtube-api.hpp
    src/chat/tiktok.cpp
    src/chat/starvios.cpp
    src/chat/updates.cpp
    src/chat/updates.hpp
    src/net/net.hpp
    src/tts/bounded-queue.hpp
    src/tts/notification-sound.cpp
    src/tts/notification-sound.hpp
    src/tts/pcm.hpp
    src/tts/speech-backend.hpp
    src/tts/speech-text.cpp
    src/tts/speech-text.hpp
    src/tts/voice-engine.cpp
    src/tts/voice-engine.hpp
    src/tts/voice-settings.cpp
    src/tts/voice-settings.hpp
    src/ui/channel-input.cpp
    src/ui/channel-input.hpp
    src/ui/chat-dock.cpp
    src/ui/chat-dock.hpp
    src/ui/chat-list.cpp
    src/ui/chat-list.hpp
    src/ui/chat-view.cpp
    src/ui/chat-view.hpp
    src/ui/dock-prefs.cpp
    src/ui/dock-prefs.hpp
    src/ui/first-run-wizard.cpp
    src/ui/first-run-wizard.hpp
    src/ui/flow-layout.cpp
    src/ui/flow-layout.hpp
    src/ui/image-cache.cpp
    src/ui/image-cache.hpp
    src/ui/profile-cache.cpp
    src/ui/profile-cache.hpp
    src/ui/settings-dialog.cpp
    src/ui/settings-dialog.hpp
    src/ui/support-dialog.cpp
    src/ui/youtube-login.cpp
    src/ui/youtube-login.hpp
    src/ui/support-dialog.hpp
    src/ui/ui-common.cpp
    src/ui/ui-common.hpp
)

# Red nativa del sistema: OBS no instala los plugins TLS de Qt en Windows ni macOS, así que
# QtNetwork no puede usar HTTPS dentro de OBS.
if(OS_WINDOWS)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE src/net/net-win.cpp src/tts/speech-backend-win.cpp src/tts/voice-source.cpp src/tts/voice-source.hpp
            src/updater/install-win.cpp src/updater/install-script-win.hpp
  )
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp shell32 sapi ole32)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /utf-8)

  # Prueba del sintetizador real de SAPI (ver tests/test-tts-win.cpp). Va aquí y no en tests/ porque
  # aquí ya están el Qt de .deps y la configuración del job de Windows; ese Qt no trae QtTest.
  add_executable(test-tts-win EXCLUDE_FROM_ALL tests/test-tts-win.cpp src/tts/speech-backend-win.cpp)
  target_link_libraries(test-tts-win PRIVATE Qt6::Core sapi ole32)
  target_compile_options(test-tts-win PRIVATE /utf-8)
  add_custom_command(
    TARGET test-tts-win
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:test-tts-win> $<TARGET_FILE_DIR:test-tts-win>
    COMMAND_EXPAND_LISTS
  )
elseif(OS_MACOS)
  enable_language(OBJCXX)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE src/net/net-mac.mm src/tts/speech-backend-mac.mm src/tts/voice-source.cpp src/tts/voice-source.hpp
            src/updater/install-mac.cpp src/updater/copy-tree.cpp src/overlay/floating-capture-mac.mm
  )
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation.framework>" "$<LINK_LIBRARY:FRAMEWORK,AVFoundation.framework>"
            "$<LINK_LIBRARY:FRAMEWORK,AppKit.framework>"
  )
else()
  # En Linux, Qt sí trae TLS, pero libcurl ya la tiene cualquier OBS. La voz usa espeak-ng, que se
  # carga al usarla (dlopen): aquí solo hacen falta sus cabeceras.
  find_package(CURL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ESPEAK_NG REQUIRED IMPORTED_TARGET espeak-ng)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE src/net/net-linux.cpp src/tts/speech-backend-linux.cpp src/tts/voice-source.cpp src/tts/voice-source.hpp
            src/updater/install-linux.cpp src/updater/copy-tree.cpp
  )
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${ESPEAK_NG_INCLUDE_DIRS})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl ${CMAKE_DL_LIBS})
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
