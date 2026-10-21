#pragma once

#include "gc_object.hpp"
#include <string>
#include <vector>
#include <cstdint>

class ObjFunction final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Function;
    std::string name;
    int arity{0};
    std::vector<Value> defaultValues;
    std::vector<uint8_t> bytecode;
    std::vector<Value> constants;
    int upvalueCount{0};

    explicit ObjFunction(std::string name = "");

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
