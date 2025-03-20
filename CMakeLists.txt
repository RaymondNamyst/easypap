cmake_minimum_required(VERSION 3.18)
project(Easypap VERSION 1.0.0)

# EasyPAP options
option(ENABLE_SCOTCH "Use Scotch library"           ON)
option(ENABLE_TRACE "Generate execution traces"     ON)
option(ENABLE_OPENCL "Compile with OpenCL"          ON)
option(ENABLE_CUDA "Compile with CUDA"              OFF)
option(ENABLE_SHA "Use SHA Hash library"            ON)
option(ENABLE_MPI "Compile with MPI"                OFF)
option(ENABLE_MIPP "Compile with MIPP"              ON)
# Turn OFF to only prepare for ezv and ezm installation
option(CONFIGURE_EASYPAP "Configure Easypap"        ON)

# General options
option(BUILD_SHARED_LIBS "Build shared libraries"   ON)


# cmake policies
cmake_policy(SET CMP0074 NEW) # to use MY_PACKAGE_ROOT if MY_PACKAGE is not in a standard place
cmake_policy(SET CMP0072 NEW) # to avoid prefer GLVND
cmake_policy(SET CMP0079 NEW) # allow to use target_link_libraries with full path to the library

# Ensure find_package prefers xxxConfig.cmake over Findxxx.cmake
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# set -fPIC flag when building shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})

# Add the path of the custom module
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# use GNU standard for file hierarchy installation system
include(GNUInstallDirs)

# RPATH stuff
# to set RPATH (to avoid patchelf use)
set(CMAKE_SKIP_BUILD_RPATH  FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

file(RELATIVE_PATH relDir
     ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
     ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_INSTALL_RPATH $ORIGIN $ORIGIN/${relDir})

# put libraries in the lib subdirectory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # static
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # dynamic

# put executables in the bin subdirectory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


# EZV and EZM
add_subdirectory(lib)

if (CONFIGURE_EASYPAP)
    # Main EasyPAP executable
    add_executable(easypap)

    target_compile_options(easypap PRIVATE -Wall -Wno-unused-function)

    # tune some flags for GCC or CLANG
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(easypap PRIVATE -march=native)
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(easypap PRIVATE -Wl,--export-dynamic)
    endif()

    target_sources(easypap
      PRIVATE
        src/arch_flags.c        src/ezp_alloc.c         src/img_data.c          src/mesh_mgpu.c         src/pthread_barrier.c
        src/debug.c             src/ezp_ctx.c           src/hash.c              src/main.c              src/pthread_distrib.c
        src/ez_pthread.c        src/ezp_monitoring.c    src/hooks.c             src/mesh_data.c)

    target_include_directories(easypap PRIVATE include internal)

    target_link_libraries(easypap PRIVATE Easypap::ezv Easypap::ezm) 

    # find dependencies
    find_package(OpenMP REQUIRED)
    target_link_libraries(easypap PRIVATE OpenMP::OpenMP_C) 

    find_package(cglm REQUIRED)
    target_link_libraries(easypap PRIVATE cglm::cglm)

    find_package(FLEX REQUIRED)

    set(LEX_FILE ${CMAKE_SOURCE_DIR}/src/rle_lexer.l)
    set(FLEX_GENERATED_C_FILE ${CMAKE_CURRENT_BINARY_DIR}/rle_lexer.c)
    add_custom_command(
      OUTPUT ${FLEX_GENERATED_C_FILE}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
      COMMAND flex -o${FLEX_GENERATED_C_FILE} ${LEX_FILE}
      DEPENDS ${LEX_FILE}
      COMMENT "Generating C file from ${LEX_FILE}"
      VERBATIM
    )

    # Add the generated file to the list of sources
    target_sources(easypap PRIVATE ${FLEX_GENERATED_C_FILE})

    # hwloc
    include(FindPkgConfig)
    if(PKG_CONFIG_EXECUTABLE)
      unset(HWLOC_FOUND CACHE)
      pkg_search_module(HWLOC hwloc)
      if(NOT HWLOC_FOUND)
        message(FATAL_ERROR "HWLOC not found with pkg-config, add the path to hwloc.pc in PKG_CONFIG_PATH.")
      endif()
    else()
      message(FATAL_ERROR "PKG_CONFIG_EXECUTABLE: not found.")
    endif()

    target_include_directories(easypap PRIVATE ${HWLOC_INCLUDE_DIRS})
    target_compile_options(easypap PRIVATE ${HWLOC_CFLAGS})
    target_link_libraries(easypap PRIVATE ${HWLOC_LINK_LIBRARIES})
    target_link_options(easypap PRIVATE ${HWLOC_LDFLAGS})

    # OpenCL
    if(ENABLE_OPENCL)
        find_package(OpenCL QUIET)
        if(NOT OpenCL_FOUND)
            message(STATUS "OpenCL not found. Disabling ENABLE_OPENCL option.")
            set(ENABLE_OPENCL OFF)
        else()
            message(STATUS "OpenCL library found.")
            target_sources(easypap PRIVATE src/ocl.c src/mesh_mgpu_ocl.c src/ezp_ocl_event.c)
            target_link_libraries(easypap PUBLIC OpenCL::OpenCL) 
            target_compile_definitions(easypap PRIVATE ENABLE_OPENCL CL_SILENCE_DEPRECATION)
        endif()
    endif()

    # CUDA
    if(ENABLE_CUDA)
        enable_language(CUDA)
        if(NOT CMAKE_CUDA_COMPILER)
            message(STATUS "CUDA compiler not found. Disabling ENABLE_CUDA option.")
            set(ENABLE_CUDA OFF)
        else()
            message(STATUS "CUDA compiler found: ${CMAKE_CUDA_COMPILER_ID}")
            cmake_policy(SET CMP0104 NEW)
            set_target_properties(easypap PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
            target_compile_definitions(easypap PRIVATE ENABLE_CUDA)
            set_property(TARGET easypap PROPERTY CUDA_ARCHITECTURES OFF)
            target_sources(easypap PRIVATE src/nvidia_cuda.cu src/mesh_mgpu_cuda.cu src/ezp_cuda_event.cu)
        endif()
    endif()

    # MIPP
    if(ENABLE_MIPP)
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/lib/mipp")
        set(MIPP_ROOT ${CMAKE_SOURCE_DIR}/lib/mipp)
        find_package(MIPP REQUIRED)
        target_link_libraries(easypap PRIVATE MIPP)
    endif()

    # SHA
    if(ENABLE_SHA)
        find_package(OpenSSL 3 QUIET)
        if(NOT OpenSSL_FOUND)
            message(STATUS "OpenSSL 3 not found. Disabling ENABLE_SHA option.")
            set(ENABLE_SHA OFF)
        else()
            message(STATUS "OpenSSL 3 library found.")
            target_link_libraries(easypap PRIVATE OpenSSL::SSL OpenSSL::Crypto) 
            target_compile_definitions(easypap PRIVATE ENABLE_SHA)
        endif()
    endif()

    # MPI
    if(ENABLE_MPI)
        find_package(MPI QUIET)
        if(NOT MPI_FOUND)
            message(STATUS "MPI not found. Disabling ENABLE_MPI option.")
            set(ENABLE_MPI OFF)
        else()
            message(STATUS "MPI library found.")
            target_link_libraries(easypap PRIVATE MPI::MPI_C) 
            target_compile_definitions(easypap PRIVATE ENABLE_MPI)
        endif()
    endif()

    # Kernels
    add_subdirectory(kernel)

    include(CMakePrintHelpers)
    cmake_print_properties(TARGETS easypap
        PROPERTIES COMPILE_OPTIONS COMPILE_DEFINITIONS)

endif()

# package config
# See https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html
include(CMakePackageConfigHelpers)

configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/EasypapConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/EasypapConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)

write_basic_package_version_file(EasypapConfigVersion.cmake
  VERSION ${EASYPAP_VERSION_LONG}
  COMPATIBILITY AnyNewerVersion)

# Install config files
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/EasypapConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/EasypapConfigVersion.cmake
  COMPONENT ezv
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Easypap)
