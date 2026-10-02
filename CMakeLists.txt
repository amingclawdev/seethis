cmake_minimum_required(VERSION 3.25)

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Minimum macOS version for exact-window capture" FORCE)
endif()

# Honor explicit toolchain/compiler settings; otherwise use the selected Xcode or CLT.
if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
  foreach(language CXX OBJCXX)
    if(NOT DEFINED CMAKE_${language}_COMPILER AND "$ENV{${language}}" STREQUAL "")
      execute_process(COMMAND /usr/bin/xcrun --sdk macosx --find clang++
        OUTPUT_VARIABLE SEETHIS_APPLE_COMPILER OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
      set(CMAKE_${language}_COMPILER "${SEETHIS_APPLE_COMPILER}" CACHE FILEPATH "Selected Apple compiler")
    endif()
  endforeach()
  if(NOT CMAKE_OSX_SYSROOT)
    execute_process(COMMAND /usr/bin/xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE SEETHIS_APPLE_SDK OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_OSX_SYSROOT "${SEETHIS_APPLE_SDK}" CACHE PATH "Selected macOS SDK")
  endif()
endif()

project(seethis VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SEETHIS_HAS_MACOS_BACKEND 0)
set(SEETHIS_HAS_WINDOWS_BACKEND 0)
set(SEETHIS_HAS_LINUX_BACKEND 0)
if(APPLE)
  set(SEETHIS_HAS_MACOS_BACKEND 1)
endif()

configure_file(
  src/platform/build_config.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/generated/seethis/build_config.h"
)

add_library(seethis_core STATIC
  src/core/interaction.cc
  src/core/reference_store.cc
  src/core/mark_controller.cc
)
target_include_directories(seethis_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(seethis_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(seethis_core PUBLIC Threads::Threads ZLIB::ZLIB)

if(APPLE)
  enable_language(OBJCXX)
  target_sources(seethis_core PRIVATE src/core/settings.cc)
  add_library(seethis_service STATIC src/service/reference_server.cc)
  target_include_directories(seethis_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(seethis_service PUBLIC seethis_core Threads::Threads)
  target_compile_features(seethis_service PUBLIC cxx_std_20)
  add_executable(seethis MACOSX_BUNDLE
    src/main_mac.mm
    src/platform/mac/input_mac.mm
    src/platform/mac/overlay_mac.mm
    src/platform/mac/context_mac.mm
    src/platform/mac/capture_mac.mm
    src/platform/mac/clipboard_mac.mm
  )
  target_link_libraries(seethis PRIVATE
    seethis_service
    "-framework AppKit"
    "-framework ApplicationServices"
    "-framework Carbon"
    "-framework ScreenCaptureKit"
  )
  target_compile_options(seethis PRIVATE -fobjc-arc)
  target_sources(seethis PRIVATE web/settings/index.html web/settings/settings.js)
  set_source_files_properties(web/settings/index.html web/settings/settings.js
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/settings")
  target_sources(seethis PRIVATE assets/app-icon/SeeThis.icns)
  set_source_files_properties(assets/app-icon/SeeThis.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_target_properties(seethis PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "SeeThis"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.seethis.overlay"
    MACOSX_BUNDLE_INFO_STRING "SeeThis overlay foundation"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/seethis.entitlements"
  )
else()
  message(STATUS
    "SeeThis native app backend is unavailable on this platform; building the shared core only")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(interaction_test tests/interaction_test.cc)
  target_link_libraries(interaction_test PRIVATE seethis_core)
  add_test(NAME interaction_state_machine COMMAND interaction_test)
  add_executable(reference_store_test tests/reference_store_test.cc)
  target_link_libraries(reference_store_test PRIVATE seethis_core)
  if(APPLE)
    target_sources(reference_store_test PRIVATE
      src/platform/mac/capture_mac.mm
      src/platform/mac/context_mac.mm)
    target_compile_definitions(reference_store_test PRIVATE SEETHIS_CAPTURE_TESTING)
    target_compile_options(reference_store_test PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(reference_store_test PRIVATE
      "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation"
      "-framework AppKit" "-framework ApplicationServices" "-framework ScreenCaptureKit")
  endif()
  add_test(NAME reference_flow COMMAND reference_store_test)
  if(APPLE)
    add_executable(inspector_feedback_test tests/inspector_feedback_test.mm)
    target_compile_options(inspector_feedback_test PRIVATE -fobjc-arc)
    target_link_libraries(inspector_feedback_test PRIVATE seethis_core "-framework AppKit")
    add_test(NAME inspector_feedback_actions COMMAND inspector_feedback_test)
    add_executable(capture_exclusion_test tests/capture_exclusion_test.mm)
    target_compile_options(capture_exclusion_test PRIVATE -fobjc-arc)
    target_link_libraries(capture_exclusion_test PRIVATE seethis_core "-framework AppKit")
    add_test(NAME capture_window_exclusion COMMAND capture_exclusion_test)
    add_executable(chrome_consent_async_test
      tests/chrome_consent_async_test.mm
      src/platform/mac/context_mac.mm
      src/platform/mac/capture_mac.mm
      src/platform/mac/clipboard_mac.mm)
    target_compile_definitions(chrome_consent_async_test PRIVATE SEETHIS_CHROME_CONSENT_TESTING)
    target_compile_options(chrome_consent_async_test PRIVATE -fobjc-arc)
    target_link_libraries(chrome_consent_async_test PRIVATE seethis_service
      "-framework AppKit" "-framework ApplicationServices" "-framework Carbon"
      "-framework ScreenCaptureKit")
    add_test(NAME chrome_consent_async COMMAND chrome_consent_async_test)
    set_tests_properties(chrome_consent_async PROPERTIES TIMEOUT 30)
    add_executable(input_permission_test tests/input_permission_test.mm)
    target_link_libraries(input_permission_test PRIVATE seethis_core)
    add_test(NAME input_permission_states COMMAND input_permission_test)
    add_executable(reference_server_test tests/reference_server_test.cc)
    target_link_libraries(reference_server_test PRIVATE seethis_service)
    add_test(NAME reference_server_security COMMAND reference_server_test)
  endif()
endif()
