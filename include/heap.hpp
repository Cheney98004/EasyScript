#pragma once

#include "value.hpp"
#include "object.hpp"
#include <vector>
#include <unordered_map>
#include <string_view>
#include <string>
#include <iostream>
#include <functional>
#include <utility>

// -----------------------------------------------------------------------------
// GC 堆積物件槽 (Slot)
// -----------------------------------------------------------------------------
struct HeapSlot {
    GcObject* object{nullptr};
    bool inUse{false};
};

// -----------------------------------------------------------------------------
// EasyScript 垃圾回收堆積 (Heap)
// -----------------------------------------------------------------------------
class Heap {
public:
    explicit Heap(size_t initialThreshold = 1024 * 1024); // 預設 1MB 門檻
    ~Heap();

    // 禁止複製
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    // -------------------------------------------------------------------------
    // 物件分配介面
    // -------------------------------------------------------------------------

    // 通用物件分配樣板
    template <typename T, typename... Args>
    GcHandle allocate(Args&&... args) {
        checkGc();
        T* obj = new T(std::forward<Args>(args)...);
        GcHandle handle = insertObject(obj);
        trackAllocation(sizeof(T));
        return handle;
    }

    // 字串專屬分配（支援 String Interning 字串池化）
    GcHandle allocateString(std::string text);
    GcHandle internString(std::string_view text);

    // 陣列專屬分配
    GcHandle allocateArray(std::vector<Value> elements = {});

    // 字典專屬分配
    GcHandle allocateMap(std::unordered_map<Value, Value> table = {});

    // -------------------------------------------------------------------------
    // 物件存取介面
    // -------------------------------------------------------------------------
    [[nodiscard]] GcObject* get(GcHandle handle) const noexcept;

    template <typename T>
    [[nodiscard]] T* getAs(GcHandle handle) const noexcept {
        GcObject* obj = get(handle);
        return (obj != nullptr) ? obj->as<T>() : nullptr;
    }

    [[nodiscard]] bool isValid(GcHandle handle) const noexcept;

    // -------------------------------------------------------------------------
    // 根物件保護（暫存 Root，防止分配中途觸發 GC 時被回收）
    // -------------------------------------------------------------------------
    void pushTempRoot(GcHandle handle);
    void popTempRoot();
    void pushTempRoot(Value value);
    void popTempRoot(size_t count);

    // -------------------------------------------------------------------------
    // 垃圾回收核心 (Mark-Sweep)
    // -------------------------------------------------------------------------
    // 外部可傳入當前 VM 運算元棧或全域環境作為根集合
    size_t collectGarbage(const std::vector<Value>& roots = {});

    // -------------------------------------------------------------------------
    // 狀態與除錯
    // -------------------------------------------------------------------------
    [[nodiscard]] size_t getBytesAllocated() const noexcept { return bytesAllocated; }
    [[nodiscard]] size_t getObjectCount() const noexcept { return liveObjectCount; }
    [[nodiscard]] size_t getNextGcThreshold() const noexcept { return nextGcThreshold; }

    void dump(std::ostream& os) const;

private:
    std::vector<HeapSlot> slots;
    std::vector<uint32_t> freeList;
    std::vector<Value> tempRoots;

    // 字串池 (用於 String Interning，弱引用)
    std::unordered_map<std::string_view, GcHandle> stringPool;

    size_t bytesAllocated{0};
    size_t nextGcThreshold;
    size_t liveObjectCount{0};

    // 灰階物件佇列（三色標記法之工作清單）
    std::vector<GcObject*> grayList;

    // 內部輔助
    GcHandle insertObject(GcObject* obj);
    void checkGc();
    void trackAllocation(size_t bytes);

    void markValue(Value value);
    void markObject(GcObject* obj);
    void markHandle(GcHandle handle);
    void traceReferences();
    size_t sweep();
};
