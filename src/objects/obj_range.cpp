#include "objects/obj_range.hpp"
#include <cmath>

ObjRange::ObjRange(int64_t start, int64_t end, int64_t step)
    : GcObject(ObjType::Range), start(start), end(end), step(step == 0 ? 1 : step) {}

int64_t ObjRange::count() const noexcept {
    if (step > 0 && start >= end) return 0;
    if (step < 0 && start <= end) return 0;
    return (std::abs(end - start) + std::abs(step) - 1) / std::abs(step);
}

bool ObjRange::contains(int64_t value) const noexcept {
    if (step > 0) {
        if (value < start || value >= end) return false;
        return (value - start) % step == 0;
    } else {
        if (value > start || value <= end) return false;
        return (start - value) % (-step) == 0;
    }
}

void ObjRange::print(std::ostream& os) const {
    os << start << ".." << end;
    if (step != 1) {
        os << " step " << step;
    }
}
