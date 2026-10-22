#include "objects/obj_instance.hpp"

ObjInstance::ObjInstance(GcHandle klass)
    : GcObject(ObjType::Instance), klass(klass) {}

void ObjInstance::trace(const std::function<void(Value)>& visitor) const {
    visitor(Value(klass));
    for (const auto& fieldVal : fields) {
        visitor(fieldVal);
    }
}

void ObjInstance::print(std::ostream& os) const {
    os << "<instance of class #" << klass.id << ">";
}
