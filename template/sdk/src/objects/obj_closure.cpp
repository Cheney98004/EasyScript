#include "objects/obj_closure.hpp"

ObjClosure::ObjClosure(GcHandle fn)
    : GcObject(ObjType::Closure), function(fn) {}

void ObjClosure::trace(const std::function<void(Value)>& visitor) const {
    visitor(Value(function));
    for (const auto& u : upvalues) {
        visitor(Value(u));
    }
}

void ObjClosure::print(std::ostream& os) const {
    os << "<closure #" << function.id << ">";
}
