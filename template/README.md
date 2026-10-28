# EasyScript C++ 原生外掛模組範本 (C++ Native Extension Template)

本目錄提供開發 **EasyScript 原生 C++ 動態鏈結庫 (Native Plugin / Shared Library)** 的完整獨立專案範本。

**本專案為完全獨立且自包含 (Self-contained)**：範本中已內建所有編譯所需之 EasyScript SDK 標頭檔與核心原始碼 (`sdk/`)，可複製至任意路徑單獨編譯與測試，不必放在 EasyScript 原始專案目錄下。

透過此範本，你可以使用現代 C++ (C++20) 為 EasyScript 撰寫高效能的底層延伸模組，支援數值運算、字串處理、陣列/映射構建，並原生支援 EasyScript Language Server (LSP) 代碼補全與懸停文檔。

---

## 📁 目錄結構

```text
template/
├── CMakeLists.txt         # 跨平台 CMake 建置設定 (支援獨立編譯與外部 SDK)
├── easy.json              # EPM (EasyScript Package Manager) 專案設定檔
├── my_module.d.json       # LSP 型別宣告與自動補全定義檔
├── test.easy              # EasyScript 模組呼叫與測試驗證腳本
├── build.ps1              # Windows 快速建置腳本 (PowerShell)
├── build.sh               # Linux / macOS 快速建置腳本 (Bash)
├── .gitignore             # Git 忽略設定
├── include/
│   └── my_module.hpp      # 模組 C++ 標頭檔
├── src/
│   └── my_module.cpp      # 模組 C++ 實作檔 (示範所有核心 API)
└── sdk/                   # 內建 EasyScript SDK (提供獨立編譯能力)
    ├── include/           # EasyScript 核心標頭檔 (easyscript_plugin.hpp 等)
    └── src/               # EasyScript 核心執行期與測試 CLI 源碼
```

---

## 🚀 快速上手

### 1. 先決條件 (Prerequisites)
- **C++20** 相容編譯器 (GCC 11+ / Clang 13+ / MSVC 2019+)
- **CMake 3.20+**
*(無需預先安裝 EasyScript，範本內建完整 SDK，編譯時會自動產生核心靜態庫與除錯用 easyscript 執行檔)*

### 2. 編譯模組 (Build)

#### 使用快速腳本 (推薦)
- **Windows (PowerShell)**：
  ```powershell
  # 僅編譯 (Release)
  .\build.ps1

  # 編譯並直接執行測試
  .\build.ps1 -RunTest
  ```
- **Linux / macOS (Bash)**：
  ```bash
  chmod +x build.sh
  ./build.sh --test
  ```

#### 手動使用 CMake
```bash
# 1. 建立配置 (預設自動使用內建 sdk/，亦可透過 -DEASYSCRIPT_ROOT 指定外部 SDK)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. 進行編譯
cmake --build build --config Release
```
編譯完成後，產生的動態庫 (`my_module.dll` / `my_module.so` / `my_module.dylib`) 將自動複製至專案根目錄。

### 3. 測試執行 (Run Test)
使用生成的 EasyScript CLI 或系統已安裝的 CLI 直接載入模組並執行：
```bash
easyscript test.easy
# 或使用本次建置產生的 CLI:
./build/easyscript test.easy  # (Windows: .\build\easyscript.exe test.easy)
```

---

## 🛠 原生 C++ 模組開發指南

### 1. 模組進入點 (Entry Point)
每個原生動態庫需匯出初始化函式 `easyscript_module_init`：

```cpp
#include "easyscript_plugin.hpp"

static VM* s_vm = nullptr;

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;

    // 註冊原生函式 (模組對象, 函式名稱, 函式指標, 參數個數 [-1 代表可變參數])
    vm->defineModuleNative(mod, "add", my_module::add, 2);

    // 註冊模組常數
    vm->defineModuleValue(mod, "VERSION", Value(100));

    return true; // 回傳 true 代表載入成功
}
```

### 2. 撰寫原生函式 (Native Functions)
原生函式簽章必須符合 `NativeFn`：
```cpp
Value my_func(int argCount, Value* args);
```

#### (1) 參數檢查與解包
```cpp
// 檢查參數個數
if (argCount < 2) {
    if (s_vm) s_vm->jitRuntimeError("Expected 2 arguments.");
    return Value();
}

// 讀取整數
int64_t a = args[0].isInt() ? *args[0].asInt() : 0;

// 讀取數值 (int 或 double 皆可)
double b = args[1].isNumber() ? *args[1].asNumber() : 0.0;

// 讀取布林值
bool flag = args[2].isBool() ? *args[2].asBool() : false;
```

#### (2) 字串處理與回傳
EasyScript 的字串受垃圾回收機制 (GC) 託管：
```cpp
// 從參數讀取字串
std::string text = "";
if (args[0].isObject() && s_vm) {
    auto* str = s_vm->getHeap().getAs<ObjString>(*args[0].asObject());
    if (str) text = str->text;
}

// 分配 GC 託管字串並回傳
GcHandle h = s_vm->getHeap().allocateString("Hello, " + text);
return Value(h);
```

#### (3) 陣列 (Array) 與映射 (Map)
```cpp
// 建立 Array
std::vector<Value> items;
items.push_back(Value(10));
items.push_back(Value(20));
GcHandle arrHandle = s_vm->getHeap().allocateArray(std::move(items));
return Value(arrHandle);

// 建立 Map (Dictionary)
std::unordered_map<Value, Value> map;
GcHandle key = s_vm->getHeap().allocateString("name");
GcHandle val = s_vm->getHeap().allocateString("EasyScript");
map[Value(key)] = Value(val);
GcHandle mapHandle = s_vm->getHeap().allocateMap(std::move(map));
return Value(mapHandle);
```

---

## 📝 LSP 型別宣告檔 (.d.json)

EasyScript 包含內建的 Language Server (LSP)。為原生模組撰寫同名 `.d.json` 檔案（例如 `my_module.d.json`），即可享有：
- **函式簽章提示 (Signature Help)**
- **自動補全 (Autocomplete)**
- **懸停文檔說明 (Hover Documentation)**

範例結構：
```json
{
  "module": "my_module",
  "version": "1.0.0",
  "doc": "模組詳細說明",
  "symbols": [
    {
      "name": "add",
      "kind": "function",
      "signature": "add(a: int, b: int): int",
      "doc": "計算兩數之和",
      "parameters": [
        { "name": "a", "type": "int", "doc": "第一個整數" },
        { "name": "b", "type": "int", "doc": "第二個整數" }
      ],
      "returnType": "int"
    }
  ]
}
```

---

## 📦 套件管理 (EPM) 整合

可將本模組發布或作為本地相依項目引入 EasyScript 專案中：

```bash
# 在 EasyScript 專案中新增此模組作為本地相依庫
epm add my_module --path ../path/to/my_module
```

在 EasyScript 腳本中引用：
```javascript
import my_module from "my_module";
print(my_module.add(10, 20));

// 或是具名引用
import { add, greet } from "my_module";
print(greet("Developer"));
```

