#pragma once

#include "gc_object.hpp"
#include <cstdint>

class ObjRange final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::Range;
    int64_t start{0};
    int64_t end{0};
    int64_t step{1};

    ObjRange(int64_t start, int64_t end, int64_t step = 1);

    int64_t count() const noexcept;
    bool contains(int64_t value) const noexcept;
    void print(std::ostream& os) const override;
};
