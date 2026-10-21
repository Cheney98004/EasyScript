#include "opcode.hpp"

std::string_view opCodeName(OpCode op) noexcept {
    switch (op) {
        case OpCode::OP_CONSTANT:      return "OP_CONSTANT";
        case OpCode::OP_CONSTANT_16:   return "OP_CONSTANT_16";
        case OpCode::OP_NIL:           return "OP_NIL";
        case OpCode::OP_TRUE:          return "OP_TRUE";
        case OpCode::OP_FALSE:         return "OP_FALSE";
        case OpCode::OP_POP:           return "OP_POP";
        case OpCode::OP_GET_LOCAL:     return "OP_GET_LOCAL";
        case OpCode::OP_SET_LOCAL:     return "OP_SET_LOCAL";
        case OpCode::OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
        case OpCode::OP_GET_GLOBAL:    return "OP_GET_GLOBAL";
        case OpCode::OP_SET_GLOBAL:    return "OP_SET_GLOBAL";
        case OpCode::OP_EQUAL:         return "OP_EQUAL";
        case OpCode::OP_GREATER:       return "OP_GREATER";
        case OpCode::OP_LESS:          return "OP_LESS";
        case OpCode::OP_ADD:           return "OP_ADD";
        case OpCode::OP_SUB:           return "OP_SUB";
        case OpCode::OP_MUL:           return "OP_MUL";
        case OpCode::OP_DIV:           return "OP_DIV";
        case OpCode::OP_MOD:           return "OP_MOD";
        case OpCode::OP_NOT:           return "OP_NOT";
        case OpCode::OP_NEGATE:        return "OP_NEGATE";
        case OpCode::OP_JUMP:          return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OpCode::OP_LOOP:          return "OP_LOOP";
        case OpCode::OP_ITER_INIT:     return "OP_ITER_INIT";
        case OpCode::OP_ITER_NEXT:     return "OP_ITER_NEXT";
        case OpCode::OP_BUILD_ARRAY:   return "OP_BUILD_ARRAY";
        case OpCode::OP_BUILD_MAP:     return "OP_BUILD_MAP";
        case OpCode::OP_BUILD_RANGE:   return "OP_BUILD_RANGE";
        case OpCode::OP_GET_INDEX:     return "OP_GET_INDEX";
        case OpCode::OP_SET_INDEX:     return "OP_SET_INDEX";
        case OpCode::OP_CALL:          return "OP_CALL";
        case OpCode::OP_RETURN:        return "OP_RETURN";
        case OpCode::OP_STRING_CONCAT: return "OP_STRING_CONCAT";
        case OpCode::OP_PRINT:         return "OP_PRINT";
        case OpCode::OP_CLASS:         return "OP_CLASS";
        case OpCode::OP_INHERIT:       return "OP_INHERIT";
        case OpCode::OP_METHOD:        return "OP_METHOD";
        case OpCode::OP_GET_PROPERTY:  return "OP_GET_PROPERTY";
        case OpCode::OP_SET_PROPERTY:  return "OP_SET_PROPERTY";
        case OpCode::OP_INVOKE:        return "OP_INVOKE";
        case OpCode::OP_GET_SUPER:     return "OP_GET_SUPER";
        case OpCode::OP_SUPER_INVOKE:  return "OP_SUPER_INVOKE";
        case OpCode::OP_EXPORT:        return "OP_EXPORT";
        case OpCode::OP_IMPORT_MODULE: return "OP_IMPORT_MODULE";
        case OpCode::OP_IMPORT_NAME:   return "OP_IMPORT_NAME";
    }
    return "OP_UNKNOWN";
}
