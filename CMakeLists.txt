cmake_minimum_required(VERSION 3.20)
project(entttree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- library ----

add_library(entttree
    src/position.cpp
)

target_include_directories(entttree
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# dependencies available via system include paths (homebrew, /usr/local, etc.)
find_path(ENTT_INCLUDE_DIR entt/entt.hpp
    HINTS /opt/homebrew/include /usr/local/include
)
find_path(GEOMC_INCLUDE_DIR geomc/linalg/LinalgTypes.h
    HINTS /opt/homebrew/include /usr/local/include
)
find_path(ANKERL_INCLUDE_DIR ankerl/unordered_dense.h
    HINTS /opt/homebrew/include /usr/local/include
)

target_include_directories(entttree
    PUBLIC
        ${ENTT_INCLUDE_DIR}
        ${GEOMC_INCLUDE_DIR}
        ${ANKERL_INCLUDE_DIR}
)

target_compile_options(entttree PRIVATE -Wall -Werror)

# ---- tests ----

option(ENTTTREE_BUILD_TESTS "Build tests" ON)

if(ENTTTREE_BUILD_TESTS)
    enable_testing()

    add_executable(test_hierarchy test/test_hierarchy.cpp)
    target_link_libraries(test_hierarchy PRIVATE entttree)
    target_compile_options(test_hierarchy PRIVATE -Wall -Werror)

    add_test(NAME test_hierarchy COMMAND test_hierarchy)
endif()

# ---- docs ----

find_package(Doxygen QUIET)

if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

# ---- install ----

include(GNUInstallDirs)

install(TARGETS entttree
    EXPORT entttreeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/entttree
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT entttreeTargets
    FILE entttreeTargets.cmake
    NAMESPACE entttree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/entttree
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/entttreeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/entttreeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/entttree
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/entttreeConfigVersion.cmake
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/entttreeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/entttreeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/entttree
)
