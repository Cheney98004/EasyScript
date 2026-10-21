#include "objects/obj_bound_method.hpp"

ObjBoundMethod::ObjBoundMethod(Value receiver, Value method)
    : GcObject(ObjType::BoundMethod), receiver(receiver), method(method) {}

void ObjBoundMethod::trace(const std::function<void(Value)>& visitor) const {
    visitor(receiver);
    visitor(method);
}

void ObjBoundMethod::print(std::ostream& os) const {
    os << "<bound method>";
}
