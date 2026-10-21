#include "heap.hpp"
#include <iomanip>

Heap::Heap(size_t initialThreshold)
    : nextGcThreshold(initialThreshold) {
    // 保留 Slot 0 為無效 Handle
    slots.push_back(HeapSlot{nullptr, false});
}

Heap::~Heap() {
    stringPool.clear();
    for (size_t i = 1; i < slots.size(); ++i) {
        if (slots[i].inUse && slots[i].object != nullptr) {
            delete slots[i].object;
            slots[i].object = nullptr;
            slots[i].inUse = false;
        }
    }
}

GcHandle Heap::insertObject(GcObject* obj) {
    if (!freeList.empty()) {
        uint32_t index = freeList.back();
        freeList.pop_back();
        slots[index].object = obj;
        slots[index].inUse = true;
        liveObjectCount++;
        return GcHandle(index);
    }

    uint32_t index = static_cast<uint32_t>(slots.size());
    slots.push_back(HeapSlot{obj, true});
    liveObjectCount++;
    return GcHandle(index);
}

GcObject* Heap::get(GcHandle handle) const noexcept {
    if (handle.id == 0 || handle.id >= slots.size()) {
        return nullptr;
    }
    const auto& slot = slots[handle.id];
    return slot.inUse ? slot.object : nullptr;
}

bool Heap::isValid(GcHandle handle) const noexcept {
    return get(handle) != nullptr;
}

void Heap::trackAllocation(size_t bytes) {
    bytesAllocated += bytes;
}

void Heap::checkGc() {
    if (bytesAllocated > nextGcThreshold) {
        collectGarbage(tempRoots);
    }
}

// -----------------------------------------------------------------------------
// 特定型別分配
// -----------------------------------------------------------------------------

GcHandle Heap::internString(std::string_view text) {
    auto it = stringPool.find(text);
    if (it != stringPool.end()) {
        return it->second;
    }

    checkGc();
    auto* strObj = new ObjString(std::string(text));
    GcHandle handle = insertObject(strObj);
    trackAllocation(sizeof(ObjString) + strObj->text.capacity());

    stringPool[strObj->view()] = handle;
    return handle;
}

GcHandle Heap::allocateString(std::string text) {
    return internString(text);
}

GcHandle Heap::allocateArray(std::vector<Value> elements) {
    checkGc();
    size_t extra = elements.capacity() * sizeof(Value);
    auto* arrObj = new ObjArray(std::move(elements));
    GcHandle handle = insertObject(arrObj);
    trackAllocation(sizeof(ObjArray) + extra);
    return handle;
}

GcHandle Heap::allocateMap(std::unordered_map<Value, Value> table) {
    checkGc();
    auto* mapObj = new ObjMap(std::move(table));
    GcHandle handle = insertObject(mapObj);
    trackAllocation(sizeof(ObjMap));
    return handle;
}

// -----------------------------------------------------------------------------
// 暫存根物件保護
// -----------------------------------------------------------------------------

void Heap::pushTempRoot(GcHandle handle) {
    tempRoots.emplace_back(handle);
}

void Heap::popTempRoot() {
    if (!tempRoots.empty()) {
        tempRoots.pop_back();
    }
}

void Heap::pushTempRoot(Value value) {
    tempRoots.push_back(value);
}

void Heap::popTempRoot(size_t count) {
    while (count > 0 && !tempRoots.empty()) {
        tempRoots.pop_back();
        count--;
    }
}

// -----------------------------------------------------------------------------
// 垃圾回收 (Mark & Sweep)
// -----------------------------------------------------------------------------

void Heap::markHandle(GcHandle handle) {
    GcObject* obj = get(handle);
    if (obj != nullptr) {
        markObject(obj);
    }
}

void Heap::markValue(Value value) {
    if (value.isObject()) {
        auto handle = value.asObject();
        if (handle) {
            markHandle(*handle);
        }
    }
}

void Heap::markObject(GcObject* obj) {
    if (obj == nullptr || obj->marked) {
        return;
    }

    obj->marked = true;
    grayList.push_back(obj);
}

void Heap::traceReferences() {
    while (!grayList.empty()) {
        GcObject* obj = grayList.back();
        grayList.pop_back();

        obj->trace([this](Value val) {
            markValue(val);
        });
    }
}

size_t Heap::sweep() {
    size_t freedCount = 0;

    for (size_t i = 1; i < slots.size(); ++i) {
        if (!slots[i].inUse || slots[i].object == nullptr) {
            continue;
        }

        GcObject* obj = slots[i].object;
        if (obj->marked) {
            // 物件存活，重設標記
            obj->marked = false;
        } else {
            // 釋放未被引用的物件
            delete obj;
            slots[i].object = nullptr;
            slots[i].inUse = false;
            freeList.push_back(static_cast<uint32_t>(i));
            liveObjectCount--;
            freedCount++;
        }
    }

    return freedCount;
}

size_t Heap::collectGarbage(const std::vector<Value>& roots) {
    // 1. 標記階段 (Mark Roots)
    for (const auto& root : roots) {
        markValue(root);
    }
    for (const auto& temp : tempRoots) {
        markValue(temp);
    }

    // 2. 追蹤所有引用的物件 (Trace)
    traceReferences();

    // 3. 清理字串池中未被標記的字串 (Weak Reference Clean)
    for (auto it = stringPool.begin(); it != stringPool.end(); ) {
        GcObject* obj = get(it->second);
        if (obj == nullptr || !obj->marked) {
            it = stringPool.erase(it);
        } else {
            ++it;
        }
    }

    // 4. 清除未被標記的物件 (Sweep)
    size_t freed = sweep();

    // 5. 調整下次 GC 觸發門檻
    nextGcThreshold = std::max(bytesAllocated * 2, size_t(1024 * 1024));

    return freed;
}

// -----------------------------------------------------------------------------
// 除錯輸出
// -----------------------------------------------------------------------------

void Heap::dump(std::ostream& os) const {
    os << "=== Heap Dump ===" << "\n";
    os << "Live Objects: " << liveObjectCount << "\n";
    os << "Total Slots: " << slots.size() << "\n";
    os << "Free Slots: " << freeList.size() << "\n";
    os << "String Pool Size: " << stringPool.size() << "\n";
    os << "------------------------------------------\n";

    for (size_t i = 1; i < slots.size(); ++i) {
        if (slots[i].inUse && slots[i].object != nullptr) {
            os << "  #" << std::setw(3) << i << ": ["
               << objTypeName(slots[i].object->type) << "] "
               << *slots[i].object << "\n";
        }
    }
    os << "==========================================" << std::endl;
}
