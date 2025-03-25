add_library (ezm)
add_library(Easypap::ezm ALIAS ezm)

target_compile_options(ezm PRIVATE -Wall -Wno-unused-function)

# tune some flags for GCC or CLANG
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ezm PRIVATE -march=native)
endif()

# ezm needs ezv
target_link_libraries(ezm PUBLIC ezv)

if(ENABLE_TRACE AND TARGET ezm)
  find_package(FXT REQUIRED)
  target_compile_definitions(ezm
      PUBLIC ENABLE_TRACE
      PRIVATE ENABLE_FUT)
  target_link_libraries(ezm PUBLIC FXT::FXT)
endif()

target_include_directories(ezm
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/ezm>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>
)

# define content of the library
target_sources(ezm
    PRIVATE
        src/ezm.c src/ezm_footprint.c src/ezm_perfmeter.c src/ezm_time.c
        $<$<BOOL:${ENABLE_TRACE}>:src/ezm_tracerec.c>)

# installation
install(EXPORT ezmTargets
        COMPONENT ezm
        FILE ezmTargets.cmake
        NAMESPACE Easypap::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap)

install(TARGETS ezm
        COMPONENT ezm
        EXPORT ezmTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
        DESTINATION include/ezm
        COMPONENT ezm)

# Generate ezm.pc for pkg-config
get_target_property(COMPILE_DEFINITIONS ezm INTERFACE_COMPILE_DEFINITIONS)

# Transform COMPILE_DEFINITIONS into flags
set(CMAKE_EXPORTED_CFLAGS "")
foreach(def ${COMPILE_DEFINITIONS})
    set(CMAKE_EXPORTED_CFLAGS "${CMAKE_EXPORTED_CFLAGS} -D${def}")
endforeach()
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ezm.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/ezm.pc
  @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ezm.pc
  DESTINATION lib/pkgconfig
  COMPONENT ezm
)

# package config
# See https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html
include(CMakePackageConfigHelpers)

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/ezmConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ezmConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)

# Install config files
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ezmConfig.cmake
    COMPONENT ezm
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap
)

# Easyview trace visualization utility
if (ENABLE_TRACE AND TARGET ezm)
    add_subdirectory(view)
endif()
