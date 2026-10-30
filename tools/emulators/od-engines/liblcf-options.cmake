# Large generated template units can exhaust the host during optimization.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/generated/ldb_database.cpp")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/generated/ldb_database.cpp"
        PROPERTIES COMPILE_OPTIONS "-O1")
endif()
