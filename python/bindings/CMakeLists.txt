# Copyright 2020 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Check if there is C++ code at all
########################################################################
if(NOT mpe_sources)
    MESSAGE(STATUS "No C++ sources... skipping python bindings")
    return()
endif(NOT mpe_sources)

########################################################################
# Check for pygccxml
########################################################################
GR_PYTHON_CHECK_MODULE_RAW(
    "pygccxml"
    "import pygccxml"
    PYGCCXML_FOUND
    )

include(GrPybind)

########################################################################
# Python Bindings
########################################################################

list(APPEND mpe_python_files
    mpe_source_python.cc
    mpe_config_python.cc python_bindings.cc)

GR_PYBIND_MAKE_OOT(mpe
   ../..
   gr::mpe
   "${mpe_python_files}")

# copy bindings extension for use in QA test module
add_custom_command(TARGET mpe_python POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mpe_python>
    ${CMAKE_BINARY_DIR}/test_modules/mpe/
)

install(TARGETS mpe_python DESTINATION ${GR_PYTHON_DIR}/mpe COMPONENT pythonapi)
