#pragma once

#include "gc_object.hpp"
#include <vector>

class ObjClosure final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Closure;
    GcHandle function;
    std::vector<GcHandle> upvalues;

    explicit ObjClosure(GcHandle fn);

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
