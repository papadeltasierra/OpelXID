cmake_minimum_required(VERSION 3.16)

# Resolve the opel_mid component from the repository root.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${REPO_ROOT}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(basic_text)
