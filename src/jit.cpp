#include "jit.hpp"
#include "vm.hpp"
#include "opcode.hpp"
#include "objects/obj_string.hpp"
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstring>
#include <iostream>

// -----------------------------------------------------------------------------
// JIT 記憶體管理 (Windows VirtualAlloc / VirtualFree)
// -----------------------------------------------------------------------------
JitMemoryManager& JitMemoryManager::instance() {
    static JitMemoryManager inst;
    return inst;
}

void* JitMemoryManager::allocateExecutable(size_t size) {
    if (size == 0) return nullptr;
    void* mem = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (mem) {
        allocations.push_back({mem, size});
    }
    return mem;
}

void JitMemoryManager::freeAll() {
    for (auto& [ptr, size] : allocations) {
        if (ptr) {
            VirtualFree(ptr, 0, MEM_RELEASE);
        }
    }
    allocations.clear();
}

JitMemoryManager::~JitMemoryManager() {
    freeAll();
}

// -----------------------------------------------------------------------------
// JIT C++ 執行期輔助函式 (Trampoline Helpers)
// -----------------------------------------------------------------------------
extern "C" {

void easyscript_jit_add(VM* vm, Value* a, const Value* b) {
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() + *b->asInt());
    } else if (a->isDouble() && b->isDouble()) {
        *a = Value(*a->asDouble() + *b->asDouble());
    } else if (a->isInt() && b->isDouble()) {
        *a = Value(static_cast<double>(*a->asInt()) + *b->asDouble());
    } else if (a->isDouble() && b->isInt()) {
        *a = Value(*a->asDouble() + static_cast<double>(*b->asInt()));
    } else if (vm) {
        std::string res = vm->valueToString(*a) + vm->valueToString(*b);
        GcHandle handle = vm->getHeap().allocateString(std::move(res));
        *a = Value(handle);
    }
}

void easyscript_jit_sub(VM* vm, Value* a, const Value* b) {
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() - *b->asInt());
    } else if (a->isDouble() && b->isDouble()) {
        *a = Value(*a->asDouble() - *b->asDouble());
    } else if (a->isInt() && b->isDouble()) {
        *a = Value(static_cast<double>(*a->asInt()) - *b->asDouble());
    } else if (a->isDouble() && b->isInt()) {
        *a = Value(*a->asDouble() - static_cast<double>(*b->asInt()));
    } else if (vm) {
        // 非數字型別報錯
        *a = Value();
    }
}

void easyscript_jit_mul(VM* vm, Value* a, const Value* b) {
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() * *b->asInt());
    } else if (a->isDouble() && b->isDouble()) {
        *a = Value(*a->asDouble() * *b->asDouble());
    } else if (a->isInt() && b->isDouble()) {
        *a = Value(static_cast<double>(*a->asInt()) * *b->asDouble());
    } else if (a->isDouble() && b->isInt()) {
        *a = Value(*a->asDouble() * static_cast<double>(*b->asInt()));
    } else if (vm) {
        *a = Value();
    }
}

void easyscript_jit_div(VM* vm, Value* a, const Value* b) {
    if (b->isNumber() && *b->asNumber() == 0.0) {
        *a = Value();
        return;
    }
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() / *b->asInt());
    } else if (a->isNumber() && b->isNumber()) {
        *a = Value(*a->asNumber() / *b->asNumber());
    } else {
        *a = Value();
    }
}

void easyscript_jit_mod(VM* vm, Value* a, const Value* b) {
    if (b->isNumber() && *b->asNumber() == 0.0) {
        *a = Value();
        return;
    }
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() % *b->asInt());
    } else if (a->isDouble() && b->isDouble()) {
        *a = Value(std::fmod(*a->asDouble(), *b->asDouble()));
    } else {
        *a = Value();
    }
}

void easyscript_jit_negate(Value* a) {
    if (a->isInt()) *a = Value(-*a->asInt());
    else if (a->isDouble()) *a = Value(-*a->asDouble());
}

void easyscript_jit_not(Value* a) {
    *a = Value(a->isFalsey());
}

void easyscript_jit_equal(Value* a, const Value* b) {
    *a = Value(*a == *b);
}

void easyscript_jit_less(Value* a, const Value* b) {
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() < *b->asInt());
    } else if (a->isNumber() && b->isNumber()) {
        *a = Value(*a->asNumber() < *b->asNumber());
    } else {
        *a = Value(false);
    }
}

void easyscript_jit_greater(Value* a, const Value* b) {
    if (a->isInt() && b->isInt()) {
        *a = Value(*a->asInt() > *b->asInt());
    } else if (a->isNumber() && b->isNumber()) {
        *a = Value(*a->asNumber() > *b->asNumber());
    } else {
        *a = Value(false);
    }
}

int64_t easyscript_jit_call(VM* vm, Value* calleeSlot, uint8_t argCount) {
    if (!vm) return 0;
    return vm->callFromJit(calleeSlot, argCount) ? 1 : 0;
}

void easyscript_jit_get_global(VM* vm, const Value* nameVal, Value* dest) {
    if (!vm || !nameVal || !dest) return;
    *dest = vm->jitGetGlobal(*nameVal);
}

void easyscript_jit_set_global(VM* vm, const Value* nameVal, const Value* val) {
    if (!vm || !nameVal || !val) return;
    vm->jitSetGlobal(*nameVal, *val);
}

void easyscript_jit_define_global(VM* vm, const Value* nameVal, const Value* val) {
    if (!vm || !nameVal || !val) return;
    vm->jitDefineGlobal(*nameVal, *val);
}

} // extern "C"

// -----------------------------------------------------------------------------
// x86_64 機械碼發射器 (X64 Assembler)
// -----------------------------------------------------------------------------
namespace {

class X64Assembler {
public:
    std::vector<uint8_t> buffer;

    void emitByte(uint8_t b) { buffer.push_back(b); }
    void emitWord(uint16_t w) {
        emitByte(w & 0xFF);
        emitByte((w >> 8) & 0xFF);
    }
    void emitDword(uint32_t d) {
        emitByte(d & 0xFF);
        emitByte((d >> 8) & 0xFF);
        emitByte((d >> 16) & 0xFF);
        emitByte((d >> 24) & 0xFF);
    }
    void emitQword(uint64_t q) {
        emitDword(static_cast<uint32_t>(q & 0xFFFFFFFF));
        emitDword(static_cast<uint32_t>((q >> 32) & 0xFFFFFFFF));
    }

    size_t offset() const { return buffer.size(); }

    void patchByte(size_t pos, uint8_t val) {
        buffer[pos] = val;
    }

    void patchDword(size_t pos, uint32_t val) {
        buffer[pos] = val & 0xFF;
        buffer[pos + 1] = (val >> 8) & 0xFF;
        buffer[pos + 2] = (val >> 16) & 0xFF;
        buffer[pos + 3] = (val >> 24) & 0xFF;
    }

    // 指令簡捷發射器
    void emitMovRcxR12() {
        // mov rcx, r12
        emitByte(0x49); emitByte(0x8B); emitByte(0xCC);
    }

    void emitMovRdxRsi() {
        // mov rdx, rsi
        emitByte(0x48); emitByte(0x8B); emitByte(0xD6);
    }

    void emitLeaRdxRsiDisp8(int8_t disp) {
        // lea rdx, [rsi + disp8]
        emitByte(0x48); emitByte(0x8D); emitByte(0x56); emitByte(static_cast<uint8_t>(disp));
    }

    void emitLeaRdxRsiDisp32(int32_t disp) {
        // lea rdx, [rsi + disp32]
        emitByte(0x48); emitByte(0x8D); emitByte(0x96); emitDword(static_cast<uint32_t>(disp));
    }

    void emitMovRdxImm64(uint64_t imm) {
        // mov rdx, imm64
        emitByte(0x48); emitByte(0xBA); emitQword(imm);
    }

    void emitMovR8Rsi() {
        // mov r8, rsi
        emitByte(0x4C); emitByte(0x8B); emitByte(0xC6);
    }

    void emitLeaR8RsiDisp8(int8_t disp) {
        // lea r8, [rsi + disp8]
        emitByte(0x4C); emitByte(0x8D); emitByte(0x46); emitByte(static_cast<uint8_t>(disp));
    }

    void emitMovR8dImm32(uint32_t imm) {
        // mov r8d, imm32
        emitByte(0x41); emitByte(0xB8); emitDword(imm);
    }

    void emitMovRaxImm64(uint64_t imm) {
        // mov rax, imm64
        emitByte(0x48); emitByte(0xB8); emitQword(imm);
    }

    void emitCallRax() {
        // call rax
        emitByte(0xFF); emitByte(0xD0);
    }
};

} // namespace

// -----------------------------------------------------------------------------
// JIT 編譯器實作
// -----------------------------------------------------------------------------
bool JitCompiler::canCompile(const ObjFunction* fn) {
    if (fn == nullptr || fn->bytecode.empty()) return false;
    if (fn->upvalueCount > 0) return false; // 帶有閉包 Upvalue 的函式落回直譯器

    const auto& code = fn->bytecode;
    size_t i = 0;
    while (i < code.size()) {
        OpCode op = static_cast<OpCode>(code[i++]);
        switch (op) {
            case OpCode::OP_CONSTANT: i += 1; break;
            case OpCode::OP_NIL:
            case OpCode::OP_TRUE:
            case OpCode::OP_FALSE:
            case OpCode::OP_POP: break;
            case OpCode::OP_GET_LOCAL:
            case OpCode::OP_SET_LOCAL: i += 1; break;
            case OpCode::OP_GET_GLOBAL:
            case OpCode::OP_SET_GLOBAL:
            case OpCode::OP_DEFINE_GLOBAL: i += 1; break;
            case OpCode::OP_EQUAL:
            case OpCode::OP_GREATER:
            case OpCode::OP_LESS:
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV:
            case OpCode::OP_MOD:
            case OpCode::OP_NOT:
            case OpCode::OP_NEGATE: break;
            case OpCode::OP_JUMP:
            case OpCode::OP_JUMP_IF_FALSE:
            case OpCode::OP_LOOP: i += 2; break;
            case OpCode::OP_CALL: i += 1; break;
            case OpCode::OP_RETURN: break;
            default:
                return false;
        }
    }
    return true;
}

bool JitCompiler::compile(ObjFunction* fn) {
    if (!canCompile(fn)) {
        fn->jitFailed = true;
        return false;
    }

    X64Assembler a;

    // 1. Prologue (Windows x64 ABI)
    // push rbp; mov rbp, rsp
    a.emitByte(0x55);
    a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0xE5);

    // push rbx, rsi, rdi, r12, r13, r14, r15
    a.emitByte(0x53);
    a.emitByte(0x56);
    a.emitByte(0x57);
    a.emitByte(0x41); a.emitByte(0x54);
    a.emitByte(0x41); a.emitByte(0x55);
    a.emitByte(0x41); a.emitByte(0x56);
    a.emitByte(0x41); a.emitByte(0x57);

    // sub rsp, 4128 (4096 bytes for 256 local/eval slots + 32 shadow space)
    a.emitByte(0x48); a.emitByte(0x81); a.emitByte(0xEC);
    a.emitDword(4128);

    // mov r12, rcx (VM* vm)
    a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0xCC);

    // mov r14, r8  (Value* outResult)
    a.emitByte(0x4D); a.emitByte(0x89); a.emitByte(0xC6);

    // lea r13, [rsp + 32] (frameSlots base)
    a.emitByte(0x4C); a.emitByte(0x8D); a.emitByte(0x6C); a.emitByte(0x24); a.emitByte(0x20);

    // 複製傳入參數到 frameSlots (slot 0 是 callee，slot 1..arity 是引數)
    // callerSlots 在 RDX
    uint32_t initSlots = static_cast<uint32_t>(fn->arity + 1);
    for (uint32_t i = 0; i < initSlots; ++i) {
        uint32_t offset = i * 16;
        // mov rax, [rdx + offset]
        a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x82); a.emitDword(offset);
        // mov [r13 + offset], rax
        a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x85); a.emitDword(offset);
        // mov rax, [rdx + offset + 8]
        a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x82); a.emitDword(offset + 8);
        // mov [r13 + offset + 8], rax
        a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x85); a.emitDword(offset + 8);
    }

    // lea rsi, [r13 + initSlots * 16] (初始運算元棧頂部)
    uint32_t initStackDisp = initSlots * 16;
    a.emitByte(0x49); a.emitByte(0x8D); a.emitByte(0xB5); a.emitDword(initStackDisp);

    std::unordered_map<size_t, size_t> bytecodeToNative;
    struct PendingJump {
        size_t patchPos;
        size_t targetBcOffset;
    };
    std::vector<PendingJump> pendingJumps;
    std::vector<size_t> jumpsToEpilogue;
    std::vector<size_t> jumpsToBailout;

    auto emitJumpToEpilogue = [&]() {
        a.emitByte(0xE9);
        jumpsToEpilogue.push_back(a.offset());
        a.emitDword(0);
    };

    auto emitJumpToBailout = [&]() {
        a.emitByte(0xE9);
        jumpsToBailout.push_back(a.offset());
        a.emitDword(0);
    };

    const auto& code = fn->bytecode;
    size_t ip = 0;

    while (ip < code.size()) {
        size_t currentBcOffset = ip;
        bytecodeToNative[currentBcOffset] = a.offset();

        OpCode op = static_cast<OpCode>(code[ip++]);

        switch (op) {
            case OpCode::OP_CONSTANT: {
                uint8_t constIdx = code[ip++];
                uint64_t addr = reinterpret_cast<uint64_t>(&fn->constants[constIdx]);
                // mov rbx, imm64
                a.emitByte(0x48); a.emitByte(0xBB); a.emitQword(addr);
                // mov rax, [rbx]; mov rdx, [rbx + 8]
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x03);
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x53); a.emitByte(0x08);
                // mov [rsi], rax; mov [rsi + 8], rdx; add rsi, 16
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x56); a.emitByte(0x08);
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_NIL: {
                // mov qword ptr [rsi], 0; mov qword ptr [rsi + 8], 0; add rsi, 16
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x06); a.emitDword(0);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0x08); a.emitDword(0);
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_TRUE: {
                // mov qword ptr [rsi], 1; mov qword ptr [rsi + 8], 1 (bool tag); add rsi, 16
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x06); a.emitDword(1);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0x08); a.emitDword(1);
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_FALSE: {
                // mov qword ptr [rsi], 0; mov qword ptr [rsi + 8], 1 (bool tag); add rsi, 16
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x06); a.emitDword(0);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0x08); a.emitDword(1);
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_POP: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = code[ip++];
                uint32_t slotOffset = static_cast<uint32_t>(slot * 16);
                // mov rax, [r13 + slotOffset]
                a.emitByte(0x49); a.emitByte(0x8B); a.emitByte(0x85); a.emitDword(slotOffset);
                // mov rdx, [r13 + slotOffset + 8]
                a.emitByte(0x49); a.emitByte(0x8B); a.emitByte(0x95); a.emitDword(slotOffset + 8);
                // mov [rsi], rax; mov [rsi + 8], rdx; add rsi, 16
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x56); a.emitByte(0x08);
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = code[ip++];
                uint32_t slotOffset = static_cast<uint32_t>(slot * 16);
                // mov rax, [rsi - 16]
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                // mov rdx, [rsi - 8]
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x56); a.emitByte(0xF8);
                // mov [r13 + slotOffset], rax
                a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x85); a.emitDword(slotOffset);
                // mov [r13 + slotOffset + 8], rdx
                a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x95); a.emitDword(slotOffset + 8);
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t constIdx = code[ip++];
                uint64_t constAddr = reinterpret_cast<uint64_t>(&fn->constants[constIdx]);
                // mov rcx, r12 (vm)
                a.emitMovRcxR12();
                // mov rdx, imm64 (constAddr)
                a.emitMovRdxImm64(constAddr);
                // mov r8, rsi (dest)
                a.emitMovR8Rsi();
                // call easyscript_jit_get_global
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_get_global));
                a.emitCallRax();
                // add rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xC6); a.emitByte(0x10);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t constIdx = code[ip++];
                uint64_t constAddr = reinterpret_cast<uint64_t>(&fn->constants[constIdx]);
                // mov rcx, r12 (vm)
                a.emitMovRcxR12();
                // mov rdx, imm64 (constAddr)
                a.emitMovRdxImm64(constAddr);
                // lea r8, [rsi - 16] (val)
                a.emitLeaR8RsiDisp8(-16);
                // call easyscript_jit_set_global
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_set_global));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_DEFINE_GLOBAL: {
                uint8_t constIdx = code[ip++];
                uint64_t constAddr = reinterpret_cast<uint64_t>(&fn->constants[constIdx]);
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // mov rcx, r12 (vm)
                a.emitMovRcxR12();
                // mov rdx, imm64 (constAddr)
                a.emitMovRdxImm64(constAddr);
                // mov r8, rsi (val)
                a.emitMovR8Rsi();
                // call easyscript_jit_define_global
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_define_global));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_ADD: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2 (is a int?)
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2 (is b int?)
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: mov rax, [rsi - 16]; add rax, [rsi]; mov [rsi - 16], rax
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x03); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                // Fallback path
                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                // mov rcx, r12 (vm)
                a.emitMovRcxR12();
                // lea rdx, [rsi - 16] (a)
                a.emitLeaRdxRsiDisp8(-16);
                // mov r8, rsi (b)
                a.emitMovR8Rsi();
                // call easyscript_jit_add
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_add));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_SUB: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: mov rax, [rsi - 16]; sub rax, [rsi]; mov [rsi - 16], rax
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x2B); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                a.emitMovRcxR12();
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_sub));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_MUL: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: mov rax, [rsi - 16]; imul rax, [rsi]; mov [rsi - 16], rax
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x0F); a.emitByte(0xAF); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                a.emitMovRcxR12();
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_mul));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_DIV: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                a.emitMovRcxR12();
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_div));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_MOD: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                a.emitMovRcxR12();
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_mod));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_NEGATE: {
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_negate));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_NOT: {
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_not));
                a.emitCallRax();
                break;
            }
            case OpCode::OP_EQUAL: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: cmp rax, [rsi]; sete al; movzx rax, al; mov [rsi - 16], rax; mov qword ptr [rsi - 8], 1
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x3B); a.emitByte(0x06);
                a.emitByte(0x0F); a.emitByte(0x94); a.emitByte(0xC0); // sete al
                a.emitByte(0x48); a.emitByte(0x0F); a.emitByte(0xB6); a.emitByte(0xC0);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0xF8); a.emitDword(1);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_equal));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_LESS: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: cmp rax, [rsi]; setl al
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x3B); a.emitByte(0x06);
                a.emitByte(0x0F); a.emitByte(0x9C); a.emitByte(0xC0); // setl al
                a.emitByte(0x48); a.emitByte(0x0F); a.emitByte(0xB6); a.emitByte(0xC0);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0xF8); a.emitDword(1);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_less));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_GREATER: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // cmp byte ptr [rsi - 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne1 = a.offset(); a.emitByte(0x00);
                // cmp byte ptr [rsi + 8], 2
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0x08); a.emitByte(0x02);
                a.emitByte(0x75); size_t jne2 = a.offset(); a.emitByte(0x00);

                // Fast path: cmp rax, [rsi]; setg al
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0x3B); a.emitByte(0x06);
                a.emitByte(0x0F); a.emitByte(0x9F); a.emitByte(0xC0); // setg al
                a.emitByte(0x48); a.emitByte(0x0F); a.emitByte(0xB6); a.emitByte(0xC0);
                a.emitByte(0x48); a.emitByte(0x89); a.emitByte(0x46); a.emitByte(0xF0);
                a.emitByte(0x48); a.emitByte(0xC7); a.emitByte(0x46); a.emitByte(0xF8); a.emitDword(1);
                a.emitByte(0xEB); size_t jmpDone = a.offset(); a.emitByte(0x00);

                size_t fallbackStart = a.offset();
                a.patchByte(jne1, static_cast<uint8_t>(fallbackStart - (jne1 + 1)));
                a.patchByte(jne2, static_cast<uint8_t>(fallbackStart - (jne2 + 1)));
                a.emitLeaRdxRsiDisp8(-16);
                a.emitMovR8Rsi();
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_greater));
                a.emitCallRax();

                size_t doneStart = a.offset();
                a.patchByte(jmpDone, static_cast<uint8_t>(doneStart - (jmpDone + 1)));
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = (code[ip] << 8) | code[ip + 1];
                ip += 2;
                size_t targetBc = ip + offset;

                // peek [rsi - 16]
                // cmp byte ptr [rsi - 8], 0 (null?)
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x00);
                a.emitByte(0x0F); a.emitByte(0x84);
                pendingJumps.push_back({a.offset(), targetBc});
                a.emitDword(0);

                // cmp byte ptr [rsi - 8], 1 (bool?)
                a.emitByte(0x80); a.emitByte(0x7E); a.emitByte(0xF8); a.emitByte(0x01);
                a.emitByte(0x75); size_t jneNotFalse = a.offset(); a.emitByte(0x00);

                // cmp qword ptr [rsi - 16], 0 (false?)
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0x7E); a.emitByte(0xF0); a.emitByte(0x00);
                a.emitByte(0x0F); a.emitByte(0x84);
                pendingJumps.push_back({a.offset(), targetBc});
                a.emitDword(0);

                size_t notFalseLabel = a.offset();
                a.patchByte(jneNotFalse, static_cast<uint8_t>(notFalseLabel - (jneNotFalse + 1)));
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = (code[ip] << 8) | code[ip + 1];
                ip += 2;
                size_t targetBc = ip + offset;

                a.emitByte(0xE9);
                pendingJumps.push_back({a.offset(), targetBc});
                a.emitDword(0);
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = (code[ip] << 8) | code[ip + 1];
                ip += 2;
                size_t targetBc = ip - offset;

                a.emitByte(0xE9);
                if (bytecodeToNative.find(targetBc) != bytecodeToNative.end()) {
                    size_t targetNative = bytecodeToNative[targetBc];
                    int32_t rel = static_cast<int32_t>(targetNative) - static_cast<int32_t>(a.offset() + 4);
                    a.emitDword(static_cast<uint32_t>(rel));
                } else {
                    pendingJumps.push_back({a.offset(), targetBc});
                    a.emitDword(0);
                }
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t argCount = code[ip++];
                int32_t calleeDisp = -static_cast<int32_t>((argCount + 1) * 16);

                // mov rcx, r12 (vm)
                a.emitMovRcxR12();
                // lea rdx, [rsi + calleeDisp]
                a.emitLeaRdxRsiDisp32(calleeDisp);
                // mov r8d, argCount
                a.emitMovR8dImm32(argCount);
                // call easyscript_jit_call
                a.emitMovRaxImm64(reinterpret_cast<uint64_t>(&easyscript_jit_call));
                a.emitCallRax();
                // test eax, eax
                a.emitByte(0x85); a.emitByte(0xC0);
                // jz bailout
                a.emitByte(0x0F); a.emitByte(0x84);
                jumpsToBailout.push_back(a.offset());
                a.emitDword(0);

                // sub rsi, argCount * 16
                uint32_t shrink = argCount * 16;
                a.emitByte(0x48); a.emitByte(0x81); a.emitByte(0xEE); a.emitDword(shrink);
                break;
            }
            case OpCode::OP_RETURN: {
                // sub rsi, 16
                a.emitByte(0x48); a.emitByte(0x83); a.emitByte(0xEE); a.emitByte(0x10);
                // mov rax, [rsi]; mov rdx, [rsi + 8]
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x06);
                a.emitByte(0x48); a.emitByte(0x8B); a.emitByte(0x56); a.emitByte(0x08);
                // mov [r14], rax; mov [r14 + 8], rdx
                a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x06);
                a.emitByte(0x49); a.emitByte(0x89); a.emitByte(0x56); a.emitByte(0x08);
                // mov eax, 0 (JIT_OK)
                a.emitByte(0xB8); a.emitDword(0);
                emitJumpToEpilogue();
                break;
            }
            default:
                fn->jitFailed = true;
                return false;
        }
    }

    // 2. Bailout Handler
    size_t bailoutOffset = a.offset();
    for (size_t patchPos : jumpsToBailout) {
        int32_t rel = static_cast<int32_t>(bailoutOffset) - static_cast<int32_t>(patchPos + 4);
        a.patchDword(patchPos, static_cast<uint32_t>(rel));
    }
    // mov eax, 1 (JIT_BAILOUT)
    a.emitByte(0xB8); a.emitDword(1);

    // 3. Epilogue
    size_t epilogueOffset = a.offset();
    for (size_t patchPos : jumpsToEpilogue) {
        int32_t rel = static_cast<int32_t>(epilogueOffset) - static_cast<int32_t>(patchPos + 4);
        a.patchDword(patchPos, static_cast<uint32_t>(rel));
    }

    // add rsp, 4128
    a.emitByte(0x48); a.emitByte(0x81); a.emitByte(0xC4); a.emitDword(4128);
    // pop r15, r14, r13, r12, rdi, rsi, rbx, rbp
    a.emitByte(0x41); a.emitByte(0x5F);
    a.emitByte(0x41); a.emitByte(0x5E);
    a.emitByte(0x41); a.emitByte(0x5D);
    a.emitByte(0x41); a.emitByte(0x5C);
    a.emitByte(0x5F);
    a.emitByte(0x5E);
    a.emitByte(0x5B);
    a.emitByte(0x5D);
    a.emitByte(0xC3); // ret

    // 4. Backpatch all pending bytecode jumps
    for (const auto& pj : pendingJumps) {
        auto it = bytecodeToNative.find(pj.targetBcOffset);
        size_t targetNative = (it != bytecodeToNative.end()) ? it->second : epilogueOffset;
        int32_t rel = static_cast<int32_t>(targetNative) - static_cast<int32_t>(pj.patchPos + 4);
        a.patchDword(pj.patchPos, static_cast<uint32_t>(rel));
    }

    // 5. 分配可執行記憶體並複製機器碼
    void* execMem = JitMemoryManager::instance().allocateExecutable(a.buffer.size());
    if (!execMem) {
        fn->jitFailed = true;
        return false;
    }

    std::memcpy(execMem, a.buffer.data(), a.buffer.size());
    fn->nativeCode = execMem;
    return true;
}
