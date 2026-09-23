cmake_minimum_required(VERSION 3.21)
project(safeparts_desktop LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.5" CACHE STRING "Development-host deployment target" FORCE)
endif()

find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets Test)
# Keep headers aligned with the selected Qt package when another Qt installation
# exists in the compiler's implicit /usr/local include search path.
get_filename_component(QT_SELECTED_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
include_directories(BEFORE "${QT_SELECTED_PREFIX}/include")
add_compile_options("-I${QT_SELECTED_PREFIX}/include")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GENERATED_DIR "${REPO_ROOT}/target/desktop-generated")
set(BRIDGE_SOURCE "${REPO_ROOT}/crates/safeparts_desktop_bridge/src/bridge.rs")
set(RUST_LIBRARY "${REPO_ROOT}/target/release/${CMAKE_STATIC_LIBRARY_PREFIX}safeparts_desktop_bridge${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_custom_command(
  OUTPUT "${GENERATED_DIR}/bridge.rs.cc" "${GENERATED_DIR}/bridge.rs.h" "${GENERATED_DIR}/rust/cxx.h"
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_bridge.sh" "${REPO_ROOT}" "${GENERATED_DIR}"
  DEPENDS "${BRIDGE_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_bridge.sh" "${REPO_ROOT}/Cargo.lock"
  VERBATIM
)
add_custom_target(desktop-bridge-generated DEPENDS
  "${GENERATED_DIR}/bridge.rs.cc" "${GENERATED_DIR}/bridge.rs.h" "${GENERATED_DIR}/rust/cxx.h")

add_custom_target(desktop-rust ALL
  COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
          mise exec -- cargo build --release --locked -p safeparts_desktop_bridge
  WORKING_DIRECTORY "${REPO_ROOT}"
  BYPRODUCTS "${RUST_LIBRARY}"
  VERBATIM
)
add_dependencies(desktop-rust desktop-bridge-generated)
add_library(safeparts-rust STATIC IMPORTED GLOBAL)
set_target_properties(safeparts-rust PROPERTIES IMPORTED_LOCATION "${RUST_LIBRARY}")
add_dependencies(safeparts-rust desktop-rust)

add_library(desktop-ui STATIC
  src/clipboard_write.cpp
  src/desktop_window.cpp
  src/desktop_window.h
  src/exact_text_edit.cpp
  src/exact_text_edit.h
  src/file_io.cpp
  src/file_io.h
  src/rust_worker.cpp
  src/rust_worker.h
  src/secure_byte_buffer.cpp
  src/secure_byte_buffer.h
  src/segmented_share_view.cpp
  src/segmented_share_view.h
  "${GENERATED_DIR}/bridge.rs.cc"
)
if(APPLE)
  target_sources(desktop-ui PRIVATE src/clipboard_mac.mm)
  target_link_libraries(desktop-ui PRIVATE "-framework AppKit")
else()
  target_sources(desktop-ui PRIVATE src/clipboard_qt.cpp)
endif()
add_dependencies(desktop-ui desktop-rust)
target_include_directories(desktop-ui PUBLIC src "${GENERATED_DIR}")
set_target_properties(desktop-ui PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
target_link_libraries(desktop-ui PUBLIC Qt6::Widgets safeparts-rust)
target_compile_options(desktop-ui PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(safeparts-desktop src/main.cpp)
set_target_properties(safeparts-desktop PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
target_link_libraries(safeparts-desktop PRIVATE desktop-ui)
target_compile_options(safeparts-desktop PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
  add_executable(desktop-bridge-contract tests/bridge_contract.cpp "${GENERATED_DIR}/bridge.rs.cc")
  add_dependencies(desktop-bridge-contract desktop-rust)
  target_include_directories(desktop-bridge-contract PRIVATE "${GENERATED_DIR}")
  target_link_libraries(desktop-bridge-contract PRIVATE safeparts-rust)
  target_compile_options(desktop-bridge-contract PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_definitions(desktop-bridge-contract PRIVATE SAFEPARTS_REPO_ROOT="${REPO_ROOT}")
  add_test(NAME desktop-bridge-contract COMMAND desktop-bridge-contract)

  add_executable(desktop-file-io tests/file_io.cpp src/file_io.cpp src/secure_byte_buffer.cpp)
  set_target_properties(desktop-file-io PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
  target_include_directories(desktop-file-io PRIVATE src)
  target_link_libraries(desktop-file-io PRIVATE Qt6::Test)
  target_compile_options(desktop-file-io PRIVATE -Wall -Wextra -Wpedantic -Werror
    -Wno-c++20-attribute-extensions -Wno-variadic-macro-arguments-omitted)
  add_test(NAME desktop-file-io COMMAND desktop-file-io)

  add_executable(desktop-actions tests/desktop_actions.cpp)
  set_target_properties(desktop-actions PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
  target_link_libraries(desktop-actions PRIVATE desktop-ui Qt6::Test)
  target_compile_options(desktop-actions PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_definitions(desktop-actions PRIVATE SAFEPARTS_REPO_ROOT="${REPO_ROOT}")
  add_test(NAME desktop-actions COMMAND desktop-actions)
  set_tests_properties(desktop-actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
endif()
