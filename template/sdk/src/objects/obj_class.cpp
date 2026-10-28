#include "objects/obj_class.hpp"

ObjClass::ObjClass(std::string name, std::optional<GcHandle> superclass)
    : GcObject(ObjType::Class), name(std::move(name)), superclass(superclass) {}

void ObjClass::trace(const std::function<void(Value)>& visitor) const {
    if (superclass) {
        visitor(Value(*superclass));
    }
    for (const auto& iface : interfaces) {
        visitor(Value(iface));
    }
    for (const auto& [_, method] : methods) {
        visitor(method);
    }
    for (const auto& method : vtable) {
        visitor(method);
    }
    for (const auto& [_, field] : staticFields) {
        visitor(field);
    }
}

void ObjClass::print(std::ostream& os) const {
    os << "<class " << name << ">";
}
