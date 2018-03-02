execute_process(COMMAND
            git describe
        WORKING_DIRECTORY
            "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE
            RES
        OUTPUT_VARIABLE
            GVERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT RES EQUAL 0)
    set(GVERSION "v0.0.0-NOTFOUND")
endif()

string(REPLACE "v" "" GVERSION "${GVERSION}")
string(REPLACE "-" ";" GVERSION "${GVERSION}")
string(REPLACE "." ";" VERSION_LIST "${GVERSION}")

list(GET VERSION_LIST 0 MAJOR)
list(GET VERSION_LIST 1 MINOR)
list(GET VERSION_LIST 2 PATCH)

list(LENGTH VERSION_LIST VSIZE)

if(${VSIZE} EQUAL 3)
    set(CPACK_PACKAGE_NAME "qtox")
else()
    set(CPACK_PACKAGE_NAME "qtox-nightly")
    set(BUILD_NUM $ENV{TRAVIS_BUILD_NUMBER})
    if(BUILD_NUM)
        set(PATCH "${PATCH}.${BUILD_NUM}")
    endif()
endif()

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_VERSION_MAJOR "${MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PATCH}")
set(CPACK_PACKAGE_CONTACT "qtox-dev@lists.tox.chat")
include(CPack)
