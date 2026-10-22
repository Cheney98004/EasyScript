#include "vm.hpp"
#include "compiler.hpp"
#include "serializer.hpp"
#include "jit.hpp"
#include "dynamic_lib.hpp"
#include <cstdarg>
#include <chrono>
#include <sstream>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cctype>

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

} // namespace

VM::VM() {
    resetStack();
    heap.setRootsCallback([this](std::vector<Value>& roots) {
        for (const auto& v : stack) roots.push_back(v);
        for (const auto& [k, v] : globals) roots.push_back(v);
        for (const auto& [k, h] : modules) roots.push_back(Value(h));
        for (GcHandle h : openUpvalues) roots.push_back(Value(h));
        for (const auto& h : tryHandlers) {
            if (!h.pendingException.isNull()) roots.push_back(h.pendingException);
        }
    });
    defineNative("print", nativePrint, -1);
    defineNative("clock", nativeClock, 0);
}

void VM::resetStack() {
    stack.clear();
    stack.reserve(65536);
    frames.clear();
    openUpvalues.clear();
    tryHandlers.clear();
}

GcHandle VM::captureUpvalue(Value* local) {
    for (GcHandle handle : openUpvalues) {
        auto* upvalue = heap.getAs<ObjUpvalue>(handle);
        if (upvalue && upvalue->location == local) {
            return handle;
        }
    }

    GcHandle handle = heap.allocate<ObjUpvalue>(local);
    openUpvalues.push_back(handle);
    return handle;
}

void VM::closeUpvalues(Value* last) {
    auto it = openUpvalues.begin();
    while (it != openUpvalues.end()) {
        auto* upvalue = heap.getAs<ObjUpvalue>(*it);
        if (upvalue && upvalue->location >= last) {
            upvalue->close();
            it = openUpvalues.erase(it);
        } else {
            ++it;
        }
    }
}

InterpretResult VM::handleThrow(Value exception) {
    while (!tryHandlers.empty()) {
        TryHandler& handler = tryHandlers.back();

        // 1. 若有 catch 區塊且尚未進入過 catch 或 finally
        if (handler.catchIp != nullptr && !handler.inCatch && !handler.inFinally) {
            handler.inCatch = true;

            // 展開調用棧訊框至 handler.frameIndex
            while (frames.size() - 1 > handler.frameIndex) {
                closeUpvalues(stack.data() + frames.back().slotOffset);
                frames.pop_back();
            }

            // 展開運算元棧至 handler.stackSize
            closeUpvalues(stack.data() + handler.stackSize);
            stack.resize(handler.stackSize);

            // 推入例外值作為 catch 局部變數
            push(exception);

            // 設定 IP 至 catch 區塊起始位置
            frames.back().ip = handler.catchIp;
            return InterpretResult::OK;
        }

        // 2. 若無 catch 區塊（或已在 catch 中），但有 finally 且尚未進入 finally
        if (handler.finallyIp != nullptr && !handler.inFinally) {
            handler.inFinally = true;
            handler.pendingException = exception;

            // 展開調用棧訊框至 handler.frameIndex
            while (frames.size() - 1 > handler.frameIndex) {
                closeUpvalues(stack.data() + frames.back().slotOffset);
                frames.pop_back();
            }

            // 展開運算元棧至 handler.stackSize
            closeUpvalues(stack.data() + handler.stackSize);
            stack.resize(handler.stackSize);

            // 設定 IP 至 finally 區塊起始位置
            frames.back().ip = handler.finallyIp;
            return InterpretResult::OK;
        }

        // 該 handler 無法處理，彈出並繼續搜尋外層
        tryHandlers.pop_back();
    }

    // 無任何捕捉處理器：未捕獲的例外
    runtimeError("Uncaught exception: %s", valueToString(exception).c_str());
    return InterpretResult::RUNTIME_ERROR;
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

VM::~VM() {
    resetStack();
    modules.clear();
    globals.clear();
    heap.freeAll();

    for (void* lib : loadedLibraries) {
        if (lib) easyscript::freeDynamicLibrary(lib);
    }
    loadedLibraries.clear();
}

void VM::defineModuleNative(ObjModule* mod, const std::string& name, NativeFn function, int arity) {
    if (!mod) return;
    GcHandle handle = heap.allocate<ObjNativeFn>(function, name, arity);
    mod->exports[name] = Value(handle);
}

void VM::defineModuleValue(ObjModule* mod, const std::string& name, Value value) {
    if (!mod) return;
    mod->exports[name] = value;
}

std::optional<Value> VM::getGlobal(const std::string& name) const {
    auto it = globals.find(name);
    if (it != globals.end()) return it->second;
    return std::nullopt;
}

Value VM::jitGetGlobal(const std::string& name) {
    auto it = globals.find(name);
    if (it != globals.end()) return it->second;
    runtimeError("Undefined variable '%s'.", name.c_str());
    return Value();
}

Value VM::jitGetGlobal(const Value& nameVal) {
    if (nameVal.isObject()) {
        auto* str = heap.getAs<ObjString>(*nameVal.asObject());
        if (str) return jitGetGlobal(str->text);
    }
    return Value();
}

void VM::jitSetGlobal(const std::string& name, Value val) {
    auto it = globals.find(name);
    if (it == globals.end()) {
        runtimeError("Undefined variable '%s'.", name.c_str());
        return;
    }
    it->second = val;
}

void VM::jitSetGlobal(const Value& nameVal, Value val) {
    if (nameVal.isObject()) {
        auto* str = heap.getAs<ObjString>(*nameVal.asObject());
        if (str) jitSetGlobal(str->text, val);
    }
}

void VM::jitDefineGlobal(const std::string& name, Value val) {
    globals[name] = val;
}

void VM::jitDefineGlobal(const Value& nameVal, Value val) {
    if (nameVal.isObject()) {
        auto* str = heap.getAs<ObjString>(*nameVal.asObject());
        if (str) jitDefineGlobal(str->text, val);
    }
}

void VM::jitRuntimeError(const std::string& msg) {
    runtimeError("%s", msg.c_str());
}

bool VM::callFromJit(Value* calleeSlot, uint8_t argCount) {
    Value callee = calleeSlot[0];
    if (callee.isObject()) {
        GcObject* obj = heap.get(*callee.asObject());
        if (obj) {
            // 原生 C++ 函式
            if (auto* native = obj->as<ObjNativeFn>()) {
                if (native->arity != -1 && native->arity != argCount) {
                    runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
                    return false;
                }
                Value res = native->function(argCount, calleeSlot + 1);
                calleeSlot[0] = res;
                return true;
            }

            // 腳本函式或閉包
            ObjFunction* fn = nullptr;
            if (auto* f = obj->as<ObjFunction>()) fn = f;
            else if (auto* c = obj->as<ObjClosure>()) fn = heap.getAs<ObjFunction>(c->function);

            if (fn) {
                if (argCount != fn->arity) {
                    runtimeError("Expected %d arguments but got %d.", fn->arity, argCount);
                    return false;
                }
                if (!fn->jitFailed && fn->nativeCode == nullptr && fn->upvalueCount == 0) {
                    if (++fn->invocationCount >= 20) {
                        JitCompiler::compile(fn);
                    }
                }
                if (fn->nativeCode != nullptr && !fn->jitFailed) {
                    JitNativeFn nativeFn = reinterpret_cast<JitNativeFn>(fn->nativeCode);
                    Value res;
                    JitStatus status = nativeFn(this, calleeSlot, &res);
                    if (status == JitStatus::OK) {
                        calleeSlot[0] = res;
                        return true;
                    }
                    fn->jitFailed = true;
                }
            }
        }
    }

    // 落回直譯器
    size_t savedStackSize = stack.size();
    size_t savedFrameSize = frames.size();

    for (uint8_t i = 0; i <= argCount; ++i) {
        push(calleeSlot[i]);
    }

    if (!call(callee, argCount)) {
        stack.resize(savedStackSize);
        return false;
    }

    // 若 JIT 函式直接執行完成且未入棧幀
    if (frames.size() == savedFrameSize) {
        Value res = pop();
        calleeSlot[0] = res;
        stack.resize(savedStackSize);
        return true;
    }

    InterpretResult result = run(savedFrameSize);
    if (result != InterpretResult::OK) {
        stack.resize(savedStackSize);
        return false;
    }

    Value retVal = pop();
    calleeSlot[0] = retVal;
    stack.resize(savedStackSize);
    return true;
}

bool VM::invokeFromJit(Value* receiverSlot, const std::string& name, uint8_t argCount) {
    Value receiver = receiverSlot[0];
    if (!receiver.isObject()) {
        runtimeError("Only instances, modules, and collections have methods.");
        return false;
    }
    GcObject* gcObj = heap.get(*receiver.asObject());
    if (!gcObj) {
        runtimeError("Calling method on invalid object.");
        return false;
    }

    size_t savedStack = stack.size();
    for (uint8_t i = 0; i <= argCount; ++i) {
        push(receiverSlot[i]);
    }

    bool success = false;
    if (auto* instance = gcObj->as<ObjInstance>()) {
        auto* klass = heap.getAs<ObjClass>(instance->klass);
        if (klass) {
            auto fIt = klass->fieldSlots.find(name);
            if (fIt != klass->fieldSlots.end() && fIt->second < instance->fields.size()) {
                Value fieldVal = instance->fields[fIt->second];
                if (!fieldVal.isNull()) {
                    stack[stack.size() - argCount - 1] = fieldVal;
                    size_t frameCountBefore = frames.size();
                    success = call(fieldVal, argCount);
                    if (success && frames.size() > frameCountBefore) {
                        InterpretResult r = run(frameCountBefore);
                        success = (r == InterpretResult::OK);
                    }
                    goto done_invoke;
                }
            }

            auto mIt = klass->methodSlots.find(name);
            if (mIt != klass->methodSlots.end()) {
                Value method = klass->vtable[mIt->second];
                size_t frameCountBefore = frames.size();
                success = call(method, argCount);
                if (success && frames.size() > frameCountBefore) {
                    InterpretResult r = run(frameCountBefore);
                    success = (r == InterpretResult::OK);
                }
                goto done_invoke;
            }
        }
        runtimeError("Undefined method '%s'.", name.c_str());
        return false;
    } else if (auto* arr = gcObj->as<ObjArray>()) {
        success = invokeArrayMethod(arr, name, argCount, receiver);
    } else if (auto* str = gcObj->as<ObjString>()) {
        success = invokeStringMethod(str, name, argCount, receiver);
    } else if (auto* map = gcObj->as<ObjMap>()) {
        success = invokeMapMethod(map, name, argCount, receiver);
    } else if (auto* rng = gcObj->as<ObjRange>()) {
        success = invokeRangeMethod(rng, name, argCount, receiver);
    } else if (auto* mod = gcObj->as<ObjModule>()) {
        auto it = mod->exports.find(name);
        if (it != mod->exports.end()) {
            stack[stack.size() - argCount - 1] = it->second;
            size_t frameCountBefore = frames.size();
            success = call(it->second, argCount);
            if (success) {
                if (frames.size() > frameCountBefore) {
                    InterpretResult r = run(frameCountBefore);
                    success = (r == InterpretResult::OK);
                }
            }
        } else {
            runtimeError("Module '%s' does not export '%s'.", mod->path.c_str(), name.c_str());
        }
    } else if (auto* klass = gcObj->as<ObjClass>()) {
        Value fnVal;
        auto sIt = klass->staticFields.find(name);
        if (sIt != klass->staticFields.end()) {
            fnVal = sIt->second;
        } else {
            auto mIt = klass->methodSlots.find(name);
            if (mIt != klass->methodSlots.end()) {
                fnVal = klass->vtable[mIt->second];
            } else {
                auto directIt = klass->methods.find(name);
                if (directIt != klass->methods.end()) {
                    fnVal = directIt->second;
                }
            }
        }
        if (!fnVal.isNull()) {
            stack[stack.size() - argCount - 1] = fnVal;
            size_t frameCountBefore = frames.size();
            success = call(fnVal, argCount);
            if (success) {
                if (frames.size() > frameCountBefore) {
                    InterpretResult r = run(frameCountBefore);
                    success = (r == InterpretResult::OK);
                }
            }
        } else {
            runtimeError("Class '%s' has no static method '%s'.", klass->name.c_str(), name.c_str());
        }
    }

done_invoke:

    if (success) {
        Value res = pop();
        receiverSlot[0] = res;
    }
    stack.resize(savedStack);
    return success;
}

bool VM::superInvokeFromJit(Value* receiverSlot, const std::string& name, uint8_t argCount) {
    Value receiver = receiverSlot[0];
    if (!receiver.isObject()) {
        runtimeError("Super receiver must be an object.");
        return false;
    }
    auto* instance = heap.getAs<ObjInstance>(*receiver.asObject());
    if (!instance) {
        runtimeError("Super receiver must be an instance.");
        return false;
    }
    auto* klass = heap.getAs<ObjClass>(instance->klass);
    if (!klass || !klass->superclass) {
        runtimeError("Superclass not found.");
        return false;
    }
    auto* superclass = heap.getAs<ObjClass>(*klass->superclass);
    if (!superclass) return false;

    auto it = superclass->methodSlots.find(name);
    if (it == superclass->methodSlots.end()) {
        runtimeError("Undefined superclass method '%s'.", name.c_str());
        return false;
    }

    Value method = superclass->vtable[it->second];

    size_t savedStack = stack.size();
    for (uint8_t i = 0; i <= argCount; ++i) {
        push(receiverSlot[i]);
    }

    size_t frameCountBefore = frames.size();
    bool success = call(method, argCount);
    if (success && frames.size() > frameCountBefore) {
        InterpretResult r = run(frameCountBefore);
        success = (r == InterpretResult::OK);
    }

    if (success) {
        Value res = pop();
        receiverSlot[0] = res;
    }
    stack.resize(savedStack);
    return success;
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
        Value* args = argCount > 0 ? &stack[stack.size() - argCount] : nullptr;
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

    auto tryInvokeJit = [this, argCount](ObjFunction* fn) -> std::optional<bool> {
        if (!fn->jitFailed && fn->nativeCode == nullptr && fn->upvalueCount == 0) {
            if (++fn->invocationCount >= 20) {
                JitCompiler::compile(fn);
            }
        }
        if (fn->nativeCode != nullptr && !fn->jitFailed) {
            Value* slots = &stack[stack.size() - argCount - 1];
            Value result;
            JitNativeFn nativeFn = reinterpret_cast<JitNativeFn>(fn->nativeCode);
            JitStatus status = nativeFn(this, slots, &result);
            if (status == JitStatus::OK) {
                stack.resize(stack.size() - argCount - 1);
                push(result);
                return true;
            }
            // 若發生 Bailout 或執行期錯誤，標記失敗並落回直譯器
            fn->jitFailed = true;
        }
        return std::nullopt;
    };

    // 4. 閉包 (Closure)
    if (auto* closure = obj->as<ObjClosure>()) {
        auto* fn = heap.getAs<ObjFunction>(closure->function);
        if (argCount != fn->arity) {
            runtimeError("Expected %d arguments but got %d.", fn->arity, argCount);
            return false;
        }
        if (fn->upvalueCount == 0) {
            auto jitRes = tryInvokeJit(fn);
            if (jitRes.has_value()) return *jitRes;
        }
        if (frames.size() >= 256) {
            runtimeError("Stack overflow.");
            return false;
        }
        frames.push_back(CallFrame{closure, fn, fn->bytecode.data(), stack.size() - argCount - 1});
        return true;
    }

    // 5. 腳本編譯函式 (Function)
    if (auto* fn = obj->as<ObjFunction>()) {
        if (argCount != fn->arity) {
            runtimeError("Expected %d arguments but got %d.", fn->arity, argCount);
            return false;
        }
        auto jitRes = tryInvokeJit(fn);
        if (jitRes.has_value()) return *jitRes;

        if (frames.size() >= 256) {
            runtimeError("Stack overflow.");
            return false;
        }
        frames.push_back(CallFrame{nullptr, fn, fn->bytecode.data(), stack.size() - argCount - 1});
        return true;
    }

    runtimeError("Can only call functions and classes.");
    return false;
}

// -----------------------------------------------------------------------------
// 容器內建方法調用實作 (Built-in Collection Methods)
// -----------------------------------------------------------------------------

bool VM::invokeArrayMethod(ObjArray* arr, const std::string& name, uint8_t argCount, Value receiver) {
    Value* args = argCount > 0 ? &stack[stack.size() - argCount] : nullptr;

    auto finish = [this, argCount](Value result) {
        stack.resize(stack.size() - argCount - 1);
        push(result);
        return true;
    };

    if (name == "length" || name == "size") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for '%s'.", name.c_str());
            return false;
        }
        return finish(Value(static_cast<int64_t>(arr->elements.size())));
    }
    if (name == "push") {
        for (int i = 0; i < argCount; ++i) {
            arr->elements.push_back(args[i]);
        }
        return finish(Value(static_cast<int64_t>(arr->elements.size())));
    }
    if (name == "pop") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for 'pop'.");
            return false;
        }
        if (arr->elements.empty()) return finish(Value());
        Value back = arr->elements.back();
        arr->elements.pop_back();
        return finish(back);
    }
    if (name == "insert") {
        if (argCount != 2 || !args[0].isInt()) {
            runtimeError("insert(index: int, value) requires 2 arguments.");
            return false;
        }
        int64_t idx = *args[0].asInt();
        if (idx < 0) idx = 0;
        if (idx > static_cast<int64_t>(arr->elements.size())) idx = arr->elements.size();
        arr->elements.insert(arr->elements.begin() + idx, args[1]);
        return finish(Value());
    }
    if (name == "removeAt") {
        if (argCount != 1 || !args[0].isInt()) {
            runtimeError("removeAt(index: int) requires 1 integer argument.");
            return false;
        }
        int64_t idx = *args[0].asInt();
        if (idx < 0 || static_cast<size_t>(idx) >= arr->elements.size()) {
            runtimeError("Array index out of bounds: %lld.", idx);
            return false;
        }
        Value removed = arr->elements[idx];
        arr->elements.erase(arr->elements.begin() + idx);
        return finish(removed);
    }
    if (name == "clear") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for 'clear'.");
            return false;
        }
        arr->elements.clear();
        return finish(Value());
    }
    if (name == "contains") {
        if (argCount != 1) {
            runtimeError("Expected 1 argument for 'contains'.");
            return false;
        }
        bool found = false;
        for (const auto& elem : arr->elements) {
            if (elem == args[0]) { found = true; break; }
        }
        return finish(Value(found));
    }
    if (name == "indexOf") {
        if (argCount != 1) {
            runtimeError("Expected 1 argument for 'indexOf'.");
            return false;
        }
        int64_t foundIdx = -1;
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (arr->elements[i] == args[0]) {
                foundIdx = static_cast<int64_t>(i);
                break;
            }
        }
        return finish(Value(foundIdx));
    }
    if (name == "join") {
        std::string delim = ",";
        if (argCount == 1) {
            delim = valueToString(args[0]);
        } else if (argCount > 1) {
            runtimeError("join() expects 0 or 1 argument.");
            return false;
        }
        std::string result;
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (i > 0) result += delim;
            result += valueToString(arr->elements[i]);
        }
        GcHandle strH = heap.allocateString(std::move(result));
        return finish(Value(strH));
    }
    if (name == "slice") {
        if (argCount < 1 || argCount > 2 || !args[0].isInt()) {
            runtimeError("slice(start: int, [end: int]) expects 1 or 2 integer arguments.");
            return false;
        }
        int64_t len = static_cast<int64_t>(arr->elements.size());
        int64_t start = *args[0].asInt();
        if (start < 0) start += len;
        if (start < 0) start = 0;
        if (start > len) start = len;

        int64_t end = len;
        if (argCount == 2) {
            if (!args[1].isInt()) {
                runtimeError("slice end index must be an integer.");
                return false;
            }
            end = *args[1].asInt();
            if (end < 0) end += len;
            if (end < 0) end = 0;
            if (end > len) end = len;
        }

        std::vector<Value> sub;
        if (start < end) {
            sub.assign(arr->elements.begin() + start, arr->elements.begin() + end);
        }
        GcHandle newArr = heap.allocateArray(std::move(sub));
        return finish(Value(newArr));
    }
    if (name == "reverse") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for 'reverse'.");
            return false;
        }
        std::reverse(arr->elements.begin(), arr->elements.end());
        return finish(receiver);
    }

    runtimeError("Array has no method '%s'.", name.c_str());
    return false;
}

bool VM::invokeStringMethod(ObjString* str, const std::string& name, uint8_t argCount, Value receiver) {
    Value* args = argCount > 0 ? &stack[stack.size() - argCount] : nullptr;

    auto finish = [this, argCount](Value result) {
        stack.resize(stack.size() - argCount - 1);
        push(result);
        return true;
    };

    if (name == "length" || name == "size") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for '%s'.", name.c_str());
            return false;
        }
        return finish(Value(static_cast<int64_t>(str->text.size())));
    }
    if (name == "substring" || name == "slice") {
        if (argCount < 1 || argCount > 2 || !args[0].isInt()) {
            runtimeError("%s(start: int, [end: int]) expects 1 or 2 integer arguments.", name.c_str());
            return false;
        }
        int64_t len = static_cast<int64_t>(str->text.size());
        int64_t start = *args[0].asInt();
        if (start < 0) start += len;
        if (start < 0) start = 0;
        if (start > len) start = len;

        int64_t end = len;
        if (argCount == 2) {
            if (!args[1].isInt()) {
                runtimeError("End index must be an integer.");
                return false;
            }
            end = *args[1].asInt();
            if (end < 0) end += len;
            if (end < 0) end = 0;
            if (end > len) end = len;
        }

        std::string sub = (start < end) ? str->text.substr(start, end - start) : "";
        GcHandle newStr = heap.allocateString(std::move(sub));
        return finish(Value(newStr));
    }
    if (name == "indexOf") {
        if (argCount < 1 || argCount > 2) {
            runtimeError("indexOf(target: string, [start: int]) expects 1 or 2 arguments.");
            return false;
        }
        std::string target = valueToString(args[0]);
        size_t startPos = 0;
        if (argCount == 2) {
            if (!args[1].isInt()) {
                runtimeError("Start position must be an integer.");
                return false;
            }
            startPos = std::max(int64_t(0), *args[1].asInt());
        }
        size_t pos = str->text.find(target, startPos);
        int64_t res = (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);
        return finish(Value(res));
    }
    if (name == "contains") {
        if (argCount != 1) {
            runtimeError("contains(target: string) expects 1 argument.");
            return false;
        }
        std::string target = valueToString(args[0]);
        bool found = (str->text.find(target) != std::string::npos);
        return finish(Value(found));
    }
    if (name == "startsWith") {
        if (argCount != 1) {
            runtimeError("startsWith(prefix: string) expects 1 argument.");
            return false;
        }
        std::string prefix = valueToString(args[0]);
        bool sw = str->text.starts_with(prefix);
        return finish(Value(sw));
    }
    if (name == "endsWith") {
        if (argCount != 1) {
            runtimeError("endsWith(suffix: string) expects 1 argument.");
            return false;
        }
        std::string suffix = valueToString(args[0]);
        bool ew = str->text.ends_with(suffix);
        return finish(Value(ew));
    }
    if (name == "toUpper") {
        if (argCount != 0) {
            runtimeError("toUpper() expects 0 arguments.");
            return false;
        }
        std::string res = str->text;
        for (char& c : res) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        GcHandle newStr = heap.allocateString(std::move(res));
        return finish(Value(newStr));
    }
    if (name == "toLower") {
        if (argCount != 0) {
            runtimeError("toLower() expects 0 arguments.");
            return false;
        }
        std::string res = str->text;
        for (char& c : res) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        GcHandle newStr = heap.allocateString(std::move(res));
        return finish(Value(newStr));
    }
    if (name == "trim") {
        if (argCount != 0) {
            runtimeError("trim() expects 0 arguments.");
            return false;
        }
        const std::string& s = str->text;
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return finish(Value(heap.allocateString("")));
        }
        size_t last = s.find_last_not_of(" \t\r\n");
        return finish(Value(heap.allocateString(s.substr(first, last - first + 1))));
    }
    if (name == "split") {
        if (argCount > 1) {
            runtimeError("split([delimiter: string]) expects 0 or 1 argument.");
            return false;
        }
        std::string delim = (argCount == 1) ? valueToString(args[0]) : " ";
        std::vector<Value> parts;
        if (delim.empty()) {
            for (char c : str->text) {
                parts.push_back(Value(heap.allocateString(std::string(1, c))));
            }
        } else {
            size_t start = 0;
            size_t pos = 0;
            while ((pos = str->text.find(delim, start)) != std::string::npos) {
                std::string token = str->text.substr(start, pos - start);
                parts.push_back(Value(heap.allocateString(std::move(token))));
                start = pos + delim.length();
            }
            parts.push_back(Value(heap.allocateString(str->text.substr(start))));
        }
        GcHandle arrH = heap.allocateArray(std::move(parts));
        return finish(Value(arrH));
    }
    if (name == "replace") {
        if (argCount != 2) {
            runtimeError("replace(from: string, to: string) expects 2 arguments.");
            return false;
        }
        std::string from = valueToString(args[0]);
        std::string to = valueToString(args[1]);
        if (from.empty()) {
            return finish(receiver);
        }
        std::string res = str->text;
        size_t startPos = 0;
        while ((startPos = res.find(from, startPos)) != std::string::npos) {
            res.replace(startPos, from.length(), to);
            startPos += to.length();
        }
        GcHandle newStr = heap.allocateString(std::move(res));
        return finish(Value(newStr));
    }

    runtimeError("String has no method '%s'.", name.c_str());
    return false;
}

bool VM::invokeMapMethod(ObjMap* map, const std::string& name, uint8_t argCount, Value /*receiver*/) {
    Value* args = argCount > 0 ? &stack[stack.size() - argCount] : nullptr;

    auto finish = [this, argCount](Value result) {
        stack.resize(stack.size() - argCount - 1);
        push(result);
        return true;
    };

    if (name == "length" || name == "size") {
        if (argCount != 0) {
            runtimeError("Expected 0 arguments for '%s'.", name.c_str());
            return false;
        }
        return finish(Value(static_cast<int64_t>(map->table.size())));
    }
    if (name == "has" || name == "contains") {
        if (argCount != 1) {
            runtimeError("has(key) expects 1 argument.");
            return false;
        }
        return finish(Value(map->table.find(args[0]) != map->table.end()));
    }
    if (name == "remove") {
        if (argCount != 1) {
            runtimeError("remove(key) expects 1 argument.");
            return false;
        }
        auto it = map->table.find(args[0]);
        if (it != map->table.end()) {
            Value val = it->second;
            map->table.erase(it);
            return finish(val);
        }
        return finish(Value());
    }
    if (name == "keys") {
        if (argCount != 0) {
            runtimeError("keys() expects 0 arguments.");
            return false;
        }
        std::vector<Value> keys;
        keys.reserve(map->table.size());
        for (const auto& [k, v] : map->table) {
            keys.push_back(k);
        }
        GcHandle arrH = heap.allocateArray(std::move(keys));
        return finish(Value(arrH));
    }
    if (name == "values") {
        if (argCount != 0) {
            runtimeError("values() expects 0 arguments.");
            return false;
        }
        std::vector<Value> vals;
        vals.reserve(map->table.size());
        for (const auto& [k, v] : map->table) {
            vals.push_back(v);
        }
        GcHandle arrH = heap.allocateArray(std::move(vals));
        return finish(Value(arrH));
    }
    if (name == "clear") {
        if (argCount != 0) {
            runtimeError("clear() expects 0 arguments.");
            return false;
        }
        map->table.clear();
        return finish(Value());
    }

    runtimeError("Map has no method '%s'.", name.c_str());
    return false;
}

bool VM::invokeRangeMethod(ObjRange* rng, const std::string& name, uint8_t argCount, Value /*receiver*/) {
    auto finish = [this, argCount](Value result) {
        stack.resize(stack.size() - argCount - 1);
        push(result);
        return true;
    };

    if (name == "start") {
        if (argCount != 0) { runtimeError("start expects 0 arguments."); return false; }
        return finish(Value(rng->start));
    }
    if (name == "end") {
        if (argCount != 0) { runtimeError("end expects 0 arguments."); return false; }
        return finish(Value(rng->end));
    }
    if (name == "step") {
        if (argCount != 0) { runtimeError("step expects 0 arguments."); return false; }
        return finish(Value(rng->step));
    }
    if (name == "length" || name == "size") {
        if (argCount != 0) { runtimeError("length expects 0 arguments."); return false; }
        int64_t len = 0;
        if (rng->step > 0 && rng->end > rng->start) {
            len = (rng->end - rng->start + rng->step - 1) / rng->step;
        } else if (rng->step < 0 && rng->end < rng->start) {
            len = (rng->start - rng->end - rng->step - 1) / (-rng->step);
        }
        return finish(Value(len));
    }
    if (name == "toArray") {
        if (argCount != 0) { runtimeError("toArray() expects 0 arguments."); return false; }
        std::vector<Value> elems;
        if (rng->step > 0) {
            for (int64_t v = rng->start; v < rng->end; v += rng->step) elems.push_back(Value(v));
        } else if (rng->step < 0) {
            for (int64_t v = rng->start; v > rng->end; v += rng->step) elems.push_back(Value(v));
        }
        GcHandle arrH = heap.allocateArray(std::move(elems));
        return finish(Value(arrH));
    }

    runtimeError("Range has no method '%s'.", name.c_str());
    return false;
}

ObjModule* VM::loadNativeModule(const std::string& fullPath, const std::string& modName) {
    void* lib = easyscript::loadDynamicLibrary(fullPath);
    if (!lib) {
        runtimeError("Could not load dynamic library '%s': %s", fullPath.c_str(), easyscript::getDynamicLibraryError().c_str());
        return nullptr;
    }

    using PluginInitFn = bool (*)(VM*, ObjModule*);
    auto initFn = reinterpret_cast<PluginInitFn>(easyscript::getDynamicSymbol(lib, "easyscript_module_init"));

    if (!initFn) {
        // 嘗試尋找具名進入點 easyscript_module_init_<cleanName>
        std::string cleanName = modName;
        size_t lastSlash = cleanName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            cleanName = cleanName.substr(lastSlash + 1);
        }
        if (cleanName.ends_with(".dll") || cleanName.ends_with(".so")) {
            cleanName = cleanName.substr(0, cleanName.size() - 4);
        } else if (cleanName.ends_with(".dylib")) {
            cleanName = cleanName.substr(0, cleanName.size() - 6);
        }
        if (cleanName.starts_with("lib")) {
            cleanName = cleanName.substr(3);
        }
        std::string namedSym = "easyscript_module_init_" + cleanName;
        initFn = reinterpret_cast<PluginInitFn>(easyscript::getDynamicSymbol(lib, namedSym));
    }

    if (!initFn) {
        runtimeError("Dynamic library '%s' does not export 'easyscript_module_init' entry point.", fullPath.c_str());
        easyscript::freeDynamicLibrary(lib);
        return nullptr;
    }

    GcHandle modHandle = heap.allocate<ObjModule>(modName);
    auto* mod = heap.getAs<ObjModule>(modHandle);
    modules[modName] = modHandle;
    modules[fullPath] = modHandle;
    loadedLibraries.push_back(lib);

    bool ok = initFn(this, mod);
    if (!ok) {
        runtimeError("Initialization failed for native module '%s'.", modName.c_str());
        return nullptr;
    }

    return mod;
}

ObjModule* VM::loadModule(const std::string& path) {
    auto it = modules.find(path);
    if (it != modules.end()) {
        return heap.getAs<ObjModule>(it->second);
    }

    std::vector<std::string> searchBases;
    searchBases.push_back(path);

    bool isRelativeOrAbsolute = path.starts_with("./") || path.starts_with("../") || 
                                (path.size() >= 2 && path[1] == ':') || path.starts_with("/");
    if (!isRelativeOrAbsolute) {
        searchBases.push_back("./" + path);
        searchBases.push_back("./lib/" + path);
        searchBases.push_back("./plugins/" + path);

        std::string altPath = path;
        std::replace(altPath.begin(), altPath.end(), '/', '_');
        if (altPath != path) {
            searchBases.push_back(altPath);
            searchBases.push_back("./" + altPath);
            searchBases.push_back("./lib/" + altPath);
            searchBases.push_back("./plugins/" + altPath);
        }

        const char* envPath = std::getenv("EASYSCRIPT_PATH");
        if (envPath && *envPath) {
            std::string envStr(envPath);
            size_t start = 0;
            char delimiter = ';';
#if !defined(_WIN32)
            delimiter = ':';
#endif
            while (start < envStr.size()) {
                size_t end = envStr.find(delimiter, start);
                if (end == std::string::npos) end = envStr.size();
                std::string dir = envStr.substr(start, end - start);
                if (!dir.empty()) {
                    if (dir.back() != '/' && dir.back() != '\\') dir += "/";
                    searchBases.push_back(dir + path);
                    if (altPath != path) {
                        searchBases.push_back(dir + altPath);
                    }
                }
                start = end + 1;
            }
        }
    }

    std::vector<std::string> candidates;
    auto addCandidate = [&](const std::string& p) {
        if (p.empty()) return;
        if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
            candidates.push_back(p);
        }
        if (p.starts_with("./")) {
            std::string stripped = p.substr(2);
            if (std::find(candidates.begin(), candidates.end(), stripped) == candidates.end()) {
                candidates.push_back(stripped);
            }
        }
    };

    for (const auto& base : searchBases) {
        if (base.ends_with(".esc") || base.ends_with(".ecs") || base.ends_with(".easy") || easyscript::isDynamicLibraryPath(base)) {
            addCandidate(base);
        } else {
            addCandidate(base + ".esc");
            addCandidate(base + ".ecs");
            addCandidate(base + ".easy");
#if defined(_WIN32) || defined(__CYGWIN__)
            addCandidate(base + ".dll");
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) {
                addCandidate(base.substr(0, slash + 1) + "lib" + base.substr(slash + 1) + ".dll");
            } else {
                addCandidate("lib" + base + ".dll");
            }
#else
            addCandidate(base + ".so");
            addCandidate(base + ".dylib");
            size_t slash = base.find_last_of("/\\");
            if (slash != std::string::npos) {
                addCandidate(base.substr(0, slash + 1) + "lib" + base.substr(slash + 1) + ".so");
            } else {
                addCandidate("lib" + base + ".so");
            }
#endif
        }
    }

    std::string foundPath;
    for (const auto& candidate : candidates) {
        std::ifstream test(candidate, std::ios::binary);
        if (test.is_open()) {
            foundPath = candidate;
            break;
        }
    }

    if (foundPath.empty()) {
        return nullptr;
    }

    // 檢查快取中是否已有相同實體路徑的模組
    auto foundIt = modules.find(foundPath);
    if (foundIt != modules.end()) {
        modules[path] = foundIt->second;
        return heap.getAs<ObjModule>(foundIt->second);
    }

    // 若找到的是原生動態鏈結庫 (.dll / .so / .dylib)
    if (easyscript::isDynamicLibraryPath(foundPath)) {
        return loadNativeModule(foundPath, path);
    }

    ObjFunction* fn = nullptr;
    if (BytecodeDeserializer::isBytecodeFile(foundPath)) {
        fn = BytecodeDeserializer::deserializeFromFile(heap, foundPath);
        if (fn == nullptr) {
            runtimeError("Could not deserialize bytecode module '%s'.", foundPath.c_str());
            return nullptr;
        }
    } else {
        std::ifstream file(foundPath);
        if (!file.is_open()) {
            return nullptr;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        Compiler compiler(heap, source);
        fn = compiler.compile();
        if (fn == nullptr) {
            return nullptr;
        }
    }

    GcHandle modHandle = heap.allocate<ObjModule>(path);
    modules[path] = modHandle;
    modules[foundPath] = modHandle;
    auto* mod = heap.getAs<ObjModule>(modHandle);

    ObjModule* previousModule = currentModule;
    currentModule = mod;

    if (fn != nullptr) {
        size_t currentDepth = frames.size();
        push(Value(modHandle));
        frames.push_back(CallFrame{nullptr, fn, fn->bytecode.data(), stack.size() - 1});
        run(currentDepth);
        if (stack.size() > currentDepth) {
            pop();
        }
    }

    currentModule = previousModule;
    return mod;
}

InterpretResult VM::execute(ObjFunction* fn, const std::string& modulePath) {
    if (fn == nullptr) {
        return InterpretResult::COMPILE_ERROR;
    }

    currentRunningVM = this;

    GcHandle modHandle = heap.allocate<ObjModule>(modulePath);
    modules[modulePath] = modHandle;
    currentModule = heap.getAs<ObjModule>(modHandle);

    push(Value(heap.allocateString("<script>"))); // 槽位 0
    frames.push_back(CallFrame{nullptr, fn, fn->bytecode.data(), 0});

    InterpretResult res = run();
    if (res != InterpretResult::OK) {
        return res;
    }

    // 若宣告了 fn main()，自動調用主進入點
    auto mainIt = globals.find("main");
    if (mainIt != globals.end() && mainIt->second.isObject()) {
        GcObject* obj = heap.get(*mainIt->second.asObject());
        if (obj && (obj->is<ObjFunction>() || obj->is<ObjClosure>())) {
            push(mainIt->second);
            size_t frameCountBefore = frames.size();
            if (call(mainIt->second, 0)) {
                if (frames.size() > frameCountBefore) {
                    return run();
                }
                return InterpretResult::OK;
            }
            return InterpretResult::RUNTIME_ERROR;
        }
    }

    return InterpretResult::OK;
}

InterpretResult VM::interpret(std::string_view source, const std::string& modulePath) {
    Compiler compiler(heap, source);
    ObjFunction* fn = compiler.compile();
    if (fn == nullptr) {
        return InterpretResult::COMPILE_ERROR;
    }

    return execute(fn, modulePath);
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
                if (++frame->function->backedgeCount >= 50 && !frame->function->jitFailed && frame->function->nativeCode == nullptr && frame->function->upvalueCount == 0) {
                    JitCompiler::compile(frame->function);
                }
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

                // 關閉該函式所有尚未關閉的局部變數 Upvalues
                closeUpvalues(stack.data() + slotOffset);

                // 清理當前幀的所有 tryHandlers
                while (!tryHandlers.empty() && tryHandlers.back().frameIndex >= frames.size() - 1) {
                    tryHandlers.pop_back();
                }

                frames.pop_back();

                if (frames.size() == stopDepth) {
                    stack.resize(slotOffset);
                    push(result);
                    return InterpretResult::OK;
                }

                stack.resize(slotOffset);
                push(result);
                frame = &frames.back();
                break;
            }

            case OpCode::OP_CLOSURE: {
                Value fnVal = readConstant();
                GcHandle fnHandle = *fnVal.asObject();
                uint8_t upvalueCount = readByte();

                GcHandle closureHandle = heap.allocate<ObjClosure>(fnHandle);
                push(Value(closureHandle)); // 先推上棧以受 GC 保護
                auto* closure = heap.getAs<ObjClosure>(closureHandle);

                for (uint8_t i = 0; i < upvalueCount; ++i) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();
                    if (isLocal) {
                        GcHandle upvalue = captureUpvalue(&stack[frame->slotOffset + index]);
                        closure->upvalues.push_back(upvalue);
                    } else {
                        closure->upvalues.push_back(frame->closure->upvalues[index]);
                    }
                }
                break;
            }

            case OpCode::OP_GET_UPVALUE: {
                uint8_t slot = readByte();
                GcHandle upvalueHandle = frame->closure->upvalues[slot];
                auto* upvalue = heap.getAs<ObjUpvalue>(upvalueHandle);
                push(*upvalue->location);
                break;
            }

            case OpCode::OP_SET_UPVALUE: {
                uint8_t slot = readByte();
                GcHandle upvalueHandle = frame->closure->upvalues[slot];
                auto* upvalue = heap.getAs<ObjUpvalue>(upvalueHandle);
                *upvalue->location = peek(0);
                break;
            }

            case OpCode::OP_CLOSE_UPVALUE: {
                closeUpvalues(stack.data() + (stack.size() - 1));
                pop();
                break;
            }

            case OpCode::OP_THROW: {
                Value exception = pop();
                InterpretResult res = handleThrow(exception);
                if (res != InterpretResult::OK) return res;
                frame = &frames.back();
                break;
            }

            case OpCode::OP_PUSH_TRY: {
                uint16_t catchOffset = readShort();
                const uint8_t* catchIp = (catchOffset != 0) ? (frame->ip + catchOffset) : nullptr;
                uint16_t finallyOffset = readShort();
                const uint8_t* finallyIp = (finallyOffset != 0) ? (frame->ip + finallyOffset) : nullptr;

                TryHandler handler;
                handler.frameIndex = frames.size() - 1;
                handler.stackSize = stack.size();
                handler.catchIp = catchIp;
                handler.finallyIp = finallyIp;
                handler.inCatch = false;
                handler.inFinally = false;
                tryHandlers.push_back(handler);
                break;
            }

            case OpCode::OP_POP_TRY: {
                if (!tryHandlers.empty()) {
                    tryHandlers.pop_back();
                }
                break;
            }

            case OpCode::OP_FINALLY_END: {
                if (!tryHandlers.empty() && tryHandlers.back().inFinally) {
                    TryHandler handler = tryHandlers.back();
                    tryHandlers.pop_back();
                    if (!handler.pendingException.isNull()) {
                        InterpretResult res = handleThrow(handler.pendingException);
                        if (res != InterpretResult::OK) return res;
                        frame = &frames.back();
                    }
                }
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

                // 複製父類的欄位槽位佈局 (P1)
                subclass->fieldSlots = superclass->fieldSlots;

                // 複製父類的虛擬方法表與方法槽位 (P2)
                subclass->vtable = superclass->vtable;
                subclass->methodSlots = superclass->methodSlots;

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

                // 更新 VTable 與 methodSlots (P2)
                auto it = klass->methodSlots.find(strObj->text);
                if (it == klass->methodSlots.end()) {
                    uint32_t slot = static_cast<uint32_t>(klass->vtable.size());
                    klass->methodSlots[strObj->text] = slot;
                    klass->vtable.push_back(methodVal);
                } else {
                    klass->vtable[it->second] = methodVal;
                }

                pop(); // 彈出 method 函式
                break;
            }

            case OpCode::OP_GET_PROPERTY: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());

                Value target = pop();
                if (!target.isObject()) {
                    runtimeError("Only instances, modules, and collections have properties.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                GcObject* gcObj = heap.get(*target.asObject());
                if (!gcObj) {
                    runtimeError("Invalid object handle.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 實例快速路徑 (P1 欄位槽位化 + Inline Cache)
                if (auto* instance = gcObj->as<ObjInstance>()) {
                    auto* klass = heap.getAs<ObjClass>(instance->klass);
                    size_t ipOffset = frame->ip - frame->function->bytecode.data() - 1;
                    if (frame->function->icTable.size() <= ipOffset) {
                        frame->function->icTable.resize(frame->function->bytecode.size());
                    }
                    auto& ic = frame->function->icTable[ipOffset];

                    // 1. Fast Path: IC 命中
                    if (ic.classId == instance->klass.id && ic.classId != 0 && ic.slot != UINT32_MAX) {
                        if (ic.slot < instance->fields.size()) {
                            push(instance->fields[ic.slot]);
                        } else {
                            push(Value());
                        }
                        break;
                    }

                    // 2. Slow Path: 查類別欄位槽位
                    if (klass) {
                        auto fIt = klass->fieldSlots.find(strObj->text);
                        if (fIt != klass->fieldSlots.end()) {
                            uint32_t slot = fIt->second;
                            ic.classId = instance->klass.id;
                            ic.slot = slot;
                            if (slot < instance->fields.size()) {
                                push(instance->fields[slot]);
                            } else {
                                push(Value());
                            }
                            break;
                        }

                        // 3. 查類別方法 -> 生成 ObjBoundMethod
                        auto mIt = klass->methodSlots.find(strObj->text);
                        if (mIt != klass->methodSlots.end()) {
                            Value method = klass->vtable[mIt->second];
                            GcHandle bound = heap.allocate<ObjBoundMethod>(target, method);
                            push(Value(bound));
                            break;
                        }
                    }

                    runtimeError("Undefined property '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 支援容器屬性讀取
                if (auto* arr = gcObj->as<ObjArray>()) {
                    if (strObj->text == "length" || strObj->text == "size") {
                        push(Value(static_cast<int64_t>(arr->elements.size())));
                        break;
                    }
                    runtimeError("Undefined array property '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                if (auto* str = gcObj->as<ObjString>()) {
                    if (strObj->text == "length" || strObj->text == "size") {
                        push(Value(static_cast<int64_t>(str->text.size())));
                        break;
                    }
                    runtimeError("Undefined string property '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                if (auto* map = gcObj->as<ObjMap>()) {
                    if (strObj->text == "length" || strObj->text == "size") {
                        push(Value(static_cast<int64_t>(map->table.size())));
                        break;
                    }
                    runtimeError("Undefined map property '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }
                if (auto* rng = gcObj->as<ObjRange>()) {
                    if (strObj->text == "start") { push(Value(rng->start)); break; }
                    if (strObj->text == "end") { push(Value(rng->end)); break; }
                    if (strObj->text == "step") { push(Value(rng->step)); break; }
                    if (strObj->text == "length" || strObj->text == "size") {
                        int64_t len = 0;
                        if (rng->step > 0 && rng->end > rng->start) {
                            len = (rng->end - rng->start + rng->step - 1) / rng->step;
                        } else if (rng->step < 0 && rng->end < rng->start) {
                            len = (rng->start - rng->end - rng->step - 1) / (-rng->step);
                        }
                        push(Value(len));
                        break;
                    }
                    runtimeError("Undefined range property '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 支援 ObjModule 屬性存取 (e.g. Math.PI)
                if (auto* mod = gcObj->as<ObjModule>()) {
                    auto it = mod->exports.find(strObj->text);
                    if (it != mod->exports.end()) {
                        push(it->second);
                        break;
                    }
                    runtimeError("Module '%s' does not export '%s'.", mod->path.c_str(), strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 支援 ObjClass 靜態欄位與方法存取 (e.g. Color.RED, Terminal.clear)
                if (auto* klass = gcObj->as<ObjClass>()) {
                    auto sIt = klass->staticFields.find(strObj->text);
                    if (sIt != klass->staticFields.end()) {
                        push(sIt->second);
                        break;
                    }
                    auto mIt = klass->methodSlots.find(strObj->text);
                    if (mIt != klass->methodSlots.end()) {
                        push(klass->vtable[mIt->second]);
                        break;
                    }
                    auto directMethodIt = klass->methods.find(strObj->text);
                    if (directMethodIt != klass->methods.end()) {
                        push(directMethodIt->second);
                        break;
                    }
                    runtimeError("Class '%s' has no static property '%s'.", klass->name.c_str(), strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                runtimeError("Only instances, modules, and collections have properties.");
                return InterpretResult::RUNTIME_ERROR;
            }

            case OpCode::OP_SET_PROPERTY: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());

                Value val = pop();
                Value target = pop();

                if (!target.isObject()) {
                    runtimeError("Only instances and classes have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                if (auto* klass = heap.getAs<ObjClass>(*target.asObject())) {
                    klass->staticFields[strObj->text] = val;
                    push(val);
                    break;
                }

                auto* instance = heap.getAs<ObjInstance>(*target.asObject());
                if (!instance) {
                    runtimeError("Only instances and classes have fields.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                auto* klass = heap.getAs<ObjClass>(instance->klass);
                size_t ipOffset = frame->ip - frame->function->bytecode.data() - 1;
                if (frame->function->icTable.size() <= ipOffset) {
                    frame->function->icTable.resize(frame->function->bytecode.size());
                }
                auto& ic = frame->function->icTable[ipOffset];

                uint32_t slot = 0;
                if (ic.classId == instance->klass.id && ic.classId != 0) {
                    slot = ic.slot;
                } else if (klass) {
                    auto it = klass->fieldSlots.find(strObj->text);
                    if (it == klass->fieldSlots.end()) {
                        slot = static_cast<uint32_t>(klass->fieldSlots.size());
                        klass->fieldSlots[strObj->text] = slot;
                    } else {
                        slot = it->second;
                    }
                    ic.classId = instance->klass.id;
                    ic.slot = slot;
                }

                if (slot >= instance->fields.size()) {
                    instance->fields.resize(slot + 1);
                }
                instance->fields[slot] = val;
                push(val);
                break;
            }

            case OpCode::OP_INVOKE: {
                Value nameVal = readConstant();
                auto strObj = heap.getAs<ObjString>(*nameVal.asObject());
                uint8_t argCount = readByte();

                Value receiver = peek(argCount);
                if (!receiver.isObject()) {
                    runtimeError("Only instances, modules, and collections have methods.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                GcObject* gcObj = heap.get(*receiver.asObject());
                if (!gcObj) {
                    runtimeError("Calling method on invalid object.");
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 實例快速調用 (P2 VTable + Monomorphic Inline Cache)
                if (auto* instance = gcObj->as<ObjInstance>()) {
                    auto* klass = heap.getAs<ObjClass>(instance->klass);
                    size_t ipOffset = frame->ip - frame->function->bytecode.data() - 2;
                    if (frame->function->icTable.size() <= ipOffset) {
                        frame->function->icTable.resize(frame->function->bytecode.size());
                    }
                    auto& ic = frame->function->icTable[ipOffset];

                    // 1. Fast Path: 單態內聯快取命中 (Monomorphic IC Hit)
                    if (ic.classId == instance->klass.id && ic.classId != 0 && !ic.value.isNull()) {
                        if (!call(ic.value, argCount)) {
                            return InterpretResult::RUNTIME_ERROR;
                        }
                        frame = &frames.back();
                        break;
                    }

                    // 2. Slow Path: 先查是否有同名欄位覆蓋函式
                    if (klass) {
                        auto fIt = klass->fieldSlots.find(strObj->text);
                        if (fIt != klass->fieldSlots.end() && fIt->second < instance->fields.size()) {
                            Value fieldVal = instance->fields[fIt->second];
                            if (!fieldVal.isNull()) {
                                stack[stack.size() - argCount - 1] = fieldVal;
                                if (!call(fieldVal, argCount)) {
                                    return InterpretResult::RUNTIME_ERROR;
                                }
                                frame = &frames.back();
                                break;
                            }
                        }

                        // 3. VTable 虛擬方法表查詢
                        auto mIt = klass->methodSlots.find(strObj->text);
                        if (mIt != klass->methodSlots.end()) {
                            Value method = klass->vtable[mIt->second];
                            ic.classId = instance->klass.id;
                            ic.slot = mIt->second;
                            ic.value = method;

                            if (!call(method, argCount)) {
                                return InterpretResult::RUNTIME_ERROR;
                            }
                            frame = &frames.back();
                            break;
                        }
                    }

                    runtimeError("Undefined method '%s'.", strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                // 檢查是否為容器內建方法
                if (auto* arr = gcObj->as<ObjArray>()) {
                    if (!invokeArrayMethod(arr, strObj->text, argCount, receiver)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    break;
                }
                if (auto* str = gcObj->as<ObjString>()) {
                    if (!invokeStringMethod(str, strObj->text, argCount, receiver)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    break;
                }
                if (auto* map = gcObj->as<ObjMap>()) {
                    if (!invokeMapMethod(map, strObj->text, argCount, receiver)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    break;
                }
                if (auto* rng = gcObj->as<ObjRange>()) {
                    if (!invokeRangeMethod(rng, strObj->text, argCount, receiver)) {
                        return InterpretResult::RUNTIME_ERROR;
                    }
                    break;
                }

                // 支援模組函式調用 (e.g. Math.sqrt(16))
                if (auto* mod = gcObj->as<ObjModule>()) {
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

                // 支援類別靜態方法調用 (e.g. Terminal.clear())
                if (auto* klass = gcObj->as<ObjClass>()) {
                    Value method;
                    auto sIt = klass->staticFields.find(strObj->text);
                    if (sIt != klass->staticFields.end()) {
                        method = sIt->second;
                    } else {
                        auto mIt = klass->methodSlots.find(strObj->text);
                        if (mIt != klass->methodSlots.end()) {
                            method = klass->vtable[mIt->second];
                        } else {
                            auto dIt = klass->methods.find(strObj->text);
                            if (dIt != klass->methods.end()) {
                                method = dIt->second;
                            }
                        }
                    }
                    if (!method.isNull()) {
                        stack[stack.size() - argCount - 1] = method;
                        if (!call(method, argCount)) {
                            return InterpretResult::RUNTIME_ERROR;
                        }
                        frame = &frames.back();
                        break;
                    }
                    runtimeError("Class '%s' has no static method '%s'.", klass->name.c_str(), strObj->text.c_str());
                    return InterpretResult::RUNTIME_ERROR;
                }

                runtimeError("Only instances, modules, and collections have methods.");
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
                    auto it = superclass->methodSlots.find(strObj->text);
                    if (it != superclass->methodSlots.end()) {
                        Value method = superclass->vtable[it->second];
                        GcHandle bound = heap.allocate<ObjBoundMethod>(receiver, method);
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
                    auto it = superclass->methodSlots.find(strObj->text);
                    if (it != superclass->methodSlots.end()) {
                        Value method = superclass->vtable[it->second];
                        if (!call(method, argCount)) {
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
                if (!mod) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                frame = &frames.back();
                globals[nameStr->text] = Value(modules[pathStr->text]);
                break;
            }

            case OpCode::OP_IMPORT_NAME: {
                Value pathVal = readConstant();
                Value symVal = readConstant();
                auto pathStr = heap.getAs<ObjString>(*pathVal.asObject());
                auto symStr = heap.getAs<ObjString>(*symVal.asObject());

                ObjModule* mod = loadModule(pathStr->text);
                if (!mod) {
                    return InterpretResult::RUNTIME_ERROR;
                }
                frame = &frames.back();
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
