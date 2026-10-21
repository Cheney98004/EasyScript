#include "objects/obj_map.hpp"

ObjMap::ObjMap(std::unordered_map<Value, Value> entries)
    : GcObject(ObjType::Map), table(std::move(entries)) {}

void ObjMap::trace(const std::function<void(Value)>& visitor) const {
    for (const auto& [k, v] : table) {
        visitor(k);
        visitor(v);
    }
}

void ObjMap::print(std::ostream& os) const {
    os << "{";
    bool first = true;
    for (const auto& [k, v] : table) {
        if (!first) os << ", ";
        first = false;
        os << k << ": " << v;
    }
    os << "}";
}
