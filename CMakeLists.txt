cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  # Linux : qt-display.cpp utilise un en-tête privé de Qt pour Wayland
  # (ENABLE_WAYLAND vient d'obsconfig.h). Avant Qt 6.9, la cible
  # Qt6::GuiPrivate est créée par le paquet Gui, sans composant séparé.
  if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
    if(NOT TARGET Qt6::GuiPrivate)
      find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    endif()
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
  endif()
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/manager.cpp
    src/manager.hpp
    src/destination.cpp
    src/destination.hpp
    src/studio-window.cpp
    src/studio-window.hpp
    src/tile-grid.cpp
    src/tile-grid.hpp
    src/scene-editor.cpp
    src/scene-editor.hpp
    src/render-helpers.cpp
    src/render-helpers.hpp
    src/dialogs.cpp
    src/dialogs.hpp
    src/audio-mixer.cpp
    src/audio-mixer.hpp
    src/text.cpp
    src/undo.cpp
    src/undo.hpp
    src/media-controls.cpp
    src/media-controls.hpp
    src/text.hpp
    src/default-strings.hpp
    src/blend.cpp
    src/blend.hpp
    src/blend-render.cpp
    src/blend-render.hpp
    src/blend-editor.cpp
    src/blend-editor.hpp
    src/reorder-list.hpp
    src/manage-destinations.cpp
    src/manage-destinations.hpp
    src/projector-window.cpp
    src/projector-window.hpp
    src/qt-display.cpp
    src/qt-display.hpp
    src/display-helpers.hpp
    src/cues.cpp
    src/cues.hpp
    src/cues-dock.cpp
    src/cues-dock.hpp
    src/dock-style.cpp
    src/dock-style.hpp
    src/remote-control.cpp
    src/remote-control.hpp
    src/obs-websocket-api.h
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
