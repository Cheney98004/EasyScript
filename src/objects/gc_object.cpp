#include "objects/gc_object.hpp"

std::string_view objTypeName(ObjType type) noexcept {
    switch (type) {
        case ObjType::String:      return "String";
        case ObjType::Array:       return "Array";
        case ObjType::Map:         return "Map";
        case ObjType::Range:       return "Range";
        case ObjType::NativeFn:    return "NativeFunction";
        case ObjType::Function:    return "Function";
        case ObjType::Closure:     return "Closure";
        case ObjType::Upvalue:     return "Upvalue";
        case ObjType::Class:       return "Class";
        case ObjType::Instance:    return "Instance";
        case ObjType::BoundMethod: return "BoundMethod";
        case ObjType::Module:      return "Module";
    }
    return "Unknown";
}

std::ostream& operator<<(std::ostream& os, const GcObject& obj) {
    obj.print(os);
    return os;
}
