#include "objects/obj_function.hpp"

ObjFunction::ObjFunction(std::string name)
    : GcObject(ObjType::Function), name(std::move(name)) {}

void ObjFunction::trace(const std::function<void(Value)>& visitor) const {
    for (const auto& val : defaultValues) {
        visitor(val);
    }
    for (const auto& c : constants) {
        visitor(c);
    }
    for (const auto& ic : icTable) {
        if (!ic.value.isNull()) {
            visitor(ic.value);
        }
    }
}

void ObjFunction::print(std::ostream& os) const {
    if (name.empty()) {
        os << "<fn (anonymous)>";
    } else {
        os << "<fn " << name << ">";
    }
}
