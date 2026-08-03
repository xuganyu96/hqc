add_executable(rs_constants
    main.c
    constants.c
    verify.c
    ${CMAKE_SOURCE_DIR}/src/ref/gf.c
)

target_include_directories(rs_constants PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/ref/hqc-1
    ${CMAKE_SOURCE_DIR}/src/common/hqc-1
)
