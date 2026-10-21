#pragma once

#include "gc_object.hpp"
#include <string>
#include <unordered_map>

class ObjInstance final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Instance;
    GcHandle klass;
    std::unordered_map<std::string, Value> fields;

    explicit ObjInstance(GcHandle klass);

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
