cmake_minimum_required(VERSION 3.16)
project(asi_felis_hello VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── RPATH 設定（Linux/macOS 專用，Windows 無此概念）─────────
# $ORIGIN = 執行檔所在目錄（Linux ELF 內建變數）
# 確保執行時不需要 export LD_LIBRARY_PATH 也能找到 .so
if(NOT WIN32)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

    if(APPLE)
        set(CMAKE_MACOSX_RPATH 1)
        set(CMAKE_INSTALL_RPATH "@executable_path")
    else()
        set(CMAKE_INSTALL_RPATH "$ORIGIN")
    endif()
endif()

# ── 平台偵測（採用方案 A：{os}_{arch} 命名）────────────────
# 目錄結構：
#   sdk/linux_x86_64/   ← WSL2 / Linux 開發機
#   sdk/linux_aarch64/  ← Jetson Orin NX
#   sdk/windows_x86_64/ ← Windows 開發機（M2 主平台）
#
# 自動偵測，可用 -DVITURE_PLATFORM=xxx 覆寫
if(NOT DEFINED VITURE_PLATFORM)
    if(WIN32)
        set(VITURE_PLATFORM "windows_x86_64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(VITURE_PLATFORM "linux_aarch64")
    else()
        set(VITURE_PLATFORM "linux_x86_64")
    endif()
endif()

set(VITURE_SDK_ROOT "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "VITURE SDK root directory")
set(VITURE_INCLUDE_DIR "${VITURE_SDK_ROOT}/include")
set(VITURE_LIB_DIR     "${VITURE_SDK_ROOT}/${VITURE_PLATFORM}")

message(STATUS "VITURE platform : ${VITURE_PLATFORM}")
message(STATUS "VITURE include  : ${VITURE_INCLUDE_DIR}")
message(STATUS "VITURE lib dir  : ${VITURE_LIB_DIR}")

# ── 匯入 VITURE 核心動態庫 ──────────────────────────────────
# 不同平台的 .lib/.dll/.so 命名對照：
#   Windows : glasses.lib + glasses.dll        (MSVC import library + runtime)
#   Linux   : libglasses.so                    (single shared object)
add_library(viture_glasses SHARED IMPORTED)

if(WIN32)
    set_target_properties(viture_glasses PROPERTIES
        IMPORTED_IMPLIB   "${VITURE_LIB_DIR}/glasses.lib"
        IMPORTED_LOCATION "${VITURE_LIB_DIR}/glasses.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${VITURE_INCLUDE_DIR}"
    )
else()
    set_target_properties(viture_glasses PROPERTIES
        IMPORTED_LOCATION "${VITURE_LIB_DIR}/libglasses.so"
        INTERFACE_INCLUDE_DIRECTORIES "${VITURE_INCLUDE_DIR}"
    )
endif()

# carina_vio：6DoF VIO 引擎（Eigen + Flatbuffers，主機端 SLAM 計算）
# Windows 上 carina_vio.dll 沒有對應的 .lib，由 glasses.dll 內部載入
# 因此 Windows 不需 IMPORTED_IMPLIB，只需確保 dll 在 runtime path
if(NOT WIN32)
    add_library(viture_carina SHARED IMPORTED)
    set_target_properties(viture_carina PROPERTIES
        IMPORTED_LOCATION "${VITURE_LIB_DIR}/libcarina_vio.so"
    )
endif()

# ── Hello World 執行檔 ──────────────────────────────────────
add_executable(asi_hello main.cpp)

target_include_directories(asi_hello PRIVATE "${VITURE_INCLUDE_DIR}")

target_link_libraries(asi_hello PRIVATE viture_glasses)
if(NOT WIN32)
    target_link_libraries(asi_hello PRIVATE viture_carina)
endif()

# ── libusb 依賴策略 ─────────────────────────────────────────
# Windows         : SDK 附帶 libusb-1.0.dll，由 glasses.dll 內部載入，不需顯式 link
# Linux x86_64    : SDK 附帶 libusb-1.0.so.0，直接 link
# Linux aarch64   : SDK 不附帶，使用 Jetson JetPack 6.2 系統內建版本
#                   安裝指令（若尚未安裝）：sudo apt install libusb-1.0-0-dev
if(WIN32)
    message(STATUS "windows: libusb-1.0.dll bundled, loaded internally by glasses.dll")
elseif(VITURE_PLATFORM STREQUAL "linux_aarch64")
    message(STATUS "linux_aarch64: using system libusb (JetPack 6.2 built-in)")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    target_link_libraries(asi_hello PRIVATE ${LIBUSB_LIBRARIES})
    target_include_directories(asi_hello PRIVATE ${LIBUSB_INCLUDE_DIRS})
else()
    message(STATUS "linux_x86_64: using SDK bundled libusb")
    add_library(viture_libusb SHARED IMPORTED)
    set_target_properties(viture_libusb PROPERTIES
        IMPORTED_LOCATION "${VITURE_LIB_DIR}/libusb-1.0.so.0"
    )
    target_link_libraries(asi_hello PRIVATE viture_libusb)
endif()

# ── 執行期 .so / .dll 路徑 ──────────────────────────────────
if(WIN32)
    # Windows 沒有 RPATH 概念，需要把所有 SDK runtime DLL 拷貝到 .exe 同目錄
    # SDK Windows release 包含 6 個 dll：
    #   glasses.dll          公開 API 入口
    #   carina_vio.dll       VIO 引擎（Eigen + Flatbuffers）
    #   opencv_world4100.dll 影像處理（OpenCV 4.1.0）
    #   glew32.dll           OpenGL Extension Wrangler
    #   libusb-1.0.dll       USB / UVC / HID 通訊
    set(VITURE_WIN_DLLS
        glasses.dll
        carina_vio.dll
        opencv_world4100.dll
        glew32.dll
        libusb-1.0.dll
    )
    foreach(dll_name ${VITURE_WIN_DLLS})
        add_custom_command(TARGET asi_hello POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VITURE_LIB_DIR}/${dll_name}"
                "$<TARGET_FILE_DIR:asi_hello>/${dll_name}"
            COMMENT "Copying ${dll_name} to output directory"
        )
    endforeach()
else()
    # Linux：透過 RPATH 讓執行檔知道從 SDK lib 目錄找 .so
    set_target_properties(asi_hello PROPERTIES
        BUILD_RPATH   "${VITURE_LIB_DIR}"
        INSTALL_RPATH "${VITURE_LIB_DIR}"
    )
endif()

# ── 編譯器警告等級 ──────────────────────────────────────────
# SDK callback 風格容易吞錯誤，提早在 build time 攔截
if(MSVC)
    target_compile_options(asi_hello PRIVATE /W4 /permissive-)
else()
    target_compile_options(asi_hello PRIVATE
        -Wall -Wextra -Wpedantic -Werror=return-type)
endif()
