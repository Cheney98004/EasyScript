#pragma once

#include "gc_object.hpp"
#include <string>
#include <unordered_map>

class ObjModule final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Module;
    std::string path;
    std::unordered_map<std::string, Value> exports;
    std::unordered_map<std::string, Value> globals;

    explicit ObjModule(std::string path);

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
