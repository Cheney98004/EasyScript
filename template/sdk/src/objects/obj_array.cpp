#include "objects/obj_array.hpp"

ObjArray::ObjArray(std::vector<Value> elems)
    : GcObject(ObjType::Array), elements(std::move(elems)) {}

void ObjArray::trace(const std::function<void(Value)>& visitor) const {
    for (const auto& elem : elements) {
        visitor(elem);
    }
}

void ObjArray::print(std::ostream& os) const {
    os << "[";
    for (size_t i = 0; i < elements.size(); ++i) {
        os << elements[i];
        if (i + 1 < elements.size()) {
            os << ", ";
        }
    }
    os << "]";
}
