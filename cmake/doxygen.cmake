find_package(Doxygen)
if(DOXYGEN_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        doxygen-awesome-css
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
    )
    FetchContent_MakeAvailable(doxygen-awesome-css)
    FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
            COMMENT "Generating HTML documentation with Doxygen..."
            VERBATIM)
else()
    message(STATUS "Doxygen not found, documentation target will not be available")
endif()
