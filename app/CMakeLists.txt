cmake_minimum_required(VERSION 3.13)

set(APP_NAME "TotalSMS")
set(APP_VERSION 1.0.0)

project(${APP_NAME}
    VERSION ${APP_VERSION}
    LANGUAGES C CXX
)

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" symbolic-ref --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (NOT GIT_COMMIT)
    set(APP_VERSION_HASH "${APP_VERSION}")
else()
    set(APP_VERSION_HASH "${APP_VERSION} [${GIT_COMMIT}]")
endif()

add_executable(${APP_NAME}
    source/ui/menus/filebrowser.cpp
    source/ui/menus/menu_base.cpp
    source/ui/menus/emu_menu.cpp
    source/ui/menus/intro_menu.cpp

    source/ui/error_box.cpp
    source/ui/notification.cpp
    source/ui/nvg_util.cpp
    source/ui/option_box.cpp
    source/ui/popup_list.cpp
    source/ui/progress_box.cpp
    source/ui/sidebar.cpp
    source/ui/widget.cpp
    source/ui/list.cpp
    source/ui/scrolling_text.cpp

    source/app.cpp
    source/option.cpp
    source/evman.cpp
    source/fs.cpp
    source/image.cpp
    source/log.cpp
    source/main.cpp
    source/nro.cpp
    source/swkbd.cpp
    source/i18n.cpp
    source/threaded_file_transfer.cpp
    source/minizip_helper.cpp

    source/emu_helpers/rewind.c
    source/emu_helpers/rewind_bar.cpp
)

target_compile_definitions(${APP_NAME} PRIVATE
    -DAPP_VERSION="${APP_VERSION}"
    -DAPP_VERSION_HASH="${APP_VERSION_HASH}"
)

target_compile_options(${APP_NAME} PRIVATE
    -Wall
    -Wextra

    # unsure if it's a good idea to enable these by default as
    # it may cause breakage upon compiler updates.
    # -Werror
    # -Wfatal-errors

    # disabled as nx uses s64 for size and offset, however stl uses size_t instead, thus
    # there being a lot of warnings.
    -Wno-sign-compare
    # disabled as many overriden methods don't use the params.
    -Wno-unused-parameter
    # pedantic warning, missing fields are set to 0.
    -Wno-missing-field-initializers
    # disabled as it warns for strcat 2 paths together, but it will never
    # overflow due to fs enforcing a max path len anyway.
    -Wno-format-truncation

    # the below are taken from my gba emulator, they've served me well ;)
    -Wformat-overflow=2
    -Wundef
    -Wmissing-include-dirs
    -fstrict-aliasing
    -Wstrict-overflow=2
    -Walloca
    -Wduplicated-cond
    -Wwrite-strings
    -Wdate-time
    -Wlogical-op
    -Wpacked
    -Wcast-qual
    -Wcast-align
    -Wimplicit-fallthrough=5
    -Wsuggest-final-types
    -Wuninitialized
    -fdiagnostics-all-candidates
)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(libpulsar
    GIT_REPOSITORY https://github.com/ITotalJustice/switch-libpulsar.git
    GIT_TAG 2596fc5
)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 8703044
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5c20573
)

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.11.1
)

FetchContent_Declare(minIni
    GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
    GIT_TAG 11cac8b
)

FetchContent_Declare(lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.10.0
    GIT_PROGRESS   TRUE
    SOURCE_SUBDIR  build/cmake
    FIND_PACKAGE_ARGS NAMES lz4
)

FetchContent_Declare(core
    GIT_REPOSITORY https://github.com/ITotalJustice/TotalSMS.git
    GIT_TAG 80be960
)

set(SMS_SINGLE_FILE ON)
set(SMS_PIXEL_WIDTH 32)
set(USE_MGB ON)

set(LZ4_BUILD_CLI OFF)

set(MININI_LIB_NAME minIni)
set(MININI_USE_STDIO ON)
set(MININI_USE_NX OFF)
set(MININI_USE_FLOAT OFF)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(NANOVG_DEBUG ON)
endif()
set(NANOVG_NO_JPEG ON)
set(NANOVG_NO_PNG OFF)
set(NANOVG_NO_BMP ON)
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)

set(STUB_I18n ON)
set(YYJSON_DISABLE_READER OFF)
set(YYJSON_DISABLE_WRITER ON)
set(YYJSON_DISABLE_UTILS ON)
set(YYJSON_DISABLE_FAST_FP_CONV ON)
set(YYJSON_DISABLE_NON_STANDARD ON)
set(YYJSON_DISABLE_UTF8_VALIDATION ON)
set(YYJSON_DISABLE_UNALIGNED_MEMORY_ACCESS OFF)

FetchContent_MakeAvailable(
    libpulsar
    nanovg
    stb
    minIni
    lz4
    core
)

if (STUB_I18n)
    target_compile_definitions(${APP_NAME} PRIVATE -DSTUB_I18n=1)
else()
    FetchContent_MakeAvailable(yyjson)
    target_link_libraries(${APP_NAME} PRIVATE yyjson)
endif()

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

find_package(ZLIB REQUIRED)
find_library(minizip_lib minizip REQUIRED)
find_path(minizip_inc minizip REQUIRED)

add_subdirectory(${core_SOURCE_DIR}/src/mgb src/mgb)

set_target_properties(${APP_NAME} PROPERTIES
    C_STANDARD 23
    C_EXTENSIONS ON
    CXX_STANDARD 26
    CXX_EXTENSIONS ON
)

target_link_libraries(${APP_NAME} PRIVATE
    libpulsar
    minIni
    nanovg
    stb

    ${minizip_lib}
    ZLIB::ZLIB
    SMS_Core
    mgb
    lz4
)

target_include_directories(${APP_NAME} PRIVATE
    include
    ${minizip_inc}
)

# copy the romfs
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# create assets target
dkp_add_asset_target(app_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

# set the embed path for assets
target_compile_options(${APP_NAME} PRIVATE
    --embed-dir=${CMAKE_SOURCE_DIR}/assets/embed
)

# add nanovg shaders to romfs
dkp_install_assets(app_romfs
    DESTINATION shaders
    TARGETS
        fill_aa_fsh
        fill_fsh
        fill_vsh
)

# create nacp
nx_generate_nacp(
    OUTPUT ${APP_NAME}.nacp
    NAME ${CMAKE_PROJECT_NAME}
    AUTHOR TotalJustice
    VERSION ${APP_VERSION}
)

# create nro
nx_create_nro(${APP_NAME}
    OUTPUT ${CMAKE_BINARY_DIR}/${APP_NAME}.nro
    ICON ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP ${APP_NAME}.nacp
    ROMFS app_romfs
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/switch/${APP_NAME})

add_custom_command(
    TARGET ${APP_NAME}_nro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${APP_NAME}.nro
        ${CMAKE_BINARY_DIR}/switch/${APP_NAME}/${APP_NAME}.nro
)

message(STATUS "generating nro in: ${CMAKE_BINARY_DIR}/${APP_NAME}.nro")
message(STATUS "run nxlink -s ${CMAKE_BINARY_DIR}/${APP_NAME}.nro")
