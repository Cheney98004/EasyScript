#pragma once

#include "gc_object.hpp"

class ObjBoundMethod final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::BoundMethod;
    Value receiver;
    Value method;

    ObjBoundMethod(Value receiver, Value method);

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
