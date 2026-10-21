#include "objects/obj_upvalue.hpp"

ObjUpvalue::ObjUpvalue(Value* slot)
    : GcObject(ObjType::Upvalue), location(slot) {}

void ObjUpvalue::close() noexcept {
    if (location != &closed && location != nullptr) {
        closed = *location;
        location = &closed;
    }
}

void ObjUpvalue::trace(const std::function<void(Value)>& visitor) const {
    if (isClosed()) {
        visitor(closed);
    }
}

void ObjUpvalue::print(std::ostream& os) const {
    os << "<upvalue " << (isClosed() ? "closed" : "open") << ">";
}
