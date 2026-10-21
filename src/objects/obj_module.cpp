#include "objects/obj_module.hpp"

ObjModule::ObjModule(std::string path)
    : GcObject(ObjType::Module), path(std::move(path)) {}

void ObjModule::trace(const std::function<void(Value)>& visitor) const {
    for (const auto& [_, exp] : exports) {
        visitor(exp);
    }
    for (const auto& [_, glob] : globals) {
        visitor(glob);
    }
}

void ObjModule::print(std::ostream& os) const {
    os << "<module " << path << ">";
}
