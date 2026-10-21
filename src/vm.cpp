#include "vm.hpp"
#include "compiler.hpp"
#include <cstdarg>
#include <chrono>
#include <sstream>
#include <cmath>
#include <fstream>

// -----------------------------------------------------------------------------
// 原生函式預設實作
// -----------------------------------------------------------------------------
namespace {

static VM* currentRunningVM = nullptr;

Value nativePrint(int argCount, Value* args) {
    for (int i = 0; i < argCount; ++i) {
        if (currentRunningVM) {
            std::cout << currentRunningVM->valueToString(args[i]);
        } else {
            std::cout << args[i];
        }
        if (i + 1 < argCount) std::cout << " ";
    }
    std::cout << std::endl;
    return Value();
}

Value nativeClock(int, Value*) {
    using namespace std::chrono;
    auto now = steady_clock::now().time_since_epoch();
    double seconds = duration<double>(now).count();
    return Value(seconds);
}

// 內建 Math 函式
Value mathSqrt(int argCount, Value* args) {
    if (argCount < 1 || !args[0].isNumber()) return Value(0.0);
    return Value(std::sqrt(*args[0].asNumber()));
}

Value mathPow(int argCount, Value* args) {
    if (argCount < 2 || !args[0].isNumber() || !args[1].isNumber()) return Value(0.0);
    return Value(std::pow(*args[0].asNumber(), *args[1].asNumber()));
}

Value mathAbs(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    if (args[0].isInt()) return Value(std::abs(*args[0].asInt()));
    if (args[0].isDouble()) return Value(std::abs(*args[0].asDouble()));
    return Value(0.0);
}

Value mathFloor(int argCount, Value* args) {
    if (argCount < 1 || !args[0].isNumber()) return Value(0.0);
    return Value(std::floor(*args[0].asNumber()));
}

Value mathCeil(int argCount, Value* args) {
    if (argCount < 1 || !args[0].isNumber()) return Value(0.0);
    return Value(std::ceil(*args[0].asNumber()));
}

} // namespace

VM::VM() {
    resetStack();
    defineNative("print", nativePrint, -1);
    defineNative("clock", nativeClock, 0);
    initBuiltinModules();
}

void VM::initBuiltinModules() {
    // 註冊 std/math 內建模組
    GcHandle mathHandle = heap.allocate<ObjModule>("std/math");
    auto* mathMod = heap.getAs<ObjModule>(mathHandle);

    auto addMathFn = [this, mathMod](const std::string& name, NativeFn fn, int arity) {
        GcHandle h = heap.allocate<ObjNativeFn>(fn, name, arity);
        mathMod->exports[name] = Value(h);
    };

    addMathFn("sqrt", mathSqrt, 1);
    addMathFn("pow", mathPow, 2);
    addMathFn("abs", mathAbs, 1);
    addMathFn("floor", mathFloor, 1);
    addMathFn("ceil", mathCeil, 1);

    mathMod->exports["PI"] = Value(3.141592653589793);
    mathMod->exports["E"] = Value(2.718281828459045);

    modules["std/math"] = mathHandle;
}

void VM::resetStack() {
    stack.clear();
    frames.clear();
}

void VM::push(Value value) {
    stack.push_back(value);
}

Value VM::pop() {
    Value top = stack.back();
    stack.pop_back();
    return top;
}

Value VM::peek(int distance) const {
    return stack[stack.size() - 1 - distance];
}

void VM::defineNative(const std::string& name, NativeFn function, int arity) {
    GcHandle handle = heap.allocate<ObjNativeFn>(function, name, arity);
    globals[name] = Value(handle);
}

std::optional<Value> VM::getGlobal(const std::string& name) const {
    auto it = globals.find(name);
    if (it != globals.end()) return it->second;
    return std::nullopt;
}

void VM::runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
        CallFrame& frame = frames[i];
        ObjFunction* fn = frame.function;
        size_t instruction = frame.ip - fn->bytecode.data() - 1;
        fprintf(stderr, "[line ?] in %s() (bytecode offset %zu)\n",
                fn->name.empty() ? "script" : fn->name.c_str(), instruction);
    }
    resetStack();
}

std::string VM::valueToString(Value value) {
    if (value.isObject()) {
        auto handle = value.asObject();
        if (handle) {
            GcObject* obj = heap.get(*handle);
            if (obj != nullptr) {
                if (auto* str = obj->as<ObjString>()) {
                    return str->text;
                }
                if (auto* inst = obj->as<ObjInstance>()) {
                    // 若實例有 toString 方法則調用
                    auto* cls = heap.getAs<ObjClass>(inst->klass);
                    if (cls) {
                        auto it = cls->methods.find("toString");
                        if (it != cls->methods.end()) {
                            // 預留可擴充 toString
                        }
                    }
                }
                std::ostringstream ss;
                obj->print(ss);
                return ss.str();
            }
        }
    }
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

bool VM::call(Value callee, uint8_t argCount) {
    if (!callee.isObject()) {
        runtimeError("Can only call functions and classes.");
        return false;
    }

    GcObject* obj = heap.get(*callee.asObject());
    if (obj == nullptr) {
        runtimeError("Calling invalid object handle.");
        return false;
    }

    // 1. 原生 C++ 函式
    if (auto* native = obj->as<ObjNativeFn>()) {
        if (native->arity != -1 && native->arity != argCount) {
            runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
            return false;
        }
        Value* args = &stack[stack.size() - argCount];
        Value result = native->function(argCount, args);
        stack.resize(stack.size() - argCount - 1);
        push(result);
        return true;
    }

    // 2. 類別實例化 (呼叫 Class 建立 Instance)
    if (auto* klass = obj->as<ObjClass>()) {
        GcHandle instanceHandle = heap.allocate<ObjInstance>(*callee.asObject());
        // 將原本棧上的 Class 物件替換為實例 (即建構子內部的 this)
        stack[stack.size() - argCount - 1] = Value(instanceHandle);

        auto it = klass->methods.find("init");
        if (it != klass->methods.end()) {
            return call(it->second, argCount);
        } else if (argCount != 0) {
            runtimeError("Expected 0 arguments but got %d.", argCount);
            return false;
        }
        return true;
    }

    // 3. 已綁定方法 (Bound Method)
    if (auto* bound = obj->as<ObjBoundMethod>()) {
        stack[stack.size() - argCount - 1] = bound->receiver; // 將 this 放入 slot 0
        return call(bound->method, argCount);
    }

    // 4. 腳本編譯函式 (Function)
    if (auto* fn = obj->as<ObjFunction>()) {
        if (argCount != fn->arity) {
            runtimeError("Expected %d arguments but got %d.", fn->arity, argCount);
            return false;
        }
        if (frames.size() >= 256) {
            runtimeError("Stack overflow.");
            return false;
        }
        frames.push_back(CallFrame{fn, fn->bytecode.data(), stack.size() - argCount - 1});
        return true;
    }

    runtimeError("Can only call functions and classes.");
    return false;
}

ObjModule* VM::loadModule(const std::string& path) {
    auto it = modules.find(path);
    if (it != modules.end()) {
        return heap.getAs<ObjModule>(it->second);
    }

    std::string filePath = path;
    if (!filePath.ends_with(".easy")) {
        filePath += ".easy";
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        if (filePath.starts_with("./")) {
            file.open(filePath.substr(2));
        }
    }

    if (!file.is_open()) {
        return nullptr;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    GcHandle modHandle = heap.allocate<ObjModule>(path);
    modules[path] = modHandle;
    auto* mod = heap.getAs<ObjModule>(modHandle);

    ObjModule* previousModule = currentModule;
    currentModule = mod;

    Compiler compiler(heap, source);
    ObjFunction* fn = compiler.compile();
    if (fn != nullptr) {
        size_t currentDepth = frames.size();
        push(Value(modHandle));
        frames.push_back(CallFrame{fn, fn->bytecode.data(), stack.size() - 1});
        run(currentDepth);
    }

    currentModule = previousModule;
    return mod;
}

InterpretResult VM::interpret(std::string_view source, const std::string& modulePath) {
    currentRunningVM = this;

    GcHandle modHandle = heap.allocate<ObjModule>(modulePath);
    modules[modulePath] = modHandle;
    currentModule = heap.getAs<ObjModule>(modHandle);

    Compiler compiler(heap, source);
    ObjFunction* fn = compiler.compile();
    if (fn == nullptr) {
        return InterpretResult::COMPILE_ERROR;
    }

    push(Value(heap.allocateString("<script>"))); // 槽位 0
    frames.push_back(CallFrame{fn, fn->bytecode.data(), 0});

    InterpretResult res = run();
    if (res != InterpretResult::OK) {
        return res;
    }

    // 若宣告了 fn main()，自動調用主進入點
    auto mainIt = globals.find("main");
    if (mainIt != globals.end() && mainIt->second.isObject()) {
        GcObject* obj = heap.get(*mainIt->second.asObject());
        if (obj && obj->is<ObjFunction>()) {
            push(mainIt->second);
            if (call(mainIt->second, 0)) {
                return run();
            }
        }
    }

    return InterpretResult::OK;
}

// -----------------------------------------------------------------------------
// 虛擬機主直譯迴圈 (Main VM Loop)
// -----------------------------------------------------------------------------
InterpretResult VM::run(size_t stopDepth) {
    CallFrame* frame = &frames.back();

    auto readByte = [&frame]() -> uint8_t {
        return *frame->ip++;
    };

    auto readShort = [&frame]() -> uint16_t {
        frame->ip += 2;
        return static_cast<uint16_t>((frame->ip[-2] << 8) | frame->ip[-1]);
    };

    auto readConstant = [&frame, &readByte]() -> Value {
        return frame->function->constants[readByte()];
    };

    while (true) {
        uint8_t instruction = readByte();

        switch (static_cast<OpCode>(instruction)) {
            case OpCode::OP_CONSTANT: {
                Value constant = readConstant();
                push(constant);
                break;
            }
            case OpCode::OP_NIL:   push(Value()); break;
            case OpCode::OP_TRUE:  push(Value(true)); break;
            case OpCode::OP_FALSE: push(Value(false)); break;
            case OpCode::OP_POP:   pop(); break;

            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                push(stack[frame->slotOffset + slot]);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                stack[frame->slotOffset + slot] = peek(0);
                break;
            }

            case OpCode::OP_DEFINE_GLOBAL: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                globals[strObj->text] = pop();
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                auto it = globals.find(strObj->text);
                if (it == globals.end()) {
                    runtimeError("Undefined variable '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                push(it->second);
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                auto it = globals.find(strObj->text);
                if (it == globals.end()) {
                    runtimeError("Undefined variable '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                it->second = peek(0);
                break;
            }

            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(Value(a == b));
                break;
            }
            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    push(Value(*a.asInt() > *b.asInt()));
                } else if (a.isNumber() && b.isNumber()) {
                    push(Value(*a.asNumber() > *b.asNumber()));
                } else {
                    runtimeError("Operands must be numbers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    push(Value(*a.asInt() < *b.asInt()));
                } else if (a.isNumber() && b.isNumber()) {
                    push(Value(*a.asNumber() < *b.asNumber()));
                } else {
                    runtimeError("Operands must be numbers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    push(Value(*a.asInt() + *b.asInt()));
                } else if (a.isNumber() && b.isNumber()) {
                    push(Value(*a.asNumber() + *b.asNumber()));
                } else {
                    std::string res = valueToString(a) + valueToString(b);
                    GcHandle handle = heap.allocateString(std::move(res));
                    push(Value(handle));
                }
                break;
            }
            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) push(Value(*a.asInt() - *b.asInt()));
                else if (a.isNumber() && b.isNumber()) push(Value(*a.asNumber() - *b.asNumber()));
                else {
                    runtimeError("Operands must be numbers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) push(Value(*a.asInt() * *b.asInt()));
                else if (a.isNumber() && b.isNumber()) push(Value(*a.asNumber() * *b.asNumber()));
                else {
                    runtimeError("Operands must be numbers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                if (b.isNumber() && *b.asNumber() == 0.0) {
                    runtimeError("Division by zero.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                if (a.isInt() && b.isInt()) push(Value(*a.asInt() / *b.asInt()));
                else if (a.isNumber() && b.isNumber()) push(Value(*a.asNumber() / *b.asNumber()));
                else {
                    runtimeError("Operands must be numbers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                if (a.isInt() && b.isInt()) {
                    if (*b.asInt() == 0) {
                        runtimeError("Modulo by zero.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(Value(*a.asInt() % *b.asInt()));
                } else {
                    runtimeError("Operands must be integers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_NOT: {
                push(Value(pop().isFalsey()));
                break;
            }
            case OpCode::OP_NEGATE: {
                Value val = pop();
                if (val.isInt()) push(Value(-*val.asInt()));
                else if (val.isDouble()) push(Value(-*val.asDouble()));
                else {
                    runtimeError("Operand must be a number.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            case OpCode::OP_JUMP: {
                uint16_t offset = readShort();
                frame->ip += offset;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readShort();
                if (peek(0).isFalsey()) {
                    frame->ip += offset;
                }
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = readShort();
                frame->ip -= offset;
                break;
            }

            case OpCode::OP_ITER_INIT: {
                push(Value(int64_t(0)));
                break;
            }

            case OpCode::OP_ITER_NEXT: {
                uint16_t exitOffset = readShort();
                Value indexVal = peek(0);
                Value iterVal = peek(1);

                if (!iterVal.isObject()) {
                    runtimeError("Cannot iterate over non-object value.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                GcObject* obj = heap.get(*iterVal.asObject());
                if (auto* arr = obj->as<ObjArray>()) {
                    int64_t idx = *indexVal.asInt();
                    if (idx >= static_cast<int64_t>(arr->elements.size())) {
                        frame->ip += exitOffset;
                    } else {
                        stack.back() = Value(idx + 1);
                        push(arr->elements[idx]);
                    }
                } else if (auto* rng = obj->as<ObjRange>()) {
                    int64_t stepIdx = *indexVal.asInt();
                    int64_t cur = rng->start + stepIdx * rng->step;
                    bool finished = (rng->step > 0) ? (cur >= rng->end) : (cur <= rng->end);
                    if (finished) {
                        frame->ip += exitOffset;
                    } else {
                        stack.back() = Value(stepIdx + 1);
                        push(Value(cur));
                    }
                } else {
                    runtimeError("Cannot iterate over non-iterable object.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            case OpCode::OP_BUILD_ARRAY: {
                uint16_t count = readShort();
                std::vector<Value> elements(count);
                for (int i = count - 1; i >= 0; --i) {
                    elements[i] = pop();
                }
                GcHandle handle = heap.allocateArray(std::move(elements));
                push(Value(handle));
                break;
            }
            case OpCode::OP_BUILD_MAP: {
                uint16_t count = readShort();
                std::unordered_map<Value, Value> table;
                for (int i = 0; i < count; ++i) {
                    Value val = pop();
                    Value key = pop();
                    table[key] = val;
                }
                GcHandle handle = heap.allocateMap(std::move(table));
                push(Value(handle));
                break;
            }
            case OpCode::OP_BUILD_RANGE: {
                Value endVal = pop();
                Value startVal = pop();
                if (!startVal.isInt() || !endVal.isInt()) {
                    runtimeError("Range bounds must be integers.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                GcHandle handle = heap.allocate<ObjRange>(*startVal.asInt(), *endVal.asInt());
                push(Value(handle));
                break;
            }

            case OpCode::OP_GET_INDEX: {
                Value index = pop();
                Value target = pop();
                if (!target.isObject()) {
                    runtimeError("Only objects can be subscripted.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                GcObject* obj = heap.get(*target.asObject());
                if (auto* arr = obj->as<ObjArray>()) {
                    if (!index.isInt()) {
                        runtimeError("Array index must be an integer.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    int64_t idx = *index.asInt();
                    if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
                        runtimeError("Array index out of bounds: %lld.", idx);
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    push(arr->elements[idx]);
                } else if (auto* map = obj->as<ObjMap>()) {
                    auto it = map->table.find(index);
                    if (it != map->table.end()) {
                        push(it->second);
                    } else {
                        push(Value());
                    }
                } else {
                    runtimeError("Target does not support indexing.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }
            case OpCode::OP_SET_INDEX: {
                Value val = pop();
                Value index = pop();
                Value target = pop();
                if (!target.isObject()) {
                    runtimeError("Only objects can be subscripted.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                GcObject* obj = heap.get(*target.asObject());
                if (auto* arr = obj->as<ObjArray>()) {
                    if (!index.isInt()) {
                        runtimeError("Array index must be an integer.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    int64_t idx = *index.asInt();
                    if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
                        runtimeError("Array index out of bounds.");
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    arr->elements[idx] = val;
                    push(val);
                } else if (auto* map = obj->as<ObjMap>()) {
                    map->table[index] = val;
                    push(val);
                } else {
                    runtimeError("Target does not support index assignment.");
                    return InterpretResult::RUNTIME_ERROR;
                }
                break;
            }

            case OpCode::OP_STRING_CONCAT: {
                Value b = pop();
                Value a = pop();
                std::string res = valueToString(a) + valueToString(b);
                GcHandle handle = heap.allocateString(std::move(res));
                push(Value(handle));
                break;
            }

            case OpCode::OP_PRINT: {
                Value val = pop();
                std::cout << valueToString(val) << std::endl;
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                Value callee = peek(argCount);
                if (!call(callee, argCount)) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                frame = &frames.back();
                break;
            }

            case OpCode::OP_RETURN: {
                Value result = pop();
                size_t slotOffset = frame->slotOffset;
                frames.pop_back();

                if (frames.size() == stopDepth) {
                    stack.resize(slotOffset);
                    return InterpretResult::OK;
                }

                stack.resize(slotOffset);
                push(result);
                frame = &frames.back();
                break;
            }

            // -----------------------------------------------------------------
            // 物件導向 (OOP) 指令執行
            // -----------------------------------------------------------------
            case OpCode::OP_CLASS: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                GcHandle classHandle = heap.allocate<ObjClass>(strObj->text);
                push(Value(classHandle));
                break;
            }

            case OpCode::OP_INHERIT: {
                Value subclassVal = peek(0);
                Value superclassVal = peek(1);

                if (!superclassVal.isObject() || !heap.get(*superclassVal.asObject())->is<ObjClass>()) {
                    runtimeError("Superclass must be a class.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto* superclass = heap.getAs<ObjClass>(*superclassVal.asObject());
                auto* subclass = heap.getAs<ObjClass>(*subclassVal.asObject());

                subclass->superclass = *superclassVal.asObject();

                // 複製父類方法表至子類 (方法覆寫 override 則直接覆蓋同名鍵)
                for (const auto& [methodName, methodVal] : superclass->methods) {
                    subclass->methods[methodName] = methodVal;
                }

                pop(); // 彈出子類
                break;
            }

            case OpCode::OP_METHOD: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());

                Value methodVal = peek(0);
                Value classVal = peek(1);

                auto* klass = heap.getAs<ObjClass>(*classVal.asObject());
                klass->methods[strObj->text] = methodVal;

                pop(); // 彈出 method 函式
                break;
            }

            case OpCode::OP_GET_PROPERTY: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());

                Value target = pop();
                if (!target.isObject()) {
                    runtimeError("Only instances and modules have properties.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 支援 ObjModule 屬性存取 (e.g. Math.PI)
                if (auto* mod = heap.getAs<ObjModule>(*target.asObject())) {
                    auto it = mod->exports.find(strObj->text);
                    if (it != mod->exports.end()) {
                        push(it->second);
                        break;
                    }
                    runtimeError("Module '%s' does not export '%s'.", mod->path.c_str(), strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto* instance = heap.getAs<ObjInstance>(*target.asObject());
                if (!instance) {
                    runtimeError("Only instances and modules have properties.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 1. 先查實例欄位 (Fields)
                auto it = instance->fields.find(strObj->text);
                if (it != instance->fields.end()) {
                    push(it->second);
                    break;
                }

                // 2. 再查類別方法 (Methods) -> 生成 ObjBoundMethod
                auto* klass = heap.getAs<ObjClass>(instance->klass);
                if (klass) {
                    auto mIt = klass->methods.find(strObj->text);
                    if (mIt != klass->methods.end()) {
                        GcHandle bound = heap.allocate<ObjBoundMethod>(target, mIt->second);
                        push(Value(bound));
                        break;
                    }
                }

                runtimeError("Undefined property '%s'.", strObj->text.c_str());
                return InterpretResult::RUNTIME_ERROR;
            }

            case OpCode::OP_SET_PROPERTY: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());

                Value val = pop();
                Value target = pop();

                if (!target.isObject()) {
                    runtimeError("Only instances have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto* instance = heap.getAs<ObjInstance>(*target.asObject());
                if (!instance) {
                    runtimeError("Only instances have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                instance->fields[strObj->text] = val;
                push(val);
                break;
            }

            case OpCode::OP_INVOKE: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                uint8_t argCount = readByte();

                Value receiver = peek(argCount);
                if (!receiver.isObject()) {
                    runtimeError("Only instances and modules have methods.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 支援模組函式調用 (e.g. Math.sqrt(16))
                if (auto* mod = heap.getAs<ObjModule>(*receiver.asObject())) {
                    auto it = mod->exports.find(strObj->text);
                    if (it != mod->exports.end()) {
                        stack[stack.size() - argCount - 1] = it->second;
                        if (!call(it->second, argCount)) {
                            return InterpretResult::RUNTIME_ERROR;
                        }
                        frame = &frames.back();
                        break;
                    }
                    runtimeError("Module '%s' does not export '%s'.", mod->path.c_str(), strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto* instance = heap.getAs<ObjInstance>(*receiver.asObject());
                if (!instance) {
                    runtimeError("Only instances and modules have methods.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 先查是否有欄位覆蓋函式
                auto it = instance->fields.find(strObj->text);
                if (it != instance->fields.end()) {
                    stack[stack.size() - argCount - 1] = it->second;
                    if (!call(it->second, argCount)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    frame = &frames.back();
                    break;
                }

                // 查詢類別方法並直接呼叫
                auto* klass = heap.getAs<ObjClass>(instance->klass);
                if (klass) {
                    auto mIt = klass->methods.find(strObj->text);
                    if (mIt != klass->methods.end()) {
                        if (!call(mIt->second, argCount)) {
                            return InterpretResult::RUNTIME_ERROR;
                        }
                        frame = &frames.back();
                        break;
                    }
                }

                runtimeError("Undefined method '%s'.", strObj->text.c_str());
                return InterpretResult::RUNTIME_ERROR;
            }

            case OpCode::OP_GET_SUPER: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                Value receiver = pop();

                auto* instance = heap.getAs<ObjInstance>(*receiver.asObject());
                auto* klass = heap.getAs<ObjClass>(instance->klass);

                if (klass->superclass) {
                    auto* superclass = heap.getAs<ObjClass>(*klass->superclass);
                    auto it = superclass->methods.find(strObj->text);
                    if (it != superclass->methods.end()) {
                        GcHandle bound = heap.allocate<ObjBoundMethod>(receiver, it->second);
                        push(Value(bound));
                        break;
                    }
                }

                runtimeError("Undefined superclass method '%s'.", strObj->text.c_str());
                return InterpretResult::RUNTIME_ERROR;
            }

            case OpCode::OP_SUPER_INVOKE: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                uint8_t argCount = readByte();

                Value receiver = peek(argCount);
                auto* instance = heap.getAs<ObjInstance>(*receiver.asObject());
                auto* klass = heap.getAs<ObjClass>(instance->klass);

                if (klass->superclass) {
                    auto* superclass = heap.getAs<ObjClass>(*klass->superclass);
                    auto it = superclass->methods.find(strObj->text);
                    if (it != superclass->methods.end()) {
                        if (!call(it->second, argCount)) {
                            return InterpretResult::RUNTIME_ERROR;
                        }
                        frame = &frames.back();
                        break;
                    }
                }

                runtimeError("Undefined superclass method '%s'.", strObj->text.c_str());
                return InterpretResult::RUNTIME_ERROR;
            }

            // -----------------------------------------------------------------
            // 模組系統 (Module System) 指令執行
            // -----------------------------------------------------------------
            case OpCode::OP_EXPORT: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                if (currentModule) {
                    auto it = globals.find(strObj->text);
                    if (it != globals.end()) {
                        currentModule->exports[strObj->text] = it->second;
                    }
                }
                break;
            }

            case OpCode::OP_IMPORT_MODULE: {
                Value pathVal = readConstant();
                Value nameVal = readConstant();
                auto pathStr = heap.getAs<ObjString>(*pathVal.asObject());
                auto nameStr = heap.getAs<ObjString>(*nameVal.asObject());

                ObjModule* mod = loadModule(pathStr->text);
                frame = &frames.back();
                if (!mod) {
                    runtimeError("Could not load module '%s'.", pathStr->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                globals[nameStr->text] = Value(modules[pathStr->text]);
                break;
            }

            case OpCode::OP_IMPORT_NAME: {
                Value pathVal = readConstant();
                Value symVal = readConstant();
                auto pathStr = heap.getAs<ObjString>(*pathVal.asObject());
                auto symStr = heap.getAs<ObjString>(*symVal.asObject());

                ObjModule* mod = loadModule(pathStr->text);
                frame = &frames.back();
                if (!mod) {
                    runtimeError("Could not load module '%s'.", pathStr->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                auto it = mod->exports.find(symStr->text);
                if (it == mod->exports.end()) {
                    runtimeError("Module '%s' does not export '%s'.", pathStr->text.c_str(), symStr->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                globals[symStr->text] = it->second;
                break;
            }

            default:
                runtimeError("Unknown opcode: %d", instruction);
                return InterpretResult::RUNTIME_ERROR;
        }
    }
}
