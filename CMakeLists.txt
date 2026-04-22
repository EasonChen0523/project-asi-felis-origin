cmake_minimum_required(VERSION 3.16)
project(asi_felis_hello VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── RPATH 設定 ───────────────────────────────────────────────
# $ORIGIN = 執行檔所在目錄（Linux ELF 內建變數）
# 確保執行時不需要 export LD_LIBRARY_PATH 也能找到 .so
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)

if(APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    set(CMAKE_INSTALL_RPATH "@executable_path")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# ── SDK 路徑（依架構切換）──────────────────────────────────
# x86_64 開發機：cmake ..                  （預設）
# Jetson Orin NX：cmake .. -DVITURE_ARCH=arm64
#
# VITURE SDK arm64 目錄實際名稱為 aarch64
# 此處自動映射：arm64 → aarch64，使用者只需傳 arm64 即可
if(NOT DEFINED VITURE_ARCH)
    set(VITURE_ARCH "x86_64")
endif()

# 架構名稱 → SDK 目錄名稱映射
if(VITURE_ARCH STREQUAL "arm64")
    set(VITURE_SDK_ARCH_DIR "aarch64")
else()
    set(VITURE_SDK_ARCH_DIR "${VITURE_ARCH}")
endif()

set(VITURE_SDK_ROOT "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "VITURE SDK root directory")
set(VITURE_INCLUDE_DIR "${VITURE_SDK_ROOT}/include")
set(VITURE_LIB_DIR     "${VITURE_SDK_ROOT}/${VITURE_SDK_ARCH_DIR}")

message(STATUS "VITURE SDK arch : ${VITURE_ARCH} → SDK dir: ${VITURE_SDK_ARCH_DIR}")
message(STATUS "VITURE include  : ${VITURE_INCLUDE_DIR}")
message(STATUS "VITURE lib dir  : ${VITURE_LIB_DIR}")

# ── 匯入 VITURE 核心動態庫（兩個架構共用）──────────────────
add_library(viture_glasses SHARED IMPORTED)
set_target_properties(viture_glasses PROPERTIES
    IMPORTED_LOCATION "${VITURE_LIB_DIR}/libglasses.so"
    INTERFACE_INCLUDE_DIRECTORIES "${VITURE_INCLUDE_DIR}"
)

add_library(viture_carina SHARED IMPORTED)
set_target_properties(viture_carina PROPERTIES
    IMPORTED_LOCATION "${VITURE_LIB_DIR}/libcarina_vio.so"
)

# ── Hello World 執行檔 ──────────────────────────────────────
add_executable(asi_hello main.cpp)

target_include_directories(asi_hello PRIVATE "${VITURE_INCLUDE_DIR}")

target_link_libraries(asi_hello
    PRIVATE
    viture_glasses
    viture_carina
)

# ── libusb 依賴策略 ─────────────────────────────────────────
# x86_64：SDK 附帶 libusb-1.0.so.0，直接 link
# arm64 ：aarch64 SDK 不附帶，使用 Jetson JetPack 6.2 系統內建版本
#          安裝指令（若尚未安裝）：sudo apt install libusb-1.0-0-dev
if(VITURE_ARCH STREQUAL "arm64")
    message(STATUS "arm64: using system libusb (JetPack 6.2 built-in)")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    target_link_libraries(asi_hello PRIVATE ${LIBUSB_LIBRARIES})
    target_include_directories(asi_hello PRIVATE ${LIBUSB_INCLUDE_DIRS})
else()
    message(STATUS "x86_64: using SDK bundled libusb")
    add_library(viture_libusb SHARED IMPORTED)
    set_target_properties(viture_libusb PROPERTIES
        IMPORTED_LOCATION "${VITURE_LIB_DIR}/libusb-1.0.so.0"
    )
    target_link_libraries(asi_hello PRIVATE viture_libusb)
endif()

# ── RPATH：執行時從 SDK lib 目錄找 .so ──────────────────────
set_target_properties(asi_hello PROPERTIES
    BUILD_RPATH   "${VITURE_LIB_DIR}"
    INSTALL_RPATH "${VITURE_LIB_DIR}"
)
