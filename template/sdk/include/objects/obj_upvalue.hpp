#pragma once

#include "gc_object.hpp"

class ObjUpvalue final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Upvalue;
    Value* location{nullptr};
    Value closed;

    explicit ObjUpvalue(Value* slot = nullptr);

    bool isClosed() const noexcept { return location == &closed; }
    void close() noexcept;

    void trace(const std::function<void(Value)>& visitor) const override;
    void print(std::ostream& os) const override;
};
