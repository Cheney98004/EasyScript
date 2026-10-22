#include "easyscript_plugin.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <random>

static double getNum(const Value& v, double defaultVal = 0.0) {
    if (v.isNumber()) return *v.asNumber();
    return defaultVal;
}

static Value math_sqrt(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::sqrt(getNum(args[0])));
}

static Value math_pow(int argCount, Value* args) {
    if (argCount < 2) return Value(0.0);
    return Value(std::pow(getNum(args[0]), getNum(args[1])));
}

static Value math_abs(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    if (args[0].isInt()) {
        int64_t val = *args[0].asInt();
        return Value(val < 0 ? -val : val);
    }
    return Value(std::abs(getNum(args[0])));
}

static Value math_floor(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::floor(getNum(args[0])));
}

static Value math_ceil(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::ceil(getNum(args[0])));
}

static Value math_round(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::round(getNum(args[0])));
}

static Value math_sin(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::sin(getNum(args[0])));
}

static Value math_cos(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::cos(getNum(args[0])));
}

static Value math_tan(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::tan(getNum(args[0])));
}

static Value math_asin(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::asin(getNum(args[0])));
}

static Value math_acos(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::acos(getNum(args[0])));
}

static Value math_atan(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::atan(getNum(args[0])));
}

static Value math_atan2(int argCount, Value* args) {
    if (argCount < 2) return Value(0.0);
    return Value(std::atan2(getNum(args[0]), getNum(args[1])));
}

static Value math_log(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::log(getNum(args[0])));
}

static Value math_exp(int argCount, Value* args) {
    if (argCount < 1) return Value(0.0);
    return Value(std::exp(getNum(args[0])));
}

static Value math_min(int argCount, Value* args) {
    if (argCount < 2) return Value(0.0);
    double a = getNum(args[0]);
    double b = getNum(args[1]);
    if (args[0].isInt() && args[1].isInt()) {
        return Value(std::min(*args[0].asInt(), *args[1].asInt()));
    }
    return Value(std::min(a, b));
}

static Value math_max(int argCount, Value* args) {
    if (argCount < 2) return Value(0.0);
    double a = getNum(args[0]);
    double b = getNum(args[1]);
    if (args[0].isInt() && args[1].isInt()) {
        return Value(std::max(*args[0].asInt(), *args[1].asInt()));
    }
    return Value(std::max(a, b));
}

static Value math_clamp(int argCount, Value* args) {
    if (argCount < 3) return Value(0.0);
    double v = getNum(args[0]);
    double lo = getNum(args[1]);
    double hi = getNum(args[2]);
    if (args[0].isInt() && args[1].isInt() && args[2].isInt()) {
        return Value(std::clamp(*args[0].asInt(), *args[1].asInt(), *args[2].asInt()));
    }
    return Value(std::clamp(v, lo, hi));
}

static Value math_random(int, Value*) {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return Value(dist(rng));
}

static Value math_randomInt(int argCount, Value* args) {
    if (argCount < 2) return Value(int64_t(0));
    int64_t minVal = args[0].isInt() ? *args[0].asInt() : static_cast<int64_t>(getNum(args[0]));
    int64_t maxVal = args[1].isInt() ? *args[1].asInt() : static_cast<int64_t>(getNum(args[1]));
    if (minVal > maxVal) std::swap(minVal, maxVal);
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    return Value(dist(rng));
}

EASYSCRIPT_MODULE_INIT {
    vm->defineModuleNative(mod, "sqrt", math_sqrt, 1);
    vm->defineModuleNative(mod, "pow", math_pow, 2);
    vm->defineModuleNative(mod, "abs", math_abs, 1);
    vm->defineModuleNative(mod, "floor", math_floor, 1);
    vm->defineModuleNative(mod, "ceil", math_ceil, 1);
    vm->defineModuleNative(mod, "round", math_round, 1);
    vm->defineModuleNative(mod, "sin", math_sin, 1);
    vm->defineModuleNative(mod, "cos", math_cos, 1);
    vm->defineModuleNative(mod, "tan", math_tan, 1);
    vm->defineModuleNative(mod, "asin", math_asin, 1);
    vm->defineModuleNative(mod, "acos", math_acos, 1);
    vm->defineModuleNative(mod, "atan", math_atan, 1);
    vm->defineModuleNative(mod, "atan2", math_atan2, 2);
    vm->defineModuleNative(mod, "log", math_log, 1);
    vm->defineModuleNative(mod, "exp", math_exp, 1);
    vm->defineModuleNative(mod, "min", math_min, 2);
    vm->defineModuleNative(mod, "max", math_max, 2);
    vm->defineModuleNative(mod, "clamp", math_clamp, 3);
    vm->defineModuleNative(mod, "random", math_random, 0);
    vm->defineModuleNative(mod, "randomInt", math_randomInt, 2);

    vm->defineModuleValue(mod, "PI", Value(3.14159265358979323846));
    vm->defineModuleValue(mod, "E", Value(2.71828182845904523536));
    return true;
}
