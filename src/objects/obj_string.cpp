#include "objects/obj_string.hpp"

namespace {

uint32_t hashString(std::string_view str) noexcept {
    uint32_t hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

ObjString::ObjString(std::string str)
    : GcObject(ObjType::String), text(std::move(str)), hash(hashString(text)) {}

void ObjString::print(std::ostream& os) const {
    os << text;
}
