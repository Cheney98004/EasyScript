#pragma once

#include <cstdint>
#include <string_view>

// -----------------------------------------------------------------------------
// EasyScript Bytecode 指令集 (OpCode)
// -----------------------------------------------------------------------------
enum class OpCode : uint8_t {
    // 常數與字面量
    OP_CONSTANT,        // [arg: uint8_t index] 載入常數
    OP_CONSTANT_16,     // [arg: uint16_t index] 載入寬常數
    OP_NIL,             // 推入 null/nil
    OP_TRUE,            // 推入 true
    OP_FALSE,           // 推入 false
    OP_POP,             // 彈出棧頂

    // 區域變數 (在運算元棧上以 slot 下標存取)
    OP_GET_LOCAL,       // [arg: uint8_t slot]
    OP_SET_LOCAL,       // [arg: uint8_t slot]

    // 全域變數
    OP_DEFINE_GLOBAL,   // [arg: uint8_t nameIndex]
    OP_GET_GLOBAL,      // [arg: uint8_t nameIndex]
    OP_SET_GLOBAL,      // [arg: uint8_t nameIndex]

    // 運算子
    OP_EQUAL,           // ==
    OP_GREATER,         // >
    OP_LESS,            // <
    OP_ADD,             // +
    OP_SUB,             // -
    OP_MUL,             // *
    OP_DIV,             // /
    OP_MOD,             // %
    OP_NOT,             // !
    OP_NEGATE,          // - (一元負號)

    // 控制流跳轉 (位移量為 16-bit 無號整數)
    OP_JUMP,            // [arg: uint16_t offset] 無條件向前跳轉
    OP_JUMP_IF_FALSE,   // [arg: uint16_t offset] 棧頂為假則跳轉 (不彈棧)
    OP_LOOP,            // [arg: uint16_t offset] 向後循環跳轉
    OP_ITER_INIT,       // 初始化迭代器 (推入 index 0)
    OP_ITER_NEXT,       // [arg: uint16_t exitOffset] 取出下一項元素，若結束則跳出

    // 複合資料結構生成
    OP_BUILD_ARRAY,     // [arg: uint16_t count] 彈出 count 個元素生成 ObjArray
    OP_BUILD_MAP,       // [arg: uint16_t count] 彈出 count*2 個 (key, val) 生成 ObjMap
    OP_BUILD_RANGE,     // 彈出 (start, end) 生成 ObjRange

    // 容器存取與賦值 (索引運算子 a[i])
    OP_GET_INDEX,       // 彈出 (obj, index)，推入 obj[index]
    OP_SET_INDEX,       // 彈出 (obj, index, value)，執行 obj[index] = value

    // 函式與呼叫
    OP_CALL,            // [arg: uint8_t argCount] 呼叫函式
    OP_RETURN,          // 函式返回
    OP_CLOSURE,         // [arg: uint8_t fnConstant, uint8_t upvalueCount, (1 byte isLocal, 1 byte index)*] 建立閉包
    OP_GET_UPVALUE,     // [arg: uint8_t upvalueIndex] 讀取 Upvalue
    OP_SET_UPVALUE,     // [arg: uint8_t upvalueIndex] 寫入 Upvalue
    OP_CLOSE_UPVALUE,   // 關閉棧頂 Upvalue 並彈棧

    // 輸出與字串內插
    OP_STRING_CONCAT,   // 彈出兩個值，轉字串後相加
    OP_PRINT,           // 印出棧頂值並彈出

    // 物件導向 (OOP)
    OP_CLASS,           // [arg: uint8_t nameIndex] 建立類別元資料
    OP_INHERIT,         // 建立父子類繼承鏈
    OP_METHOD,          // [arg: uint8_t nameIndex] 將棧頂函式註冊至類別方法表
    OP_GET_PROPERTY,    // [arg: uint8_t nameIndex] 讀取實例屬性或綁定方法
    OP_SET_PROPERTY,    // [arg: uint8_t nameIndex] 寫入實例屬性
    OP_INVOKE,          // [arg: uint8_t nameIndex, uint8_t argCount] 最佳化方法調用
    OP_GET_SUPER,       // [arg: uint8_t nameIndex] 取得父類方法
    OP_SUPER_INVOKE,    // [arg: uint8_t nameIndex, uint8_t argCount] 最佳化父類方法調用

    // 模組系統 (Module System)
    OP_EXPORT,          // [arg: uint8_t nameIndex] 將棧頂符號匯出
    OP_IMPORT_MODULE,   // [arg: uint8_t pathIndex, uint8_t nameIndex] 匯入整個模組為變數
    OP_IMPORT_NAME,     // [arg: uint8_t pathIndex, uint8_t symbolIndex] 具名匯入模組符號
};

std::string_view opCodeName(OpCode op) noexcept;
