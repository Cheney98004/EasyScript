#pragma once

#include <string>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace easyscript {

inline void* loadDynamicLibrary(const std::string& path) {
#if defined(_WIN32) || defined(__CYGWIN__)
    return static_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
}

inline void* getDynamicSymbol(void* handle, const std::string& symbol) {
    if (!handle) return nullptr;
#if defined(_WIN32) || defined(__CYGWIN__)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol.c_str()));
#else
    return dlsym(handle, symbol.c_str());
#endif
}

inline void freeDynamicLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32) || defined(__CYGWIN__)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

inline std::string getDynamicLibraryError() {
#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD err = GetLastError();
    if (err == 0) return "No error";
    char buf[512];
    DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               buf, sizeof(buf), nullptr);
    if (len == 0) return "Unknown Windows error code " + std::to_string(err);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }
    return std::string(buf);
#else
    const char* err = dlerror();
    return err ? std::string(err) : "Unknown error";
#endif
}

inline bool isDynamicLibraryPath(const std::string& path) {
    return path.ends_with(".dll") || path.ends_with(".so") || path.ends_with(".dylib");
}

} // namespace easyscript
