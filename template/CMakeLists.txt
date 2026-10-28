cmake_minimum_required(VERSION 3.20)
project(my_module VERSION 1.0.0 LANGUAGES CXX)

# 設定 C++20 標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 編譯器警告設定
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 在 MinGW 環境下靜態鏈結 C++ 執行期庫，避免缺失 libgcc / libstdc++ DLL
if (MINGW)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# -----------------------------------------------------------------------------
# EasyScript SDK 配置 (支援內建獨立 SDK 與外部 SDK)
# -----------------------------------------------------------------------------
# 優先採用模組內部包含的 sdk/ 目錄（完全獨立編譯，無須依賴外部 EasyScript 專案）
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/easyscript_plugin.hpp")
    message(STATUS "Using embedded EasyScript SDK from: ${CMAKE_CURRENT_SOURCE_DIR}/sdk")
    set(EASYSCRIPT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include")

    # 編譯內置 EasyScript 核心靜態庫
    set(EASYSCRIPT_CORE_SOURCES
        sdk/src/value.cpp
        sdk/src/heap.cpp
        sdk/src/token.cpp
        sdk/src/lexer.cpp
        sdk/src/opcode.cpp
        sdk/src/compiler.cpp
        sdk/src/serializer.cpp
        sdk/src/jit.cpp
        sdk/src/vm.cpp
        sdk/src/objects/gc_object.cpp
        sdk/src/objects/obj_string.cpp
        sdk/src/objects/obj_array.cpp
        sdk/src/objects/obj_map.cpp
        sdk/src/objects/obj_range.cpp
        sdk/src/objects/obj_native_fn.cpp
        sdk/src/objects/obj_function.cpp
        sdk/src/objects/obj_upvalue.cpp
        sdk/src/objects/obj_closure.cpp
        sdk/src/objects/obj_class.cpp
        sdk/src/objects/obj_instance.cpp
        sdk/src/objects/obj_bound_method.cpp
        sdk/src/objects/obj_module.cpp
        sdk/src/lsp/lsp_server.cpp
    )

    add_library(easyscript_core STATIC ${EASYSCRIPT_CORE_SOURCES})
    target_include_directories(easyscript_core PUBLIC ${EASYSCRIPT_INCLUDE_DIR})
    set(EASYSCRIPT_CORE_LIB easyscript_core)

    # 同時編譯獨立 easyscript 執行檔供模組驗證測試使用
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk/src/main.cpp")
        add_executable(easyscript sdk/src/main.cpp)
        target_include_directories(easyscript PRIVATE ${EASYSCRIPT_INCLUDE_DIR})
        target_link_libraries(easyscript PRIVATE easyscript_core)
        set_target_properties(easyscript PROPERTIES ENABLE_EXPORTS ON)
    endif()

else()
    # 回退模式：尋找外部 EasyScript SDK (由環境變數或系統路徑指定)
    set(EASYSCRIPT_SEARCH_PATHS
        ${EASYSCRIPT_ROOT}
        $ENV{EASYSCRIPT_ROOT}
        $ENV{EASYSCRIPT_PATH}/..
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../../EasyScript"
        "$ENV{USERPROFILE}/.easyscript"
        "$ENV{HOME}/.easyscript"
        "/usr/local"
        "/usr"
    )

    # 1. 尋找標頭檔目錄
    find_path(EASYSCRIPT_INCLUDE_DIR
        NAMES easyscript_plugin.hpp
        PATHS ${EASYSCRIPT_SEARCH_PATHS}
        PATH_SUFFIXES include include/easyscript
    )

    if (NOT EASYSCRIPT_INCLUDE_DIR)
        message(FATAL_ERROR 
            "Could not find EasyScript headers (easyscript_plugin.hpp).\n"
            "Please provide the EasyScript location via -DEASYSCRIPT_ROOT=<path> or set the EASYSCRIPT_ROOT environment variable."
        )
    endif()
    message(STATUS "Found external EasyScript headers: ${EASYSCRIPT_INCLUDE_DIR}")

    # 2. 尋找核心靜態庫 easyscript_core
    find_library(EASYSCRIPT_CORE_LIB
        NAMES easyscript_core libeasyscript_core
        PATHS ${EASYSCRIPT_SEARCH_PATHS}
        PATH_SUFFIXES build lib bin
    )

    if (NOT EASYSCRIPT_CORE_LIB)
        message(FATAL_ERROR 
            "Could not find easyscript_core library.\n"
            "Please build EasyScript first or specify -DEASYSCRIPT_ROOT=<path>."
        )
    endif()
    message(STATUS "Found external EasyScript core library: ${EASYSCRIPT_CORE_LIB}")
endif()

# -----------------------------------------------------------------------------
# 建立外掛動態鏈結庫 (Plugin Shared Library Target)
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
    src/my_module.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EASYSCRIPT_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${EASYSCRIPT_CORE_LIB}
)

# 移除預設的 'lib' 前綴（在 Windows 上產生 my_module.dll 而非 libmy_module.dll）
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${PROJECT_NAME}"
)

# -----------------------------------------------------------------------------
# 建置後操作 (Post-build Actions)
# 將動態庫複製至本模組根目錄，方便直接執行 easyscript test.easy 進行測試
# -----------------------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_CURRENT_SOURCE_DIR}/"
    COMMENT "Copying compiled plugin to template root for testing..."
)

# -----------------------------------------------------------------------------
# 安裝配置 (Installation Rules)
# -----------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION plugins
    RUNTIME DESTINATION plugins
)

install(FILES ${PROJECT_NAME}.d.json
    DESTINATION plugins
)
