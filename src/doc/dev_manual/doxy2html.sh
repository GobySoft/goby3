#!/bin/bash
set -e -u

CMAKE_CURRENT_SOURCE_DIR=$1
CMAKE_CURRENT_BINARY_DIR=$2

# ((cat ${CMAKE_CURRENT_BINARY_DIR}/goby-dev.doxy; bzr version-info --custom --template="PROJECT_NUMBER = \"Series: {branch_nick}, revision: {revno}, released on {date} \"") | doxygen - 

(cat ${CMAKE_CURRENT_BINARY_DIR}/goby-dev.doxy; echo "GENERATE_HTML = YES"; echo "GENERATE_LATEX = NO"; echo "HIDE_UNDOC_CLASSES = NO") | doxygen -

