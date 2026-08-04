cmake_minimum_required(VERSION 3.21)
include(CTest)

if(NOT DEFINED HQC_ARCH)
    message(FATAL_ERROR "HQC_ARCH must be set (ref or x86_64)")
endif()

find_package(OpenSSL REQUIRED)

set(TEST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test_outputs")
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

set(NIST_RNG_SRC "${CMAKE_SOURCE_DIR}/lib/nist-rng/rng.c")
set_source_files_properties(${NIST_RNG_SRC} PROPERTIES
        COMPILE_OPTIONS "-Wno-error=sign-compare;-Wno-sign-compare"
)

if(HQC_ARCH STREQUAL "x86_64")
    set(IMPL_DIR        "${CMAKE_SOURCE_DIR}/src/${HQC_ARCH}/${HQC_X86_IMPL}")
    set(ARCH_COMMON_DIR "${CMAKE_SOURCE_DIR}/src/${HQC_ARCH}/common")
    set(KAT_GOLD_DIR    "${CMAKE_SOURCE_DIR}/kats/${HQC_ARCH}/${HQC_X86_IMPL}")
    set(IMD_GOLD_DIR    "${CMAKE_SOURCE_DIR}/kats/${HQC_ARCH}/${HQC_X86_IMPL}")
else()
    set(IMPL_DIR        "${CMAKE_SOURCE_DIR}/src/ref")
    set(ARCH_COMMON_DIR "${CMAKE_SOURCE_DIR}/src/ref")
    set(KAT_GOLD_DIR    "${CMAKE_SOURCE_DIR}/kats/${HQC_ARCH}")
    set(IMD_GOLD_DIR    "${CMAKE_SOURCE_DIR}/kats/${HQC_ARCH}")
endif()

file(GLOB KAT_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_kat.c"
)
file(GLOB IMD_SRCS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_intermediates_values.c"
)

foreach(variant IN LISTS VARIANTS)
    string(REPLACE "-" "_" safe_var ${variant})

    #─────────────────────────────────────────────────────
    # 1) KAT tests
    #─────────────────────────────────────────────────────
    foreach(src IN LISTS KAT_SRCS)
        get_filename_component(name ${src} NAME_WE)
        set(exe "${name}_${safe_var}")

        file(GLOB COMMON_SRCS
                "${CMAKE_SOURCE_DIR}/src/common/*.c"
                "${CMAKE_SOURCE_DIR}/src/common/*/*.c"
        )
        list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/randombytes\\.c$")
        list(APPEND COMMON_SRCS "${NIST_RNG_SRC}")

        file(GLOB IMPL_SRCS
                "${IMPL_DIR}/*.c"
        )
        file(GLOB ARCH_COMMON_SRCS
                "${ARCH_COMMON_DIR}/*.c"
                "${ARCH_COMMON_DIR}/${variant}/*.c"
        )
        file(GLOB VARIANT_SRCS
                "${IMPL_DIR}/${variant}/*.c"
                "${IMPL_DIR}/${variant}/*/*.c"
        )

        add_executable(${exe}
                ${src}
                ${COMMON_SRCS}
                ${ARCH_COMMON_SRCS}
                ${IMPL_SRCS}
                ${VARIANT_SRCS}
        )
        target_compile_options(${exe} PRIVATE
                -Wno-unused-result
                -Wno-error=unused-result
                -Wno-missing-prototypes
        )
        target_link_libraries(${exe} PRIVATE fips202 OpenSSL::Crypto)
        target_compile_definitions(${exe} PRIVATE
                $<$<STREQUAL:${HQC_ARCH},ref>:REF_MODE>
                $<$<AND:$<STREQUAL:${HQC_ARCH},x86_64>,$<STREQUAL:${HQC_X86_IMPL},avx256>>:AVX2_MODE>
        )
        target_include_directories(${exe} PRIVATE
                ${CMAKE_SOURCE_DIR}/tests/kats
                ${CMAKE_SOURCE_DIR}/src/common
                ${CMAKE_SOURCE_DIR}/src/common/${variant}
                ${ARCH_COMMON_DIR}
                ${ARCH_COMMON_DIR}/${variant}
                ${IMPL_DIR}
                ${IMPL_DIR}/${variant}
                ${CMAKE_SOURCE_DIR}/lib/fips202
                ${CMAKE_SOURCE_DIR}/lib/nist-rng
        )

        file(GLOB GOLD_RSP
                "${KAT_GOLD_DIR}/${variant}/PQCkemKAT_*.rsp"
        )
        list(GET GOLD_RSP 0 GOLD_FILE)
        get_filename_component(GOLD_NAME ${GOLD_FILE} NAME)

        add_test(NAME ${exe}
                COMMAND /bin/sh -c
                "mkdir -p '${TEST_OUTPUT_DIR}/${exe}' && cd '${TEST_OUTPUT_DIR}/${exe}' && \
               '$<TARGET_FILE:${exe}>' && \
               diff -u '${GOLD_FILE}' '${GOLD_NAME}'"
        )
        set_tests_properties(${exe} PROPERTIES LABELS "kats")
    endforeach()

    #─────────────────────────────────────────────────────
    # 2) Intermediate-values tests
    #─────────────────────────────────────────────────────
    foreach(src IN LISTS IMD_SRCS)
        get_filename_component(name ${src} NAME_WE)
        set(exe "${name}_${safe_var}")

        file(GLOB COMMON_SRCS
                "${CMAKE_SOURCE_DIR}/src/common/*.c"
                "${CMAKE_SOURCE_DIR}/src/common/*/*.c"
        )
        list(FILTER COMMON_SRCS EXCLUDE REGEX ".*/randombytes\\.c$")
        list(APPEND COMMON_SRCS "${NIST_RNG_SRC}")

        file(GLOB IMPL_SRCS
                "${IMPL_DIR}/*.c"
        )
        file(GLOB ARCH_COMMON_SRCS
                "${ARCH_COMMON_DIR}/*.c"
                "${ARCH_COMMON_DIR}/${variant}/*.c"
        )
        file(GLOB VARIANT_SRCS
                "${IMPL_DIR}/${variant}/*.c"
                "${IMPL_DIR}/${variant}/*/*.c"
        )

        add_executable(${exe}
                ${src}
                ${COMMON_SRCS}
                ${ARCH_COMMON_SRCS}
                ${IMPL_SRCS}
                ${VARIANT_SRCS}
        )
        target_compile_options(${exe} PRIVATE
                -Wno-unused-result
                -Wno-error=unused-result
                -Wno-missing-prototypes
        )
        target_compile_definitions(${exe} PRIVATE
                VERBOSE
                $<$<STREQUAL:${HQC_ARCH},ref>:REF_MODE>
                $<$<AND:$<STREQUAL:${HQC_ARCH},x86_64>,$<STREQUAL:${HQC_X86_IMPL},avx256>>:AVX2_MODE>
        )
        target_include_directories(${exe} PRIVATE
                ${CMAKE_SOURCE_DIR}/tests/kats
                ${CMAKE_SOURCE_DIR}/src/common
                ${CMAKE_SOURCE_DIR}/src/common/${variant}
                ${ARCH_COMMON_DIR}
                ${ARCH_COMMON_DIR}/${variant}
                ${IMPL_DIR}
                ${IMPL_DIR}/${variant}
                ${CMAKE_SOURCE_DIR}/lib/fips202
                ${CMAKE_SOURCE_DIR}/lib/nist-rng
        )
        target_link_libraries(${exe} PRIVATE
                fips202
                OpenSSL::Crypto
        )

        set(GOLD_FILE_IMD
                "${IMD_GOLD_DIR}/${variant}/intermediates_values"
        )
        if(NOT EXISTS "${GOLD_FILE_IMD}")
            message(FATAL_ERROR
                    "Could not find golden intermediates file: ${GOLD_FILE_IMD}"
            )
        endif()
        get_filename_component(GOLD_IMD_NAME ${GOLD_FILE_IMD} NAME)

        add_test(NAME ${exe}
                COMMAND /bin/sh -c
                "mkdir -p '${TEST_OUTPUT_DIR}/${exe}' && cd '${TEST_OUTPUT_DIR}/${exe}' && \
               '$<TARGET_FILE:${exe}>' > '${GOLD_IMD_NAME}' && \
               diff -u '${GOLD_FILE_IMD}' '${GOLD_IMD_NAME}'"
        )
        set_tests_properties(${exe} PROPERTIES LABELS "kats")
    endforeach()

endforeach()

#─────────────────────────────────────────────────────
# 3) Reed-Solomon code TV tests
#─────────────────────────────────────────────────────
if(HQC_ARCH STREQUAL "x86_64")
    foreach(variant IN LISTS VARIANTS)
        string(REPLACE "-" "_" safe_var ${variant})
        set(exe_tv "test_reed_solomon_tv_${safe_var}")

        file(GLOB COMMON_SRCS
                "${CMAKE_SOURCE_DIR}/src/common/*.c"
                "${CMAKE_SOURCE_DIR}/src/common/*/*.c"
        )
        file(GLOB IMPL_SRCS
                "${IMPL_DIR}/*.c"
        )
        file(GLOB ARCH_COMMON_SRCS
                "${ARCH_COMMON_DIR}/*.c"
                "${ARCH_COMMON_DIR}/${variant}/*.c"
        )
        file(GLOB VARIANT_SRCS
                "${IMPL_DIR}/${variant}/*.c"
                "${IMPL_DIR}/${variant}/*/*.c"
        )

        list(FILTER COMMON_SRCS  EXCLUDE REGEX ".*/hqc-kem\\.c$")
        list(FILTER VARIANT_SRCS EXCLUDE REGEX ".*/reed_solomon\\.c$")

        add_executable(${exe_tv}
                ${CMAKE_SOURCE_DIR}/tests/kats/test_reed_solomon_tv.c
                ${COMMON_SRCS}
                ${ARCH_COMMON_SRCS}
                ${IMPL_SRCS}
                ${VARIANT_SRCS}
        )

        target_compile_definitions(${exe_tv} PRIVATE
                RS_IMPL_INCLUDE=\"${IMPL_DIR}/${variant}/reed_solomon.c\"
                $<$<STREQUAL:${HQC_ARCH},x86_64>:AVX2_MODE>
        )
        target_compile_options(${exe_tv} PRIVATE
                -Wno-unused-result -Wno-error=unused-result -Wno-missing-prototypes
        )
        target_include_directories(${exe_tv} PRIVATE
                ${CMAKE_SOURCE_DIR}/tests/kats
                ${CMAKE_SOURCE_DIR}/src/common
                ${CMAKE_SOURCE_DIR}/src/common/${variant}
                ${ARCH_COMMON_DIR}
                ${ARCH_COMMON_DIR}/${variant}
                ${IMPL_DIR}
                ${IMPL_DIR}/${variant}
                ${CMAKE_SOURCE_DIR}/lib/fips202
                ${CMAKE_SOURCE_DIR}/lib/nist-rng
        )
        target_link_libraries(${exe_tv} PRIVATE fips202)

        set(GOLD_RS_TV "${CMAKE_SOURCE_DIR}/kats/rs/${variant}/rs.tv")
        if(NOT EXISTS "${GOLD_RS_TV}")
            message(FATAL_ERROR "Could not find golden RS TV: ${GOLD_RS_TV}")
        endif()

        add_test(NAME ${exe_tv}
                COMMAND /bin/sh -c
                "mkdir -p '${TEST_OUTPUT_DIR}/${exe_tv}' && cd '${TEST_OUTPUT_DIR}/${exe_tv}' && \
           '$<TARGET_FILE:${exe_tv}>' '${GOLD_RS_TV}' && \
           diff -u '${GOLD_RS_TV}' rs.rsp"
        )
        set_tests_properties(${exe_tv} PROPERTIES LABELS "kats")
    endforeach()

elseif(HQC_ARCH STREQUAL "ref")
    foreach(variant IN LISTS VARIANTS)
        string(REPLACE "-" "_" safe_var ${variant})
        set(exe_tv_ref "test_reed_solomon_tv_${safe_var}")

        file(GLOB COMMON_SRCS
                "${CMAKE_SOURCE_DIR}/src/common/*.c"
                "${CMAKE_SOURCE_DIR}/src/common/*/*.c"
        )

        file(GLOB ARCH_COMMON_SRCS
                "${ARCH_COMMON_DIR}/*.c"
                "${ARCH_COMMON_DIR}/${variant}/*.c"
        )

        file(GLOB IMPL_SRCS
                "${IMPL_DIR}/*.c"
        )

        file(GLOB VARIANT_SRCS
                "${IMPL_DIR}/${variant}/*.c"
                "${IMPL_DIR}/${variant}/*/*.c"
        )

        list(FILTER COMMON_SRCS      EXCLUDE REGEX ".*/hqc-kem\\.c$")
        list(FILTER ARCH_COMMON_SRCS EXCLUDE REGEX ".*/reed_solomon\\.c$")
        list(FILTER IMPL_SRCS        EXCLUDE REGEX ".*/reed_solomon\\.c$")

        add_executable(${exe_tv_ref}
                ${CMAKE_SOURCE_DIR}/tests/kats/test_reed_solomon_tv.c
                ${COMMON_SRCS}
                ${ARCH_COMMON_SRCS}
                ${IMPL_SRCS}
                ${VARIANT_SRCS}
        )

        target_compile_definitions(${exe_tv_ref} PRIVATE
                RS_IMPL_INCLUDE=\"${IMPL_DIR}/reed_solomon.c\"
                REF_MODE
        )
        target_compile_options(${exe_tv_ref} PRIVATE
                -Wno-unused-result -Wno-error=unused-result -Wno-missing-prototypes
        )
        target_include_directories(${exe_tv_ref} PRIVATE
                ${CMAKE_SOURCE_DIR}/tests/kats
                ${CMAKE_SOURCE_DIR}/src/common
                ${CMAKE_SOURCE_DIR}/src/common/${variant}
                ${ARCH_COMMON_DIR}
                ${ARCH_COMMON_DIR}/${variant}
                ${IMPL_DIR}
                ${IMPL_DIR}/${variant}
                ${CMAKE_SOURCE_DIR}/lib/fips202
                ${CMAKE_SOURCE_DIR}/lib/nist-rng
        )
        target_link_libraries(${exe_tv_ref} PRIVATE fips202)

        set(GOLD_RS_TV "${CMAKE_SOURCE_DIR}/kats/rs/${variant}/rs.tv")
        if(NOT EXISTS "${GOLD_RS_TV}")
            message(FATAL_ERROR "Could not find golden RS TV: ${GOLD_RS_TV}")
        endif()

        add_test(NAME ${exe_tv_ref}
                COMMAND /bin/sh -c
                "mkdir -p '${TEST_OUTPUT_DIR}/${exe_tv_ref}' && cd '${TEST_OUTPUT_DIR}/${exe_tv_ref}' && \
           '$<TARGET_FILE:${exe_tv_ref}>' '${GOLD_RS_TV}' && \
           diff -u '${GOLD_RS_TV}' rs.rsp"
        )
        set_tests_properties(${exe_tv_ref} PROPERTIES LABELS "kats")
    endforeach()
endif()
