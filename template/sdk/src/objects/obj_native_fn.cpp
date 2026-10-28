#include "objects/obj_native_fn.hpp"

ObjNativeFn::ObjNativeFn(NativeFn fn, std::string name, int arity)
    : GcObject(ObjType::NativeFn), function(fn), name(std::move(name)), arity(arity) {}

void ObjNativeFn::print(std::ostream& os) const {
    os << "<native fn " << (name.empty() ? "anonymous" : name) << ">";
}
