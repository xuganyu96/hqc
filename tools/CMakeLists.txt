option(HQC_BUILD_RS_TOOL "Build the Reed-Solomon developer tool" ON)

if(HQC_BUILD_RS_TOOL)
    add_subdirectory(rs)
endif()
