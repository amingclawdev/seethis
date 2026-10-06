# Store-only compilation of fixed assets; original disk path remains the default.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(settings_html "${CMAKE_CURRENT_SOURCE_DIR}/web/settings/index.html")
set(settings_js "${CMAKE_CURRENT_SOURCE_DIR}/web/settings/settings.js")
set(settings_generator "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_settings_assets.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${settings_html}" "${settings_js}" "${settings_generator}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${settings_generator}"
  --html "${settings_html}" --javascript "${settings_js}"
  --output "${CMAKE_CURRENT_BINARY_DIR}/generated/seethis/settings_assets.h"
  COMMAND_ERROR_IS_FATAL ANY)
