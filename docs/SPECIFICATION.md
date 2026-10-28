# EasyScript 程式語言規範與官方語法參考手冊
**版本**：1.0-RC  
**發行日期**：2026 年 10 月  
**語言規範狀態**：Official Reference Manual  

---

## 目錄 (Table of Contents)
1. [語言概述 (Introduction)](#1-語言概述-introduction)
2. [詞法結構 (Lexical Structure)](#2-詞法結構-lexical-structure)
3. [型別系統 (Type System)](#3-型別系統-type-system)
4. [宣告與變數 (Declarations and Variables)](#4-宣告與變數-declarations-and-variables)
5. [運算式與運算子 (Expressions and Operators)](#5-運算式與運算子-expressions-and-operators)
6. [語句與控制流程 (Statements and Control Flow)](#6-語句與控制流程-statements-and-control-flow)
7. [函式與閉包 (Functions and Closures)](#7-函式與閉包-functions-and-closures)
8. [物件導向系統 (Object-Oriented Programming)](#8-物件導向系統-object-oriented-programming)
9. [例外處理 (Exception Handling)](#9-例外處理-exception-handling)
10. [模組系統 (Module System)](#10-模組系統-module-system)
11. [內建型別方法參考 (Built-in Standard Methods)](#11-內建型別方法參考-built-in-standard-methods)
12. [附錄：語法文法摘要 (Grammar Summary in EBNF)](#12-附錄語法文法摘要-grammar-summary-in-ebnf)

---

## 1. 語言概述 (Introduction)

EasyScript 是一門現代化、多範式（Multi-paradigm）、兼具動態靈活性與漸進式靜態型別安全（Gradual Static Typing）的高效能腳本語言。其設計結合了簡潔優雅的語法、基於暫存器/堆疊混合的高效能 Bytecode 虛擬機、即時編譯（JIT）優化，以及完善的閉包（Closure / Upvalues）與物件導向系統。

### 核心設計理念
* **簡潔易讀**：吸納現代語言（TypeScript、Python、Ruby、Rust）的優秀語法特性。
* **漸進型別**：支援可選型別標註（Type Annotations），兼備動態開發的原型速度與編譯期型別檢查的穩健度。
* **零開銷抽象**：一級函式（First-class Functions）、詞法閉包、物件多型均具備優化的虛擬機底層發射。

---

## 2. 詞法結構 (Lexical Structure)

### 2.1 原始碼編碼
EasyScript 原始碼檔案必須以 **UTF-8** 編碼儲存。

### 2.2 空白與換行 (Whitespace)
空白字元（空格 `0x20`、水平定位字元 `\t`、換行符 `\r` 與 `\n`）用於分隔 Token，在語法結構中通常不具備語意。

### 2.3 註解 (Comments)
* **單行註解**：以 `//` 起始，至行末結束。
* **多行註解**：以 `/*` 開始，並以 `*/` 結束（不支援巢狀多行註解）。

```easyscript
// 這是單行註解
/* 這是
   多行註解 */
```

### 2.4 識別碼 (Identifiers)
識別碼由 ASCII 字母（`a`-`z`, `A`-`Z`）、底線（`_`）以及後續的數字（`0`-`9`）組成。區分大小寫。

### 2.5 保留關鍵字 (Keywords)
下列識別碼為語言保留字，不得直接作為變數、函式或類別名稱：

| 控制流程 | 宣告與定義 | 物件導向 | 模組化 | 例外處理 | 字面量與型別 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `if` | `let` | `class` | `import` | `try` | `true` |
| `else` | `const` | `init` | `export` | `catch` | `false` |
| `for` | `fn` | `this` | `from` | `finally` | `null` / `nil` |
| `while` | `return` | `super` | | `throw` | `int`, `double` |
| `in` | `break` | `public` | | | `bool`, `string` |
| | `continue` | `private` | | | `Map`, `void` |
| | | `protected`| | | |
| | | `override` | | | |

### 2.6 字面量 (Literals)
* **整數 (Integer Literals)**：
  - 十進位：`42`, `0`
  - 十六進位：`0x2A`, `0XFF`
  - 二進位：`0b101010`, `0B1100`
* **浮點數 (Floating-Point Literals)**：
  - 含有小數點且非區間運算子的數字：`3.14159`, `0.5`
* **布林值 (Boolean Literals)**：`true`、`false`。
* **空值 (Nil Literal)**：`null` 或 `nil`。
* **字串 (String Literals)**：以雙引號封裝：`"Hello, World"`。支援逸出字元（`\n`, `\t`, `\r`, `\"`, `\\`）。
* **字串內插 (String Interpolation)**：
  在字串內使用 `${ <expression> }` 可將任意運算式之求值結果直接格式化嵌入字串：
  ```easyscript
  let name = "Alice";
  let greeting = "Hello, ${name}! 1 + 1 = ${1 + 1}";
  ```

---

## 3. 型別系統 (Type System)

EasyScript 採用漸進式型別系統。變數與函式可選擇性標註型別。若未標註，預設推導或視為動態型別 `any`。

### 3.1 基礎型別 (Primitive Types)
* `int`：64 位元有號整數（Signed 64-bit Integer）。
* `double`：IEEE 754 雙精度 64 位元浮點數。
* `bool`：布林值（`true` 或 `false`）。
* `string`：不可變 UTF-8 字串物件。
* `void`：表示無回傳值。
* `nil`：表示無值或空參考。

### 3.2 複合型別 (Composite Types)
* **陣列型別**：`T[]`，例如 `int[]`、`string[]`。
* **字典型別**：`Map<KeyType, ValueType>`，例如 `Map<string, int>`。
* **區間型別**：`Range`，表示離散整數數列範圍。
* **類別型別**：自訂類別實例型別，以類別名稱表示（如 `Animal`）。
* **函式型別**：`(ParamTypes...) -> ReturnType`。

### 3.3 型別標註語法 (Type Annotations)
變數宣告、參數列表與函式回傳值均可標註型別，支援 `:` 或 `->` 符號：
```easyscript
let count: int = 10;
fn add(a: int, b: int) -> int {
    return a + b;
}
```

---

## 4. 宣告與變數 (Declarations and Variables)

### 4.1 變數宣告 (`let`)
`let` 用於宣告可重新賦值的區域變數或全域變數：
```easyscript
let x = 10;
let message: string = "EasyScript";
x = 20; // 合法
```

### 4.2 常數宣告 (`const`)
`const` 用於宣告不可變更綁定的常數，宣告時必須立即初始化：
```easyscript
const PI: double = 3.1415926535;
// PI = 3.0; // 編譯期錯誤：禁止指派常數
```

### 4.3 作用域規則 (Lexical Scoping)
* 採用塊級作用域（Block Scope）。
* 大括號 `{ ... }` 內部的宣告僅在該區塊及其巢狀子區塊可見。
* 支援變數遮蔽（Shadowing）：內層作用域可宣告與外層同名之變數。

---

## 5. 運算式與運算子 (Expressions and Operators)

### 5.1 運算子優先級表 (由高至低)

| 優先權 | 運算子 | 說明 | 結合性 |
| :--- | :--- | :--- | :--- |
| 1 (最高) | `.` `()` `[]` `++` `--` (後綴) | 成員存取、呼叫、索引/切片、後綴自增減 | 左至右 |
| 2 | `!` `-` (前綴單元) | 邏輯非、數值取負 | 右至左 |
| 3 | `*` `/` `%` | 乘法、除法、取模 | 左至右 |
| 4 | `+` `-` | 加法（字串拼接）、減法 | 左至右 |
| 5 | `..` | 區間生成運算子 | 左至右 |
| 6 | `<` `<=` `>` `>=` | 數值大小比較 | 左至右 |
| 7 | `==` `!=` | 相等性與不等性判定 | 左至右 |
| 8 | `&&` | 邏輯與（短路求值） | 左至右 |
| 9 | `\|\|` | 邏輯或（短路求值） | 左至右 |
| 10 (最低)| `=` `+=` `-=` `*=` `/=` `%=` | 賦值與複合賦值 | 右至左 |

### 5.2 條件運算式 (Conditional Expression)
`if-else` 可作為表達式使用，自動求值並產出對應分支的結果：
```easyscript
let status = if (score >= 60) "Pass" else "Fail";
```

### 5.3 區間運算式 (Range Expression)
使用 `..` 運算子建立半開區間 `[start, end)`：
```easyscript
let rng = 0..5; // 包含 0, 1, 2, 3, 4
```

### 5.4 索引與 Ruby 風格切片運算子 (Subscript & Slicing)
陣列與字串全面支援負數索引與多種切片語法：

1. **單元素存取（支援負數）**：
   * `arr[0]`：首個元素。
   * `arr[-1]`：最後一個元素（等價於 `arr[arr.length - 1]`）。
   * `arr[-2]`：倒數第二個元素。
2. **範圍切片 (`arr[start..end]`)**：
   * `arr[1..3]`：截取索引 1 至 2 之子陣列。
   * `arr[1..-1]`：截取索引 1 至倒數第 1 個元素之子陣列。
3. **雙參數長度切片 (`arr[start, length]`)**：
   * `arr[1, 3]`：從索引 1 起截取 3 個元素。
   * `arr[-3, 2]`：從倒數第 3 個元素起截取 2 個元素。
   * 邊界語意：若 `length < 0` 或 `start > size`，求值結果為 `nil`。
4. **負數索引寫入**：
   ```easyscript
   let list = [1, 2, 3];
   list[-1] = 99; // list 變為 [1, 2, 99]
   ```

---

## 6. 語句與控制流程 (Statements and Control Flow)

### 6.1 條件分支 (`if-else`)
```easyscript
if (x > 0) {
    print("Positive");
} else if (x == 0) {
    print("Zero");
} else {
    print("Negative");
}
```

### 6.2 迴圈結構
#### (1) `while` 迴圈
```easyscript
let i = 0;
while (i < 5) {
    print(i);
    i++;
}
```

#### (2) C-Style `for` 迴圈
```easyscript
for (let i = 0; i < 10; i++) {
    if (i == 3) continue;
    if (i == 7) break;
    print(i);
}
```

#### (3) 疊代器迴圈 (`for ... in`)
支援遍歷陣列、區間（Range）與各類可疊代容器：
```easyscript
for (item in ["apple", "banana", "cherry"]) {
    print(item);
}

for (i in 0..5) {
    print(i);
}
```

---

## 7. 函式與閉包 (Functions and Closures)

### 7.1 命名函式宣告
使用 `fn` 關鍵字宣告具名函式：
```easyscript
fn add(a: int, b: int) -> int {
    return a + b;
}
```

### 7.2 預設參數 (Default Parameters)
參數可具備預設值：
```easyscript
fn greet(name: string = "Guest") {
    print("Hello, ${name}!");
}
```

### 7.3 傳統匿名函式 (Lambda)
```easyscript
let multiply = fn(x: int, y: int) -> int {
    return x * y;
};
```

### 7.4 現代箭頭函式 (Arrow Functions)
EasyScript 支援現代 `() => {}` 式箭頭函式：
1. **區塊主體 (Block Body)**：
   ```easyscript
   let sum = (a, b) => {
       return a + b;
   };
   ```
2. **運算式主體 (Expression Body)**：省略大括號與 `return` 關鍵字，直接回傳運算式值：
   ```easyscript
   let square = (x: int) => x * x;
   let greet = () => "Hello World";
   ```
3. **帶型別標註**：
   ```easyscript
   let multiply = (a: int, b: int) -> int => a * b;
   ```

### 7.5 閉包語意 (Closures & Upvalues)
函式具備詞法閉包能力，能捕捉其外層作用域之區域變數，並保持獨立的狀態生命週期：
```easyscript
fn makeCounter(start: int) {
    let count = start;
    return () => {
        count += 1;
        return count;
    };
}

let counter = makeCounter(10);
print(counter()); // 11
print(counter()); // 12
```

---

## 8. 物件導向系統 (Object-Oriented Programming)

EasyScript 具備現代類別基（Class-based）單繼承物件導向系統。

### 8.1 類別定義與建構子
* 類別使用 `class` 宣告。
* 建構子以保留名稱 `init` 定義。
* 透過呼叫類別名稱即可建立實例（毋需 `new` 關鍵字）。

```easyscript
class Point {
    init(x: int, y: int) {
        this.x = x;
        this.y = y;
    }

    fn distanceSquared() -> int {
        return this.x * this.x + this.y * this.y;
    }
}

let pt = Point(3, 4);
print(pt.distanceSquared()); // 25
```

### 8.2 建構子參數屬性語法糖 (Parameter Properties)
建構子參數前置存取修飾詞（`public`、`private`）時，編譯器將自動生成同名欄位並於進入主體前完成賦值：
```easyscript
class User {
    public init(public name: string, public age: int) {}
}

let u = User("Bob", 20);
print(u.name); // Bob
```

### 8.3 繼承、方法覆寫與 `super`
* 透過冒號 `:` 繼承父類別。
* 子類別建構子使用 `super(...)` 調用父類別建構子。
* 覆寫方法使用 `override fn` 修飾。
* 透過 `super.method()` 調用父類別實作。

```easyscript
class Animal {
    public init(public name: string) {}

    public fn speak() {
        print("...");
    }
}

class Dog : Animal {
    public init(name: string, public breed: string) {
        super(name);
    }

    public override fn speak() {
        print("Woof!");
    }
}
```

---

## 9. 例外處理 (Exception Handling)

EasyScript 支援基於 `try-catch-finally` 的結構化例外處理機制：

```easyscript
try {
    throw "An unexpected error occurred";
} catch (err) {
    print("Caught error: ${err}");
} finally {
    print("Always executed.");
}
```

---

## 10. 模組系統 (Module System)

### 10.1 模組匯出 (`export`)
於頂層宣告前加上 `export` 即可將變數、常數、函式或類別對外公開：
```easyscript
// math_utils.easy
export fn add(a: int, b: int) -> int {
    return a + b;
}

export const PI = 3.14159;
```

### 10.2 模組匯入 (`import`)
```easyscript
import { add, PI } from "./math_utils";
import Math from "std/math";

print(add(10, 20));
print(Math.sqrt(16));
```

---

## 11. 內建型別方法參考 (Built-in Standard Methods)

### 11.1 陣列 (Array)
| 方法 / 屬性 | 說明 | 回傳值 |
| :--- | :--- | :--- |
| `.length` / `.size` | 陣列長度屬性 | `int` |
| `.isEmpty` | 判斷陣列是否為空 | `bool` |
| `.first` / `.last` | 讀取首項或尾項元素 | `T` 或 `nil` |
| `.push(val...)` | 於末尾追加元素 | `int` (新長度) |
| `.pop()` | 彈出末尾元素 | `T` |
| `.insert(idx, val)` | 於指定索引插入元素 | `void` |
| `.removeAt(idx)` | 移除指定索引元素 | `T` |
| `.clear()` | 清空陣列 | `void` |
| `.contains(val)` | 檢查是否包含特定值 | `bool` |
| `.join(delim = ",")` | 將元素串接為字串 | `string` |
| `.reverse()` | 反轉陣列 | `Array` (自身) |
| `.sort([comparator])` | 排序陣列 | `Array` (自身) |
| `.slice(start, [end])` | 區間截取（支援整數與 `Range`） | `Array` |
| **`.each(callback)`** | 遍歷元素並回傳陣列自身 (Ruby 風格) | `Array` (自身) |
| **`.each_with_index(cb)`** | 傳入 `(item, index)` 遍歷並回傳自身 | `Array` (自身) |
| `.forEach(callback)` | 遍歷元素 | `nil` |
| `.map(callback)` | 投影並產生新陣列 | `Array` |
| `.filter(callback)` | 篩選符合條件之元素 | `Array` |
| `.reduce(cb, [init])` | 累加歸納計算 | 歸納值 |
| `.find(callback)` | 尋找首個符合條件之元素 | `T` 或 `nil` |
| `.findIndex(cb)` | 尋找首個符合條件之索引 | `int` |

### 11.2 字串 (String)
| 方法 / 屬性 | 說明 | 回傳值 |
| :--- | :--- | :--- |
| `.length` | 字串字元長度屬性 | `int` |
| `str[i]` / `str[-1]` | 支援正負數索引取字元 | `string` |
| `str[start, length]` | 截取指定長度子字串 | `string` |
| `str[start..end]` | 範圍截取子字串 | `string` |
| `.split(delim)` | 依分隔符號分割字串 | `string[]` |
| `.trim()` | 去除前後空白字元 | `string` |
| `.toLowerCase()` | 轉換為全小寫字串 | `string` |
| `.toUpperCase()` | 轉換為全大寫字串 | `string` |
| `.startsWith(prefix)` | 檢查前綴 | `bool` |
| `.endsWith(suffix)` | 檢查後綴 | `bool` |
| `.indexOf(sub)` | 尋找子字串首個出現位置 | `int` (-1 表示未找到) |
| `.replace(old, new)` | 替換子字串 | `string` |

### 11.3 字典 (Map)
| 方法 / 屬性 | 說明 | 回傳值 |
| :--- | :--- | :--- |
| `.length` / `.size` | 鍵值對數量屬性 | `int` |
| `map[key]` | 讀取鍵值對（無則回傳 `nil`） | `V` 或 `nil` |
| `map[key] = val` | 寫入或更新鍵值對 | `V` |
| `.has(key)` | 檢查是否包含特定鍵 | `bool` |
| `.delete(key)` | 移除鍵值對 | `bool` |
| `.keys()` | 取得所有鍵之陣列 | `Array` |
| `.values()` | 取得所有值之陣列 | `Array` |
| `.clear()` | 清空字典 | `void` |

### 11.4 數值 (Number: int & double)
| 方法 | 說明 | 回傳值 |
| :--- | :--- | :--- |
| `.abs()` | 絕對值 | `int` / `double` |
| `.floor()` / `.ceil()` / `.round()` | 取整函式 | `int` |
| `.isFinite()` / `.isNaN()` | 浮點狀態檢驗 | `bool` |
| `.times(callback)` | 重複執行 `N` 次回呼 | `void` |
| `.upTo(limit, [callback])` | 遞增計數至 limit | `void` 或 `int[]` |
| `.downTo(limit, [callback])` | 遞減計數至 limit | `void` 或 `int[]` |

---

## 12. 附錄：語法文法摘要 (Grammar Summary in EBNF)

```ebnf
program         ::= declaration* EOF ;

declaration     ::= classDecl
                  | fnDecl
                  | varDecl
                  | constDecl
                  | importDecl
                  | exportDecl
                  | statement ;

varDecl         ::= "let" IDENTIFIER ( ":" type )? ( "=" expression )? ";" ;
constDecl       ::= "const" IDENTIFIER ( ":" type )? "=" expression ";" ;

fnDecl          ::= "fn" IDENTIFIER "(" parameters? ")" ( "->" type )? block ;
parameters      ::= parameter ( "," parameter )* ;
parameter       ::= IDENTIFIER ( ":" type )? ( "=" expression )? ;

classDecl       ::= "class" IDENTIFIER ( ":" IDENTIFIER ( "," IDENTIFIER )* )? "{" classMember* "}" ;
classMember     ::= modifier* ( "init" "(" methodParams? ")" block
                              | "override"? "fn" IDENTIFIER "(" methodParams? ")" ( "->" type )? block
                              | ";" ) ;
modifier        ::= "public" | "private" | "protected" ;
methodParams    ::= modifier* IDENTIFIER ( ":" type )? ( "=" expression )? ( "," methodParams )* ;

statement       ::= exprStmt
                  | ifStmt
                  | whileStmt
                  | forStmt
                  | returnStmt
                  | breakStmt
                  | continueStmt
                  | tryStmt
                  | throwStmt
                  | block ;

block           ::= "{" declaration* "}" ;

ifStmt          ::= "if" "(" expression ")" statement ( "else" statement )? ;
whileStmt       ::= "while" "(" expression ")" statement ;
forStmt         ::= "for" "(" ( varDecl | exprStmt | ";" ) expression? ";" expression? ")" statement
                  | "for" "(" IDENTIFIER "in" expression ")" statement ;

tryStmt         ::= "try" block ( "catch" "(" IDENTIFIER ( ":" type )? ")" block )? ( "finally" block )? ;
throwStmt       ::= "throw" expression ";" ;

expression      ::= assignment ;
assignment      ::= ( call "." IDENTIFIER | subscript ) ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) assignment
                  | conditional ;

conditional     ::= "if" "(" expression ")" expression "else" expression
                  | logicOr ;

logicOr         ::= logicAnd ( "||" logicAnd )* ;
logicAnd        ::= equality ( "&&" equality )* ;
equality        ::= comparison ( ( "!=" | "==" ) comparison )* ;
comparison      ::= rangeExpr ( ( ">" | ">=" | "<" | "<=" ) rangeExpr )* ;
rangeExpr       ::= term ( ".." term )? ;
term            ::= factor ( ( "-" | "+" ) factor )* ;
factor          ::= unary ( ( "/" | "*" | "%" ) unary )* ;
unary           ::= ( "!" | "-" ) unary
                  | postfix ;
postfix         ::= primary ( call | subscript | memberAccess | "++" | "--" )* ;

subscript       ::= "[" expression ( "," expression )? "]" ;
call            ::= "(" arguments? ")" ;
memberAccess    ::= "." IDENTIFIER ;

primary         ::= NUMBER | STRING | "true" | "false" | "null" | "nil"
                  | IDENTIFIER
                  | "(" expression ")"
                  | arrowFunction
                  | lambdaFunction
                  | arrayLiteral
                  | mapLiteral ;

arrowFunction   ::= "(" parameters? ")" ( "->" type )? "=>" ( block | expression ) ;
lambdaFunction  ::= "fn" "(" parameters? ")" ( "->" type )? block ;
arrayLiteral    ::= "[" ( expression ( "," expression )* )? "]" ;
mapLiteral      ::= "{" ( ( STRING | IDENTIFIER ) ":" expression ( "," ... )* )? "}" ;
```
