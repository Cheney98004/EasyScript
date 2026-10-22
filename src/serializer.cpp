#include "serializer.hpp"
#include <fstream>
#include <cstdint>
#include <cstring>

namespace {

constexpr char MAGIC[4] = {'E', 'S', 'C', '\x01'};
constexpr uint32_t ESC_VERSION = 1;

enum ValueTag : uint8_t {
    TAG_NIL = 0x00,
    TAG_BOOL_FALSE = 0x01,
    TAG_BOOL_TRUE = 0x02,
    TAG_INT = 0x03,
    TAG_DOUBLE = 0x04,
    TAG_STRING = 0x05,
    TAG_FUNCTION = 0x06
};

template <typename T>
void writeRaw(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
bool readRaw(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

void writeString(std::ostream& out, const std::string& str) {
    uint32_t length = static_cast<uint32_t>(str.size());
    writeRaw(out, length);
    if (length > 0) {
        out.write(str.data(), length);
    }
}

std::string readString(std::istream& in) {
    uint32_t length = 0;
    if (!readRaw(in, length)) return "";
    std::string str(length, '\0');
    if (length > 0) {
        in.read(str.data(), length);
    }
    return str;
}

} // namespace

bool BytecodeSerializer::serializeToFile(ObjFunction* function, const Heap& heap, const std::string& filepath) {
    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(MAGIC, 4);
    writeRaw(out, ESC_VERSION);

    serialize(function, heap, out);
    return out.good();
}

void BytecodeSerializer::serialize(ObjFunction* function, const Heap& heap, std::ostream& out) {
    writeFunction(function, heap, out);
}

void BytecodeSerializer::writeFunction(ObjFunction* fn, const Heap& heap, std::ostream& out) {
    if (fn == nullptr) {
        writeString(out, "");
        writeRaw<int32_t>(out, 0);
        writeRaw<int32_t>(out, 0);
        writeRaw<uint32_t>(out, 0);
        writeRaw<uint32_t>(out, 0);
        writeRaw<uint32_t>(out, 0);
        return;
    }

    writeString(out, fn->name);
    writeRaw<int32_t>(out, static_cast<int32_t>(fn->arity));
    writeRaw<int32_t>(out, static_cast<int32_t>(fn->upvalueCount));

    // Bytecode
    uint32_t codeSize = static_cast<uint32_t>(fn->bytecode.size());
    writeRaw(out, codeSize);
    if (codeSize > 0) {
        out.write(reinterpret_cast<const char*>(fn->bytecode.data()), codeSize);
    }

    // Default values
    uint32_t defCount = static_cast<uint32_t>(fn->defaultValues.size());
    writeRaw(out, defCount);
    for (const auto& val : fn->defaultValues) {
        writeValue(val, heap, out);
    }

    // Constants
    uint32_t constCount = static_cast<uint32_t>(fn->constants.size());
    writeRaw(out, constCount);
    for (const auto& val : fn->constants) {
        writeValue(val, heap, out);
    }
}

void BytecodeSerializer::writeValue(Value val, const Heap& heap, std::ostream& out) {
    if (val.isNull()) {
        writeRaw<uint8_t>(out, TAG_NIL);
    } else if (val.isBool()) {
        writeRaw<uint8_t>(out, *val.asBool() ? TAG_BOOL_TRUE : TAG_BOOL_FALSE);
    } else if (val.isInt()) {
        writeRaw<uint8_t>(out, TAG_INT);
        writeRaw<int64_t>(out, *val.asInt());
    } else if (val.isDouble()) {
        writeRaw<uint8_t>(out, TAG_DOUBLE);
        writeRaw<double>(out, *val.asDouble());
    } else if (val.isObject()) {
        GcHandle handle = *val.asObject();
        const GcObject* obj = heap.get(handle);
        if (obj == nullptr) {
            writeRaw<uint8_t>(out, TAG_NIL);
        } else if (obj->type == ObjType::String) {
            writeRaw<uint8_t>(out, TAG_STRING);
            const ObjString* str = obj->as<ObjString>();
            writeString(out, str->text);
        } else if (obj->type == ObjType::Function) {
            writeRaw<uint8_t>(out, TAG_FUNCTION);
            const ObjFunction* subFn = obj->as<ObjFunction>();
            writeFunction(const_cast<ObjFunction*>(subFn), heap, out);
        } else {
            writeRaw<uint8_t>(out, TAG_NIL);
        }
    } else {
        writeRaw<uint8_t>(out, TAG_NIL);
    }
}

bool BytecodeDeserializer::isBytecodeFile(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return false;

    char magic[4];
    in.read(magic, 4);
    return in.gcount() == 4 && std::memcmp(magic, MAGIC, 4) == 0;
}

ObjFunction* BytecodeDeserializer::deserializeFromFile(Heap& heap, const std::string& filepath) {
    if (!isBytecodeFile(filepath)) return nullptr;

    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) return nullptr;

    char magic[4];
    in.read(magic, 4);

    uint32_t version = 0;
    if (!readRaw(in, version) || version != ESC_VERSION) {
        return nullptr;
    }

    return deserialize(heap, in);
}

ObjFunction* BytecodeDeserializer::deserialize(Heap& heap, std::istream& in) {
    return readFunction(heap, in);
}

GcHandle BytecodeDeserializer::readFunctionHandle(Heap& heap, std::istream& in) {
    std::string name = readString(in);
    int32_t arity = 0;
    int32_t upvalueCount = 0;
    if (!readRaw(in, arity)) return GcHandle{0};
    if (!readRaw(in, upvalueCount)) return GcHandle{0};

    GcHandle fnHandle = heap.allocate<ObjFunction>(name);
    heap.pushTempRoot(fnHandle);
    ObjFunction* fn = heap.getAs<ObjFunction>(fnHandle);
    fn->arity = arity;
    fn->upvalueCount = upvalueCount;

    // Bytecode
    uint32_t codeSize = 0;
    if (!readRaw(in, codeSize)) {
        heap.popTempRoot();
        return GcHandle{0};
    }
    fn->bytecode.resize(codeSize);
    if (codeSize > 0) {
        in.read(reinterpret_cast<char*>(fn->bytecode.data()), codeSize);
    }

    // Default values
    uint32_t defCount = 0;
    if (!readRaw(in, defCount)) {
        heap.popTempRoot();
        return GcHandle{0};
    }
    fn->defaultValues.reserve(defCount);
    for (uint32_t i = 0; i < defCount; ++i) {
        fn->defaultValues.push_back(readValue(heap, in));
    }

    // Constants
    uint32_t constCount = 0;
    if (!readRaw(in, constCount)) {
        heap.popTempRoot();
        return GcHandle{0};
    }
    fn->constants.reserve(constCount);
    for (uint32_t i = 0; i < constCount; ++i) {
        fn->constants.push_back(readValue(heap, in));
    }

    heap.popTempRoot();
    return fnHandle;
}

ObjFunction* BytecodeDeserializer::readFunction(Heap& heap, std::istream& in) {
    GcHandle h = readFunctionHandle(heap, in);
    return heap.getAs<ObjFunction>(h);
}

Value BytecodeDeserializer::readValue(Heap& heap, std::istream& in) {
    uint8_t tag = 0;
    if (!readRaw(in, tag)) return Value();

    switch (tag) {
        case TAG_NIL:
            return Value();
        case TAG_BOOL_FALSE:
            return Value(false);
        case TAG_BOOL_TRUE:
            return Value(true);
        case TAG_INT: {
            int64_t val = 0;
            readRaw(in, val);
            return Value(val);
        }
        case TAG_DOUBLE: {
            double val = 0.0;
            readRaw(in, val);
            return Value(val);
        }
        case TAG_STRING: {
            std::string text = readString(in);
            GcHandle handle = heap.allocateString(std::move(text));
            return Value(handle);
        }
        case TAG_FUNCTION: {
            GcHandle fnHandle = readFunctionHandle(heap, in);
            return Value(fnHandle);
        }
    }
    return Value();
}
