cmake_minimum_required(VERSION 3.20)
project(EasyScript VERSION 0.1.0 LANGUAGES CXX)

# 設定 C++20 標準
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 編譯警告設定
if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 在 MinGW 環境下靜態鏈結 C++ 執行期庫，避免缺失 DLL
if (MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

# 核心庫原始碼
set(EASYSCRIPT_SOURCES
    src/value.cpp
    src/heap.cpp
    src/token.cpp
    src/lexer.cpp
    src/opcode.cpp
    src/compiler.cpp
    src/serializer.cpp
    src/jit.cpp
    src/vm.cpp
    src/objects/gc_object.cpp
    src/objects/obj_string.cpp
    src/objects/obj_array.cpp
    src/objects/obj_map.cpp
    src/objects/obj_range.cpp
    src/objects/obj_native_fn.cpp
    src/objects/obj_function.cpp
    src/objects/obj_upvalue.cpp
    src/objects/obj_closure.cpp
    src/objects/obj_class.cpp
    src/objects/obj_instance.cpp
    src/objects/obj_bound_method.cpp
    src/objects/obj_module.cpp
)

# 建立核心靜態庫
add_library(easyscript_core STATIC ${EASYSCRIPT_SOURCES})

target_include_directories(easyscript_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 核心執行檔與 CLI 工具
add_executable(easyscript src/main.cpp)
target_link_libraries(easyscript PRIVATE easyscript_core)
set_target_properties(easyscript PROPERTIES ENABLE_EXPORTS ON)

# -----------------------------------------------------------------------------
# 範例動態鏈結庫外掛 (Sample Dynamic Extension Plugin)
# -----------------------------------------------------------------------------
add_library(sample_plugin SHARED tests/plugins/sample_plugin.cpp)
target_link_libraries(sample_plugin PRIVATE easyscript_core)
target_include_directories(sample_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(sample_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sample_plugin"
)

add_custom_command(TARGET sample_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:sample_plugin>" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/"
)

# 測試可執行檔 alias
add_custom_target(easyscript_test DEPENDS easyscript sample_plugin)
