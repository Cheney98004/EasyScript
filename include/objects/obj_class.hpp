#pragma once

#include "gc_object.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

class ObjClass final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Class;
    std::string name;
    std::optional<GcHandle> superclass;
    std::vector<GcHandle> interfaces;
    std::unordered_map<std::string, Value> methods;
    std::unordered_map<std::string, Value> staticFields;

    explicit ObjClass(std::string name, std::optional<GcHandle> superclass = std::nullopt);

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
