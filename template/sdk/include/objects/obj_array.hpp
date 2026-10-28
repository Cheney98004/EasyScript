#pragma once

#include "gc_object.hpp"
#include <vector>

class ObjArray final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Array;
    std::vector<Value> elements;

    explicit ObjArray(std::vector<Value> elems = {});

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
