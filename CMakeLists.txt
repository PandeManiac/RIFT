cmake_minimum_required(VERSION 3.21)
project(RFT LANGUAGES C)

option(RFT_ENABLE_ASAN "Enable AddressSanitizer" OFF)

message(STATUS "RFT_ENABLE_ASAN = ${RFT_ENABLE_ASAN}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/header")

file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS
    "${SRC_DIR}/*.c"
)

file(GLOB_RECURSE VENDOR_SRC CONFIGURE_DEPENDS
    "${SRC_DIR}/vendor/*.c"
)

list(REMOVE_ITEM ENGINE_SRC ${VENDOR_SRC})

add_executable(${PROJECT_NAME}
    ${ENGINE_SRC}
    ${VENDOR_SRC}
)

find_package(glfw3 CONFIG REQUIRED)
find_package(cglm CONFIG REQUIRED)
find_package(json-c CONFIG REQUIRED)
find_package(Threads REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw
    cglm::cglm
    json-c::json-c
    Threads::Threads
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${INC_DIR}
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    "${INC_DIR}/vendor"
)

if (MSVC)
    set_source_files_properties(${VENDOR_SRC}
        PROPERTIES
            COMPILE_OPTIONS "/W0"
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /wd4068
        /wd4100
        /wd4201
        /wd4204
        /wd4706
        /wd4996
        $<$<CONFIG:Debug>:
            /Od
            /Zi
        >
        $<$<CONFIG:Release>:
            /O2
            /GL
            /arch:AVX2
        >
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    set_source_files_properties(${VENDOR_SRC}
        PROPERTIES
            COMPILE_OPTIONS "-w"
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        # -Werror

        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wfloat-equal

        -Wbad-function-cast
        -Werror=implicit-function-declaration
        -Wmissing-prototypes
        -Wstrict-prototypes
        -Wold-style-definition

        -Wredundant-decls
        -Wnested-externs

        -Wshadow

        -Wswitch
        -Wswitch-enum
        -Wswitch-default
        -Wimplicit-fallthrough

        -Wvla
        -Walloca
        -Wcast-align
        -Wcast-qual
        -Wpointer-arith
        -Wnull-dereference
        -Wuninitialized
        -Wmissing-field-initializers

        -Wundef
        -Wunused-macros
        -Wno-unused-macros

        -Wformat=2
        -Wformat-security

        -Wwrite-strings
        # -Wcomma

        -Wno-declaration-after-statement
        -Wno-empty-translation-unit
        $<$<CONFIG:Debug>:
            -O1
            -g
            -fno-omit-frame-pointer
            -mavx2
            -mfma
        >
        $<$<CONFIG:Release>:
            -O3
            -funroll-loops
            -fmerge-all-constants
            -falign-functions=64
            -falign-loops=64
            -mavx2
            -mfma
        >
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:-flto>
    )

    if (WIN32)
        target_link_options(${PROJECT_NAME} PRIVATE
            -static
            -static-libgcc
        )
    endif()
endif()

if (RFT_ENABLE_ASAN)
    if (NOT MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -fno-omit-frame-pointer
            -fsanitize=address
        )

        target_link_options(${PROJECT_NAME} PRIVATE
            -fsanitize=address
        )
    endif()
else()
    if (NOT MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE
            -fno-sanitize=all
        )
    endif()
endif()

set(RFT_LSAN_SUPP "${CMAKE_SOURCE_DIR}/tools/suppressions/lsan.supp")

if (WIN32)
    set(RFT_RUN_COMMAND ${CMAKE_COMMAND} -E env $<TARGET_FILE:${PROJECT_NAME}>)
else()
    set(RFT_RUN_COMMAND ${CMAKE_COMMAND} -E env
        LSAN_OPTIONS=suppressions=${RFT_LSAN_SUPP}
        ASAN_OPTIONS=detect_leaks=1
        $<TARGET_FILE:${PROJECT_NAME}>
    )
endif()

add_custom_target(run
    COMMAND ${RFT_RUN_COMMAND}
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/glfw3.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/libcglm-0.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/libjson-c.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/libgcc_s_seh-1.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/libstdc++-6.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/libwinpthread-1.dll
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/res
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/res
    COMMAND ${CMAKE_COMMAND}
            -DRFT_STAGE_TARGET_FILE=$<TARGET_FILE:${PROJECT_NAME}>
            -DRFT_STAGE_SOURCE_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
            -DRFT_STAGE_DIST_DIR=${CMAKE_BINARY_DIR}/dist/RFT
            -DRFT_STAGE_RES_DIR=${CMAKE_SOURCE_DIR}/res
            -P ${CMAKE_SOURCE_DIR}/cmake/rft_stage_dist.cmake
)
