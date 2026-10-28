#pragma once

#include "gc_object.hpp"
#include <string>

using NativeFn = Value(*)(int argCount, Value* args);

class ObjNativeFn final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::NativeFn;
    NativeFn function{nullptr};
    std::string name;
    int arity{-1};

    ObjNativeFn(NativeFn fn, std::string name, int arity = -1);

    void print(std::ostream& os) const override;
};
