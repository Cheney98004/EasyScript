#pragma once

#include "object.hpp"
#include "heap.hpp"
#include <iostream>
#include <string>
#include <vector>

class BytecodeSerializer {
public:
    static bool serializeToFile(ObjFunction* function, const Heap& heap, const std::string& filepath);
    static void serialize(ObjFunction* function, const Heap& heap, std::ostream& out);

private:
    static void writeFunction(ObjFunction* fn, const Heap& heap, std::ostream& out);
    static void writeValue(Value val, const Heap& heap, std::ostream& out);
};

class BytecodeDeserializer {
public:
    static bool isBytecodeFile(const std::string& filepath);
    static ObjFunction* deserializeFromFile(Heap& heap, const std::string& filepath);
    static ObjFunction* deserialize(Heap& heap, std::istream& in);

private:
    static GcHandle readFunctionHandle(Heap& heap, std::istream& in);
    static ObjFunction* readFunction(Heap& heap, std::istream& in);
    static Value readValue(Heap& heap, std::istream& in);
};
