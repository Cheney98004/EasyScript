#pragma once

#include "gc_object.hpp"
#include <string>
#include <string_view>

class ObjString final : public GcObject {
public:
    static constexpr ObjType TYPE = ObjType::String;
    std::string text;
    uint32_t hash{0};

    explicit ObjString(std::string str);

    size_t length() const noexcept { return text.size(); }
    std::string_view view() const noexcept { return text; }

    void print(std::ostream& os) const override;
};
