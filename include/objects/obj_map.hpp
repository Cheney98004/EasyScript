#pragma once

#include "gc_object.hpp"
#include <unordered_map>

class ObjMap final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Map;
    std::unordered_map<Value, Value> table;

    explicit ObjMap(std::unordered_map<Value, Value> entries = {});

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
