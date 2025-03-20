
# Easyview trace visualization utility
add_executable(easyview)

# tune some flags for GCC or CLANG
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(easyview PRIVATE -march=native)
endif()

target_sources(easyview
  PRIVATE
    src/main.c
    src/trace_colors.c
    src/trace_data.c
    src/trace_file.c
    src/trace_graphics.c
)

#FIXME: we need ../../../include for perfcounter.h that we do not actually use
target_include_directories(easyview
  PRIVATE
    include
    ../include
    ../internal
    ../../../include
)

target_compile_definitions(easyview PRIVATE INSTALL_DIR="${CMAKE_INSTALL_PREFIX}")

find_package (SDL3 REQUIRED)
find_package (SDL3_image REQUIRED)
find_package (SDL3_ttf REQUIRED)

target_link_libraries(easyview
  PRIVATE
    Easypap::ezm SDL3::SDL3 SDL3_image::SDL3_image SDL3_ttf::SDL3_ttf
)

# to have a local access in build to shaders and img
file(COPY share DESTINATION ${CMAKE_BINARY_DIR})

install(TARGETS easyview
  COMPONENT ezm
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY share/
        DESTINATION share
        COMPONENT ezm)
