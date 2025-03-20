add_library(ezv)
add_library(Easypap::ezv ALIAS ezv)

target_compile_options(ezv 
    PUBLIC -DGL_SILENCE_DEPRECATION
    PRIVATE -Wall -Wno-unused-function)

# tune some flags for GCC or CLANG
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ezv PRIVATE -march=native)
endif()

#find dependencies
find_package(SDL3 REQUIRED)
find_package(OpenGL 3 REQUIRED)
find_package(cglm REQUIRED)

target_link_libraries(ezv
    PUBLIC
        SDL3::SDL3 OpenGL::GL
    PRIVATE
        cglm::cglm
)

if(ENABLE_SCOTCH AND TARGET ezv)
    find_package(SCOTCH QUIET)
    if(NOT SCOTCH_FOUND)
        message(STATUS "Scotch library not found. Disabling ENABLE_SCOTCH option.")
        set(ENABLE_SCOTCH OFF)
    else()
        message(STATUS "Scotch library found.")
        target_link_libraries(ezv PUBLIC SCOTCH::scotch SCOTCH::scotcherr) 
        target_compile_definitions(ezv PRIVATE ENABLE_SCOTCH)
    endif()
endif()

target_include_directories(ezv
  PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/ezv>
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>
)

# Pass the installation directory as a definition
target_compile_definitions(ezv PRIVATE INSTALL_DIR="${CMAKE_INSTALL_PREFIX}")
target_compile_definitions(ezv INTERFACE EZP_INSTALLED)

# define content of the library
target_sources(ezv PRIVATE
    src/ezv.c                       src/ezv_mon_object.c            src/glad.c                      src/mon_renderer.c
    src/ezv_boolmat.c               src/ezv_palette.c               src/img2d_obj.c                 src/stb_image.c
    src/ezv_event.c                 src/ezv_prefix.c                src/img2d_renderer.c            src/tinyobj_loader_c.c
    src/ezv_hud.c                   src/ezv_sdl_gl.c                src/mesh3d_obj.c
    src/ezv_img2d_object.c          src/ezv_shader.c                src/mesh3d_renderer.c
    src/ezv_mesh3d_object.c         src/ezv_virtual.c               src/mon_obj.c
)

# to have a local access in build to shaders and img
file(COPY share DESTINATION ${CMAKE_BINARY_DIR})

# installation
install(EXPORT ezvTargets
        COMPONENT ezv
        FILE ezvTargets.cmake
        NAMESPACE Easypap::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap)

install(TARGETS ezv
        COMPONENT ezv
        EXPORT ezvTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}) 

install(DIRECTORY include/ 
        DESTINATION include/ezv
        COMPONENT ezv)
install(DIRECTORY share/
        DESTINATION share
        COMPONENT ezv)


# Generate ezv.pc for pkg-config
get_target_property(COMPILE_DEFINITIONS ezv INTERFACE_COMPILE_DEFINITIONS)

# Transform COMPILE_DEFINITIONS into flags
set(CMAKE_EXPORTED_CFLAGS "")
foreach(def ${COMPILE_DEFINITIONS})
    set(CMAKE_EXPORTED_CFLAGS "${CMAKE_EXPORTED_CFLAGS} -D${def}")
endforeach()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ezv.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/ezv.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ezv.pc
        DESTINATION lib/pkgconfig
        COMPONENT ezv
)

# package config
# See https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html
include(CMakePackageConfigHelpers)

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/ezvConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ezvConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)

# Install config files
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ezvConfig.cmake
    COMPONENT ezv
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap)
