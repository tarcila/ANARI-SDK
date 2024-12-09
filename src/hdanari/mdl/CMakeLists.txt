## Copyright 2024 The Khronos Group
## SPDX-License-Identifier: Apache-2.0

project(hdanari_mdl LANGUAGES CXX)

find_package(mdl REQUIRED)

project_add_library(SHARED
  mdlRegistry.cpp
)

project_link_libraries(
  PUBLIC mdl::mdl_sdk
)

project_include_directories(
  PUBLIC ..
)

install(
    TARGETS
      ${PROJECT_NAME}
    LIBRARY
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
    RUNTIME
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
    ARCHIVE
      DESTINATION ${OPENUSD_PLUGIN_INSTALL_PREFIX}
  )