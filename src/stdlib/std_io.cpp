#include "easyscript_plugin.hpp"
#include "objects/obj_class.hpp"
#include "objects/obj_instance.hpp"
#include "objects/obj_array.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <cctype>

#if defined(_WIN32) || defined(__CYGWIN__)
  #include <windows.h>
  #include <io.h>
  #include <conio.h>
  #define isatty _isatty
  #define fileno _fileno
#else
  #include <unistd.h>
  #include <termios.h>
  #include <sys/ioctl.h>
#endif

static VM* s_vm = nullptr;

// -----------------------------------------------------------------------------
// 輔助工具函式 (Helper Utilities)
// -----------------------------------------------------------------------------

static void enableVirtualTerminal() {
#if defined(_WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hErr, &dwMode)) {
            SetConsoleMode(hErr, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    // 預設將控制台輸出與輸入編碼設定為 UTF-8 (Code Page 65001)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static int readSingleChar() {
#if defined(_WIN32)
    return _getch();
#else
    struct termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        return getchar();
    }
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
#endif
}

static void getTerminalSize(int& width, int& height) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return;
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        width = w.ws_col;
        height = w.ws_row;
        return;
    }
#endif
    width = 80;
    height = 24;
}

static void getCursorPosition(int& x, int& y) {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        x = csbi.dwCursorPosition.X - csbi.srWindow.Left + 1;
        y = csbi.dwCursorPosition.Y - csbi.srWindow.Top + 1;
        return;
    }
#endif
    x = 1;
    y = 1;
}

static bool isInteractiveTerminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

static std::string valToStr(const Value& v) {
    if (v.isObject() && s_vm) {
        auto* str = s_vm->getHeap().getAs<ObjString>(*v.asObject());
        if (str) return str->text;
    }
    return s_vm ? s_vm->valueToString(v) : "";
}

static Value strVal(const std::string& str) {
    if (!s_vm) return Value();
    return Value(s_vm->getHeap().allocateString(str));
}

static void defineClassNative(ObjClass* klass, const std::string& name, NativeFn fn, int arity = -1) {
    if (!klass || !s_vm) return;
    GcHandle handle = s_vm->getHeap().allocate<ObjNativeFn>(fn, klass->name + "." + name, arity);
    klass->staticFields[name] = Value(handle);
}

static void defineInstanceNative(ObjClass* klass, const std::string& name, NativeFn fn, int arity = -1) {
    if (!klass || !s_vm) return;
    GcHandle handle = s_vm->getHeap().allocate<ObjNativeFn>(fn, klass->name + "." + name, arity);
    Value val(handle);
    klass->methods[name] = val;
    uint32_t slot = static_cast<uint32_t>(klass->vtable.size());
    klass->vtable.push_back(val);
    klass->methodSlots[name] = slot;
}

static void defineClassValue(ObjClass* klass, const std::string& name, Value val) {
    if (!klass) return;
    klass->staticFields[name] = val;
}

// -----------------------------------------------------------------------------
// 1. 根層級：基本輸出輸入 (Root Basic I/O)
// -----------------------------------------------------------------------------

// write(...args)
static Value io_write(int argCount, Value* args) {
    for (int i = 0; i < argCount; ++i) {
        std::cout << valToStr(args[i]);
    }
    return Value();
}

// writeln(...args)
static Value io_writeln(int argCount, Value* args) {
    for (int i = 0; i < argCount; ++i) {
        std::cout << valToStr(args[i]);
    }
    std::cout << "\n";
    return Value();
}

// writeError(...args)
static Value io_writeError(int argCount, Value* args) {
    for (int i = 0; i < argCount; ++i) {
        std::cerr << valToStr(args[i]);
    }
    return Value();
}

// writeErrorln(...args)
static Value io_writeErrorln(int argCount, Value* args) {
    for (int i = 0; i < argCount; ++i) {
        std::cerr << valToStr(args[i]);
    }
    std::cerr << "\n";
    return Value();
}

// readLine(): string | nil
static Value io_readLine(int, Value*) {
    if (!s_vm) return Value();
    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return strVal(line);
    }
    return Value();
}

// readAll(): string
static Value io_readAll(int, Value*) {
    if (!s_vm) return Value();
    std::stringstream ss;
    ss << std::cin.rdbuf();
    return strVal(ss.str());
}

// readChar(): string | nil
static Value io_readChar(int, Value*) {
    if (!s_vm) return Value();
    int ch = readSingleChar();
    if (ch == EOF) return Value();
    std::string s(1, static_cast<char>(ch));
    return strVal(s);
}

// flush()
static Value io_flush(int, Value*) {
    std::cout.flush();
    return Value();
}

// flushError()
static Value io_flushError(int, Value*) {
    std::cerr.flush();
    return Value();
}

// -----------------------------------------------------------------------------
// 2. Terminal 類別：終端螢幕與游標控制 (Terminal Control)
// -----------------------------------------------------------------------------

// Terminal.clear()
static Value term_clear(int, Value*) {
    std::cout << "\033[2J\033[H";
    std::cout.flush();
    return Value();
}

// Terminal.clearLine()
static Value term_clearLine(int, Value*) {
    std::cout << "\033[2K\r";
    std::cout.flush();
    return Value();
}

// Terminal.clearToEnd()
static Value term_clearToEnd(int, Value*) {
    std::cout << "\033[0J";
    std::cout.flush();
    return Value();
}

// Terminal.setCursor(x, y)
static Value term_setCursor(int argCount, Value* args) {
    int x = 1, y = 1;
    if (argCount >= 1 && args[0].isInt()) x = static_cast<int>(*args[0].asInt());
    if (argCount >= 2 && args[1].isInt()) y = static_cast<int>(*args[1].asInt());
    std::cout << "\033[" << y << ";" << x << "H";
    std::cout.flush();
    return Value();
}

// Terminal.getCursor(): Map { "x": x, "y": y }
static Value term_getCursor(int, Value*) {
    if (!s_vm) return Value();
    int x = 1, y = 1;
    getCursorPosition(x, y);
    GcHandle mapH = s_vm->getHeap().allocate<ObjMap>();
    auto* map = s_vm->getHeap().getAs<ObjMap>(mapH);
    map->table[strVal("x")] = Value(static_cast<int64_t>(x));
    map->table[strVal("y")] = Value(static_cast<int64_t>(y));
    return Value(mapH);
}

// Terminal.moveUp(n = 1)
static Value term_moveUp(int argCount, Value* args) {
    int n = 1;
    if (argCount >= 1 && args[0].isInt()) n = static_cast<int>(*args[0].asInt());
    std::cout << "\033[" << n << "A";
    std::cout.flush();
    return Value();
}

// Terminal.moveDown(n = 1)
static Value term_moveDown(int argCount, Value* args) {
    int n = 1;
    if (argCount >= 1 && args[0].isInt()) n = static_cast<int>(*args[0].asInt());
    std::cout << "\033[" << n << "B";
    std::cout.flush();
    return Value();
}

// Terminal.moveRight(n = 1)
static Value term_moveRight(int argCount, Value* args) {
    int n = 1;
    if (argCount >= 1 && args[0].isInt()) n = static_cast<int>(*args[0].asInt());
    std::cout << "\033[" << n << "C";
    std::cout.flush();
    return Value();
}

// Terminal.moveLeft(n = 1)
static Value term_moveLeft(int argCount, Value* args) {
    int n = 1;
    if (argCount >= 1 && args[0].isInt()) n = static_cast<int>(*args[0].asInt());
    std::cout << "\033[" << n << "D";
    std::cout.flush();
    return Value();
}

// Terminal.hideCursor()
static Value term_hideCursor(int, Value*) {
    std::cout << "\033[?25l";
    std::cout.flush();
    return Value();
}

// Terminal.showCursor()
static Value term_showCursor(int, Value*) {
    std::cout << "\033[?25h";
    std::cout.flush();
    return Value();
}

// Terminal.getWidth(): int
static Value term_getWidth(int, Value*) {
    int w = 80, h = 24;
    getTerminalSize(w, h);
    return Value(static_cast<int64_t>(w));
}

// Terminal.getHeight(): int
static Value term_getHeight(int, Value*) {
    int w = 80, h = 24;
    getTerminalSize(w, h);
    return Value(static_cast<int64_t>(h));
}

// Terminal.getSize(): Map { "width": w, "height": h }
static Value term_getSize(int, Value*) {
    if (!s_vm) return Value();
    int w = 80, h = 24;
    getTerminalSize(w, h);
    GcHandle mapH = s_vm->getHeap().allocate<ObjMap>();
    auto* map = s_vm->getHeap().getAs<ObjMap>(mapH);
    map->table[strVal("width")] = Value(static_cast<int64_t>(w));
    map->table[strVal("height")] = Value(static_cast<int64_t>(h));
    return Value(mapH);
}

// Terminal.isTerminal(): bool
static Value term_isTerminal(int, Value*) {
    return Value(isInteractiveTerminal());
}

// Terminal.beep()
static Value term_beep(int, Value*) {
    std::cout << "\a";
    std::cout.flush();
    return Value();
}

// -----------------------------------------------------------------------------
// 編碼解析與轉換輔助 (Encoding Helpers)
// -----------------------------------------------------------------------------

static unsigned int parseEncodingToCodePage(const Value& val) {
    if (val.isInt()) {
        return static_cast<unsigned int>(*val.asInt());
    }
    std::string s = valToStr(val);
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    if (lower == "utf-8" || lower == "utf8" || lower == "utf_8") return 65001;
    if (lower == "big5" || lower == "cp950") return 950;
    if (lower == "gbk" || lower == "gb2312" || lower == "cp936" || lower == "gb18030") return 936;
    if (lower == "shift-jis" || lower == "shift_jis" || lower == "sjis" || lower == "cp932") return 932;
    if (lower == "ascii" || lower == "cp437") return 437;
    if (lower == "latin1" || lower == "iso-8859-1" || lower == "cp1252") return 1252;
    if (lower == "euc-kr" || lower == "cp949") return 949;

    if (lower.rfind("cp", 0) == 0) {
        try {
            return static_cast<unsigned int>(std::stoul(lower.substr(2)));
        } catch (...) {}
    }

    try {
        return static_cast<unsigned int>(std::stoul(lower));
    } catch (...) {}

    return 0;
}

static std::string codePageToEncodingName(unsigned int cp) {
    switch (cp) {
        case 65001: return "utf-8";
        case 950:   return "big5";
        case 936:   return "gbk";
        case 932:   return "shift-jis";
        case 437:   return "ascii";
        case 1252:  return "cp1252";
        case 949:   return "euc-kr";
        default:    return "cp" + std::to_string(cp);
    }
}

// Terminal.setEncoding(encoding: string | int, setInput: bool = true): bool
static Value term_setEncoding(int argCount, Value* args) {
    if (argCount < 1) return Value(false);
    unsigned int cp = parseEncodingToCodePage(args[0]);
    if (cp == 0) return Value(false);

    bool setInput = true;
    if (argCount >= 2 && args[1].isBool()) {
        setInput = *args[1].asBool();
    }

#if defined(_WIN32)
    BOOL okOut = SetConsoleOutputCP(cp);
    BOOL okIn = TRUE;
    if (setInput) {
        okIn = SetConsoleCP(cp);
    }
    return Value(static_cast<bool>(okOut && okIn));
#else
    if (cp == 65001) {
        std::setlocale(LC_ALL, "C.UTF-8");
    }
    return Value(true);
#endif
}

// Terminal.getEncoding(): string
static Value term_getEncoding(int, Value*) {
    if (!s_vm) return Value();
#if defined(_WIN32)
    unsigned int cp = GetConsoleOutputCP();
    return strVal(codePageToEncodingName(cp));
#else
    return strVal("utf-8");
#endif
}

// Terminal.getCodePage(): int
static Value term_getCodePage(int, Value*) {
#if defined(_WIN32)
    unsigned int cp = GetConsoleOutputCP();
    return Value(static_cast<int64_t>(cp));
#else
    return Value(static_cast<int64_t>(65001));
#endif
}

// Terminal.setCodePage(cp: int, setInput: bool = true): bool
static Value term_setCodePage(int argCount, Value* args) {
    if (argCount < 1 || !args[0].isInt()) return Value(false);
    return term_setEncoding(argCount, args);
}

// -----------------------------------------------------------------------------
// 3. Color 類別：ANSI 終端顏色與文字樣式 (Color & Style)
// -----------------------------------------------------------------------------

#define DEFINE_COLOR_FN(name, code) \
static Value color_##name(int argCount, Value* args) { \
    if (argCount < 1) return strVal(""); \
    std::string s = valToStr(args[0]); \
    return strVal("\033[" #code "m" + s + "\033[0m"); \
}

// 前景色彩
DEFINE_COLOR_FN(red, 31)
DEFINE_COLOR_FN(green, 32)
DEFINE_COLOR_FN(yellow, 33)
DEFINE_COLOR_FN(blue, 34)
DEFINE_COLOR_FN(magenta, 35)
DEFINE_COLOR_FN(cyan, 36)
DEFINE_COLOR_FN(white, 37)
DEFINE_COLOR_FN(gray, 90)

// 背景色彩
DEFINE_COLOR_FN(bgRed, 41)
DEFINE_COLOR_FN(bgGreen, 42)
DEFINE_COLOR_FN(bgYellow, 43)
DEFINE_COLOR_FN(bgBlue, 44)
DEFINE_COLOR_FN(bgMagenta, 45)
DEFINE_COLOR_FN(bgCyan, 46)
DEFINE_COLOR_FN(bgWhite, 47)
DEFINE_COLOR_FN(bgGray, 100)

// 文字樣式
DEFINE_COLOR_FN(bold, 1)
DEFINE_COLOR_FN(dim, 2)
DEFINE_COLOR_FN(italic, 3)
DEFINE_COLOR_FN(underline, 4)
DEFINE_COLOR_FN(inverse, 7)

// Color.rgb(r, g, b, text)
static Value color_rgb(int argCount, Value* args) {
    if (argCount < 4) return strVal("");
    int r = args[0].isInt() ? static_cast<int>(*args[0].asInt()) : 0;
    int g = args[1].isInt() ? static_cast<int>(*args[1].asInt()) : 0;
    int b = args[2].isInt() ? static_cast<int>(*args[2].asInt()) : 0;
    std::string text = valToStr(args[3]);
    std::string res = "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m" + text + "\033[0m";
    return strVal(res);
}

// Color.bgRgb(r, g, b, text)
static Value color_bgRgb(int argCount, Value* args) {
    if (argCount < 4) return strVal("");
    int r = args[0].isInt() ? static_cast<int>(*args[0].asInt()) : 0;
    int g = args[1].isInt() ? static_cast<int>(*args[1].asInt()) : 0;
    int b = args[2].isInt() ? static_cast<int>(*args[2].asInt()) : 0;
    std::string text = valToStr(args[3]);
    std::string res = "\033[48;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m" + text + "\033[0m";
    return strVal(res);
}

// Color.strip(text)
static Value color_strip(int argCount, Value* args) {
    if (argCount < 1) return strVal("");
    std::string text = valToStr(args[0]);
    static const std::regex ansiRegex(R"(\x1b\[[0-9;]*[a-zA-Z]|\x1b\][^\x07\x1b]*(\x07|\x1b\\))");
    std::string cleaned = std::regex_replace(text, ansiRegex, "");
    return strVal(cleaned);
}

// -----------------------------------------------------------------------------
// 4. Prompt 類別：互動式問答與提示 (Interactive Prompt)
// -----------------------------------------------------------------------------

// Prompt.ask(question, defaultVal = "")
static Value prompt_ask(int argCount, Value* args) {
    if (argCount >= 1) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }
    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() && argCount >= 2) {
            return strVal(valToStr(args[1]));
        }
        return strVal(line);
    }
    if (argCount >= 2) return strVal(valToStr(args[1]));
    return strVal("");
}

// Prompt.confirm(question, defaultVal = false)
static Value prompt_confirm(int argCount, Value* args) {
    bool def = false;
    if (argCount >= 2 && args[1].isBool()) {
        def = *args[1].asBool();
    }

    std::string q = (argCount >= 1) ? valToStr(args[0]) : "Confirm?";
    std::cout << q << (def ? " [Y/n] " : " [y/N] ");
    std::cout.flush();

    int ch = readSingleChar();
    if (ch == '\r' || ch == '\n' || ch == EOF) {
        std::cout << (def ? "Y\n" : "N\n");
        std::cout.flush();
        return Value(def);
    }

    char c = static_cast<char>(std::tolower(ch));
    std::cout << static_cast<char>(ch) << "\n";
    std::cout.flush();

    if (c == 'y') return Value(true);
    if (c == 'n') return Value(false);
    return Value(def);
}

// Prompt.password(question, maskChar = "*")
static Value prompt_password(int argCount, Value* args) {
    if (argCount >= 1) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }

    std::string mask = "*";
    if (argCount >= 2) {
        mask = valToStr(args[1]);
    }

    std::string pass;
    while (true) {
        int ch = readSingleChar();
        if (ch == EOF || ch == '\r' || ch == '\n') {
            std::cout << "\n";
            std::cout.flush();
            break;
        } else if (ch == 3) { // Ctrl+C
            std::cout << "\n";
            std::cout.flush();
            return strVal("");
        } else if (ch == '\b' || ch == 127 || ch == 8) { // Backspace
            if (!pass.empty()) {
                pass.pop_back();
                if (!mask.empty()) {
                    std::cout << "\b \b";
                    std::cout.flush();
                }
            }
        } else if (ch >= 32 && ch <= 126) { // Printable characters
            pass.push_back(static_cast<char>(ch));
            if (!mask.empty()) {
                std::cout << mask;
                std::cout.flush();
            }
        }
    }
    return strVal(pass);
}

// Prompt.choose(question, optionsArray): int (0-based, or -1 on invalid)
static Value prompt_choose(int argCount, Value* args) {
    if (argCount < 2 || !s_vm) return Value(static_cast<int64_t>(-1));
    std::string q = valToStr(args[0]);
    if (!args[1].isObject()) return Value(static_cast<int64_t>(-1));

    auto* arr = s_vm->getHeap().getAs<ObjArray>(*args[1].asObject());
    if (!arr || arr->elements.empty()) return Value(static_cast<int64_t>(-1));

    std::cout << q << "\n";
    for (size_t i = 0; i < arr->elements.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << valToStr(arr->elements[i]) << "\n";
    }
    std::cout << "請選擇 [1-" << arr->elements.size() << "]: ";
    std::cout.flush();

    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            int sel = std::stoi(line);
            if (sel >= 1 && sel <= static_cast<int>(arr->elements.size())) {
                return Value(static_cast<int64_t>(sel - 1));
            }
        } catch (...) {}
    }
    return Value(static_cast<int64_t>(-1));
}

// -----------------------------------------------------------------------------
// 外掛初始化進入點 (Module Init Entrypoint)
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// 5. 串流文字掃描器 (Scanner)
// -----------------------------------------------------------------------------

static GcHandle s_scannerClassHandle;

static ObjInstance* getScannerInstance(int argCount) {
    if (!s_vm) return nullptr;
    Value receiver = s_vm->peek(argCount);
    if (!receiver.isObject()) return nullptr;
    return s_vm->getHeap().getAs<ObjInstance>(*receiver.asObject());
}

static bool isDelimiterChar(char c, const std::string& delim) {
    return delim.find(c) != std::string::npos;
}

static void ensureScannerFields(ObjInstance* inst, bool fromStdin, std::string sourceText = "") {
    inst->fields.resize(6);
    if (fromStdin) {
        inst->fields[0] = Value(); // source nil
    } else {
        GcHandle h = s_vm->getHeap().allocateString(std::move(sourceText));
        inst->fields[0] = Value(h);
    }
    inst->fields[1] = Value(int64_t(0)); // pos
    GcHandle dHandle = s_vm->getHeap().allocateString(" \t\r\n");
    inst->fields[2] = Value(dHandle); // delim
    inst->fields[3] = Value(fromStdin); // fromStdin
    inst->fields[4] = Value(false); // closed
    GcHandle bufHandle = s_vm->getHeap().allocateString("");
    inst->fields[5] = Value(bufHandle); // stdinBuffer
}

static Value scanner_init(int argCount, Value* args) {
    auto* inst = getScannerInstance(argCount);
    if (!inst) return Value();

    bool fromStdin = true;
    std::string text;
    if (argCount >= 1 && !args[0].isNull()) {
        fromStdin = false;
        text = valToStr(args[0]);
    }
    ensureScannerFields(inst, fromStdin, std::move(text));
    return s_vm->peek(argCount);
}

static bool fetchNextToken(ObjInstance* inst, bool consume, std::string& outToken) {
    if (!inst || inst->fields.size() < 6 || inst->fields[4].asBool().value_or(false)) return false;

    bool fromStdin = inst->fields[3].asBool().value_or(false);
    std::string delim = " \t\r\n";
    if (inst->fields[2].isObject()) {
        if (auto* dStr = s_vm->getHeap().getAs<ObjString>(*inst->fields[2].asObject())) {
            delim = dStr->text;
        }
    }

    int64_t pos = *inst->fields[1].asInt();

    if (!fromStdin) {
        if (!inst->fields[0].isObject()) return false;
        auto* srcStr = s_vm->getHeap().getAs<ObjString>(*inst->fields[0].asObject());
        if (!srcStr) return false;
        const std::string& text = srcStr->text;

        while (pos < static_cast<int64_t>(text.size()) && isDelimiterChar(text[pos], delim)) {
            pos++;
        }
        if (pos >= static_cast<int64_t>(text.size())) return false;

        int64_t start = pos;
        while (pos < static_cast<int64_t>(text.size()) && !isDelimiterChar(text[pos], delim)) {
            pos++;
        }
        outToken = text.substr(start, pos - start);
        if (consume) {
            inst->fields[1] = Value(pos);
        }
        return true;
    } else {
        std::string buffer;
        if (inst->fields[5].isObject()) {
            if (auto* bStr = s_vm->getHeap().getAs<ObjString>(*inst->fields[5].asObject())) {
                buffer = bStr->text;
            }
        }

        while (true) {
            while (pos < static_cast<int64_t>(buffer.size()) && isDelimiterChar(buffer[pos], delim)) {
                pos++;
            }
            if (pos < static_cast<int64_t>(buffer.size())) {
                int64_t start = pos;
                while (pos < static_cast<int64_t>(buffer.size()) && !isDelimiterChar(buffer[pos], delim)) {
                    pos++;
                }
                outToken = buffer.substr(start, pos - start);
                if (consume) {
                    inst->fields[1] = Value(pos);
                }
                return true;
            }
            // Buffer exhausted, read from stdin
            std::string line;
            if (!std::getline(std::cin, line)) {
                return false; // EOF
            }
            buffer = line + "\n";
            pos = 0;
            GcHandle bHandle = s_vm->getHeap().allocateString(buffer);
            inst->fields[5] = Value(bHandle);
            inst->fields[1] = Value(int64_t(0));
        }
    }
}

static bool fetchNextLine(ObjInstance* inst, bool consume, std::string& outLine) {
    if (!inst || inst->fields.size() < 6 || inst->fields[4].asBool().value_or(false)) return false;

    bool fromStdin = inst->fields[3].asBool().value_or(false);
    int64_t pos = *inst->fields[1].asInt();

    if (!fromStdin) {
        if (!inst->fields[0].isObject()) return false;
        auto* srcStr = s_vm->getHeap().getAs<ObjString>(*inst->fields[0].asObject());
        if (!srcStr) return false;
        const std::string& text = srcStr->text;

        if (pos >= static_cast<int64_t>(text.size())) return false;

        size_t nextN = text.find('\n', pos);
        size_t end = (nextN == std::string::npos) ? text.size() : nextN;
        outLine = text.substr(pos, end - pos);
        if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();

        if (consume) {
            inst->fields[1] = Value(int64_t((nextN == std::string::npos) ? text.size() : nextN + 1));
        }
        return true;
    } else {
        std::string buffer;
        if (inst->fields[5].isObject()) {
            if (auto* bStr = s_vm->getHeap().getAs<ObjString>(*inst->fields[5].asObject())) {
                buffer = bStr->text;
            }
        }

        if (pos < static_cast<int64_t>(buffer.size())) {
            size_t nextN = buffer.find('\n', pos);
            size_t end = (nextN == std::string::npos) ? buffer.size() : nextN;
            outLine = buffer.substr(pos, end - pos);
            if (!outLine.empty() && outLine.back() == '\r') outLine.pop_back();
            if (consume) {
                inst->fields[1] = Value(int64_t((nextN == std::string::npos) ? buffer.size() : nextN + 1));
            }
            return true;
        } else {
            std::string line;
            if (!std::getline(std::cin, line)) {
                return false;
            }
            outLine = line;
            if (consume) {
                inst->fields[1] = Value(int64_t(0));
                inst->fields[5] = Value(s_vm->getHeap().allocateString(""));
            }
            return true;
        }
    }
}

static Value scanner_next(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    if (!fetchNextToken(inst, true, token)) {
        return Value(); // nil on EOF
    }
    return Value(s_vm->getHeap().allocateString(std::move(token)));
}

static Value scanner_nextLine(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string line;
    if (!fetchNextLine(inst, true, line)) {
        return Value(); // nil on EOF
    }
    return Value(s_vm->getHeap().allocateString(std::move(line)));
}

static bool parseIntegerToken(const std::string& token, int radix, int64_t& outVal) {
    if (token.empty()) return false;
    const char* str = token.c_str();
    bool neg = false;
    if (*str == '-') { neg = true; str++; }
    else if (*str == '+') { str++; }
    if (*str == '\0') return false;

    if ((radix == 0 || radix == 2) && str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) {
        str += 2;
        radix = 2;
    } else if ((radix == 0 || radix == 8) && str[0] == '0' && (str[1] == 'o' || str[1] == 'O')) {
        str += 2;
        radix = 8;
    } else if ((radix == 0 || radix == 16) && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        radix = 16;
    } else if (radix == 0) {
        radix = 10;
    }

    if (*str == '\0') return false;
    char* endPtr = nullptr;
    errno = 0;
    long long val = std::strtoll(str, &endPtr, radix);
    if (!endPtr || *endPtr != '\0' || errno == ERANGE) return false;
    outVal = neg ? -val : val;
    return true;
}

static Value scanner_nextInt(int argCount, Value* args) {
    auto* inst = getScannerInstance(argCount);
    int radix = (argCount >= 1 && args[0].isInt()) ? static_cast<int>(*args[0].asInt()) : 10;
    std::string token;
    if (!fetchNextToken(inst, true, token)) {
        s_vm->jitRuntimeError("Scanner.nextInt: No more tokens (EOF).");
        return Value();
    }
    int64_t val = 0;
    if (!parseIntegerToken(token, radix, val)) {
        s_vm->jitRuntimeError("Scanner.nextInt: Token '" + token + "' cannot be parsed as an integer.");
        return Value();
    }
    return Value(val);
}

static Value scanner_nextDouble(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    if (!fetchNextToken(inst, true, token)) {
        s_vm->jitRuntimeError("Scanner.nextDouble: No more tokens (EOF).");
        return Value();
    }
    char* endPtr = nullptr;
    double val = std::strtod(token.c_str(), &endPtr);
    if (!endPtr || endPtr == token.c_str() || *endPtr != '\0') {
        s_vm->jitRuntimeError("Scanner.nextDouble: Token '" + token + "' cannot be parsed as a double.");
        return Value();
    }
    return Value(val);
}

static Value scanner_nextBool(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    if (!fetchNextToken(inst, true, token)) {
        s_vm->jitRuntimeError("Scanner.nextBool: No more tokens (EOF).");
        return Value();
    }
    std::string lower = token;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "y") return Value(true);
    if (lower == "false" || lower == "0" || lower == "no" || lower == "n") return Value(false);
    s_vm->jitRuntimeError("Scanner.nextBool: Token '" + token + "' cannot be parsed as a boolean.");
    return Value();
}

static Value scanner_hasNext(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    bool has = fetchNextToken(inst, false, token);
    return Value(has);
}

static Value scanner_hasNextLine(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string line;
    bool has = fetchNextLine(inst, false, line);
    return Value(has);
}

static Value scanner_hasNextInt(int argCount, Value* args) {
    auto* inst = getScannerInstance(argCount);
    int radix = (argCount >= 1 && args[0].isInt()) ? static_cast<int>(*args[0].asInt()) : 10;
    std::string token;
    if (!fetchNextToken(inst, false, token)) return Value(false);
    int64_t val = 0;
    return Value(parseIntegerToken(token, radix, val));
}

static Value scanner_hasNextDouble(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    if (!fetchNextToken(inst, false, token)) return Value(false);
    char* endPtr = nullptr;
    std::strtod(token.c_str(), &endPtr);
    return Value(bool(endPtr && endPtr != token.c_str() && *endPtr == '\0'));
}

static Value scanner_hasNextBool(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::string token;
    if (!fetchNextToken(inst, false, token)) return Value(false);
    std::string lower = token;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool isB = (lower == "true" || lower == "false" || lower == "1" || lower == "0" || lower == "yes" || lower == "no" || lower == "y" || lower == "n");
    return Value(isB);
}

static Value scanner_useDelimiter(int argCount, Value* args) {
    auto* inst = getScannerInstance(argCount);
    if (inst && argCount >= 1) {
        std::string delim = valToStr(args[0]);
        GcHandle dHandle = s_vm->getHeap().allocateString(std::move(delim));
        inst->fields[2] = Value(dHandle);
    }
    return s_vm->peek(argCount); // return this
}

static Value scanner_getDelimiter(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    if (!inst || inst->fields.size() < 3) return Value();
    return inst->fields[2];
}

static Value scanner_reset(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    if (inst && inst->fields.size() >= 2) {
        inst->fields[1] = Value(int64_t(0));
    }
    return Value();
}

static Value scanner_allTokens(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::vector<Value> tokens;
    std::string token;
    while (fetchNextToken(inst, true, token)) {
        GcHandle h = s_vm->getHeap().allocateString(token);
        tokens.push_back(Value(h));
    }
    return Value(s_vm->getHeap().allocateArray(std::move(tokens)));
}

static Value scanner_allLines(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    std::vector<Value> lines;
    std::string line;
    while (fetchNextLine(inst, true, line)) {
        GcHandle h = s_vm->getHeap().allocateString(line);
        lines.push_back(Value(h));
    }
    return Value(s_vm->getHeap().allocateArray(std::move(lines)));
}

static Value scanner_close(int argCount, Value*) {
    auto* inst = getScannerInstance(argCount);
    if (inst && inst->fields.size() >= 5) {
        inst->fields[4] = Value(true);
    }
    return Value();
}

// 靜態工廠與便利方法
static Value scanner_static_fromString(int argCount, Value* args) {
    if (argCount < 1 || !s_vm || s_scannerClassHandle.id == 0) return Value();
    GcHandle instHandle = s_vm->getHeap().allocate<ObjInstance>(s_scannerClassHandle);
    auto* inst = s_vm->getHeap().getAs<ObjInstance>(instHandle);
    if (!inst) return Value();
    ensureScannerFields(inst, false, valToStr(args[0]));
    return Value(instHandle);
}

static Value scanner_static_fromStdin(int, Value*) {
    if (!s_vm || s_scannerClassHandle.id == 0) return Value();
    GcHandle instHandle = s_vm->getHeap().allocate<ObjInstance>(s_scannerClassHandle);
    auto* inst = s_vm->getHeap().getAs<ObjInstance>(instHandle);
    if (!inst) return Value();
    ensureScannerFields(inst, true, "");
    return Value(instHandle);
}

static Value scanner_static_scanInt(int argCount, Value* args) {
    if (argCount >= 1 && !args[0].isNull()) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }
    int64_t val = 0;
    if (std::cin >> val) {
        return Value(val);
    }
    std::cin.clear();
    return Value();
}

static Value scanner_static_scanDouble(int argCount, Value* args) {
    if (argCount >= 1 && !args[0].isNull()) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }
    double val = 0.0;
    if (std::cin >> val) {
        return Value(val);
    }
    std::cin.clear();
    return Value();
}

static Value scanner_static_scanWord(int argCount, Value* args) {
    if (argCount >= 1 && !args[0].isNull()) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }
    std::string word;
    if (std::cin >> word) {
        return Value(s_vm->getHeap().allocateString(std::move(word)));
    }
    std::cin.clear();
    return Value();
}

static Value scanner_static_scanLine(int argCount, Value* args) {
    if (argCount >= 1 && !args[0].isNull()) {
        std::cout << valToStr(args[0]);
        std::cout.flush();
    }
    std::string line;
    if (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return Value(s_vm->getHeap().allocateString(std::move(line)));
    }
    std::cin.clear();
    return Value();
}

EASYSCRIPT_MODULE_INIT {
    s_vm = vm;
    enableVirtualTerminal();

    // 1. 註冊根層級基本輸出輸入 (Root Basic I/O)
    vm->defineModuleNative(mod, "write", io_write, -1);
    vm->defineModuleNative(mod, "writeln", io_writeln, -1);
    vm->defineModuleNative(mod, "writeError", io_writeError, -1);
    vm->defineModuleNative(mod, "writeErrorln", io_writeErrorln, -1);
    vm->defineModuleNative(mod, "readLine", io_readLine, 0);
    vm->defineModuleNative(mod, "readAll", io_readAll, 0);
    vm->defineModuleNative(mod, "readChar", io_readChar, 0);
    vm->defineModuleNative(mod, "flush", io_flush, 0);
    vm->defineModuleNative(mod, "flushError", io_flushError, 0);
    vm->defineModuleNative(mod, "setEncoding", term_setEncoding, -1);
    vm->defineModuleNative(mod, "getEncoding", term_getEncoding, 0);

    // 2. 封裝 Terminal 類別
    GcHandle termClassHandle = vm->getHeap().allocate<ObjClass>("Terminal");
    auto* termClass = vm->getHeap().getAs<ObjClass>(termClassHandle);
    defineClassNative(termClass, "clear", term_clear, 0);
    defineClassNative(termClass, "clearLine", term_clearLine, 0);
    defineClassNative(termClass, "clearToEnd", term_clearToEnd, 0);
    defineClassNative(termClass, "setCursor", term_setCursor, -1);
    defineClassNative(termClass, "getCursor", term_getCursor, 0);
    defineClassNative(termClass, "moveUp", term_moveUp, -1);
    defineClassNative(termClass, "moveDown", term_moveDown, -1);
    defineClassNative(termClass, "moveRight", term_moveRight, -1);
    defineClassNative(termClass, "moveLeft", term_moveLeft, -1);
    defineClassNative(termClass, "hideCursor", term_hideCursor, 0);
    defineClassNative(termClass, "showCursor", term_showCursor, 0);
    defineClassNative(termClass, "getWidth", term_getWidth, 0);
    defineClassNative(termClass, "getHeight", term_getHeight, 0);
    defineClassNative(termClass, "getSize", term_getSize, 0);
    defineClassNative(termClass, "isTerminal", term_isTerminal, 0);
    defineClassNative(termClass, "beep", term_beep, 0);
    defineClassNative(termClass, "setEncoding", term_setEncoding, -1);
    defineClassNative(termClass, "getEncoding", term_getEncoding, 0);
    defineClassNative(termClass, "setCodePage", term_setCodePage, -1);
    defineClassNative(termClass, "getCodePage", term_getCodePage, 0);
    vm->defineModuleValue(mod, "Terminal", Value(termClassHandle));

    // 3. 封裝 Color 類別
    GcHandle colorClassHandle = vm->getHeap().allocate<ObjClass>("Color");
    auto* colorClass = vm->getHeap().getAs<ObjClass>(colorClassHandle);
    defineClassNative(colorClass, "red", color_red, 1);
    defineClassNative(colorClass, "green", color_green, 1);
    defineClassNative(colorClass, "yellow", color_yellow, 1);
    defineClassNative(colorClass, "blue", color_blue, 1);
    defineClassNative(colorClass, "magenta", color_magenta, 1);
    defineClassNative(colorClass, "cyan", color_cyan, 1);
    defineClassNative(colorClass, "white", color_white, 1);
    defineClassNative(colorClass, "gray", color_gray, 1);

    defineClassNative(colorClass, "bgRed", color_bgRed, 1);
    defineClassNative(colorClass, "bgGreen", color_bgGreen, 1);
    defineClassNative(colorClass, "bgYellow", color_bgYellow, 1);
    defineClassNative(colorClass, "bgBlue", color_bgBlue, 1);
    defineClassNative(colorClass, "bgMagenta", color_bgMagenta, 1);
    defineClassNative(colorClass, "bgCyan", color_bgCyan, 1);
    defineClassNative(colorClass, "bgWhite", color_bgWhite, 1);
    defineClassNative(colorClass, "bgGray", color_bgGray, 1);

    defineClassNative(colorClass, "bold", color_bold, 1);
    defineClassNative(colorClass, "dim", color_dim, 1);
    defineClassNative(colorClass, "italic", color_italic, 1);
    defineClassNative(colorClass, "underline", color_underline, 1);
    defineClassNative(colorClass, "inverse", color_inverse, 1);
    defineClassNative(colorClass, "rgb", color_rgb, 4);
    defineClassNative(colorClass, "bgRgb", color_bgRgb, 4);
    defineClassNative(colorClass, "strip", color_strip, 1);

    // 顏色字串常數
    defineClassValue(colorClass, "RESET", strVal("\033[0m"));
    defineClassValue(colorClass, "BOLD", strVal("\033[1m"));
    defineClassValue(colorClass, "DIM", strVal("\033[2m"));
    defineClassValue(colorClass, "ITALIC", strVal("\033[3m"));
    defineClassValue(colorClass, "UNDERLINE", strVal("\033[4m"));
    defineClassValue(colorClass, "INVERSE", strVal("\033[7m"));

    defineClassValue(colorClass, "RED", strVal("\033[31m"));
    defineClassValue(colorClass, "GREEN", strVal("\033[32m"));
    defineClassValue(colorClass, "YELLOW", strVal("\033[33m"));
    defineClassValue(colorClass, "BLUE", strVal("\033[34m"));
    defineClassValue(colorClass, "MAGENTA", strVal("\033[35m"));
    defineClassValue(colorClass, "CYAN", strVal("\033[36m"));
    defineClassValue(colorClass, "WHITE", strVal("\033[37m"));
    defineClassValue(colorClass, "GRAY", strVal("\033[90m"));

    defineClassValue(colorClass, "BG_RED", strVal("\033[41m"));
    defineClassValue(colorClass, "BG_GREEN", strVal("\033[42m"));
    defineClassValue(colorClass, "BG_YELLOW", strVal("\033[43m"));
    defineClassValue(colorClass, "BG_BLUE", strVal("\033[44m"));
    defineClassValue(colorClass, "BG_MAGENTA", strVal("\033[45m"));
    defineClassValue(colorClass, "BG_CYAN", strVal("\033[46m"));
    defineClassValue(colorClass, "BG_WHITE", strVal("\033[47m"));
    defineClassValue(colorClass, "BG_GRAY", strVal("\033[100m"));
    vm->defineModuleValue(mod, "Color", Value(colorClassHandle));

    // 4. 封裝 Prompt 類別
    GcHandle promptClassHandle = vm->getHeap().allocate<ObjClass>("Prompt");
    auto* promptClass = vm->getHeap().getAs<ObjClass>(promptClassHandle);
    defineClassNative(promptClass, "ask", prompt_ask, -1);
    defineClassNative(promptClass, "confirm", prompt_confirm, -1);
    defineClassNative(promptClass, "password", prompt_password, -1);
    defineClassNative(promptClass, "choose", prompt_choose, 2);
    vm->defineModuleValue(mod, "Prompt", Value(promptClassHandle));

        // 5. 封裝 Scanner 類別
    s_scannerClassHandle = vm->getHeap().allocate<ObjClass>("Scanner");
    auto* scannerClass = vm->getHeap().getAs<ObjClass>(s_scannerClassHandle);

    defineInstanceNative(scannerClass, "init", scanner_init, -1);
    defineInstanceNative(scannerClass, "next", scanner_next, 0);
    defineInstanceNative(scannerClass, "nextLine", scanner_nextLine, 0);
    defineInstanceNative(scannerClass, "nextInt", scanner_nextInt, -1);
    defineInstanceNative(scannerClass, "nextDouble", scanner_nextDouble, 0);
    defineInstanceNative(scannerClass, "nextFloat", scanner_nextDouble, 0);
    defineInstanceNative(scannerClass, "nextBool", scanner_nextBool, 0);
    defineInstanceNative(scannerClass, "hasNext", scanner_hasNext, 0);
    defineInstanceNative(scannerClass, "hasNextLine", scanner_hasNextLine, 0);
    defineInstanceNative(scannerClass, "hasNextInt", scanner_hasNextInt, -1);
    defineInstanceNative(scannerClass, "hasNextDouble", scanner_hasNextDouble, 0);
    defineInstanceNative(scannerClass, "hasNextBool", scanner_hasNextBool, 0);
    defineInstanceNative(scannerClass, "useDelimiter", scanner_useDelimiter, 1);
    defineInstanceNative(scannerClass, "getDelimiter", scanner_getDelimiter, 0);
    defineInstanceNative(scannerClass, "reset", scanner_reset, 0);
    defineInstanceNative(scannerClass, "allTokens", scanner_allTokens, 0);
    defineInstanceNative(scannerClass, "allLines", scanner_allLines, 0);
    defineInstanceNative(scannerClass, "close", scanner_close, 0);

    defineClassNative(scannerClass, "fromString", scanner_static_fromString, 1);
    defineClassNative(scannerClass, "fromStdin", scanner_static_fromStdin, 0);
    defineClassNative(scannerClass, "scanInt", scanner_static_scanInt, -1);
    defineClassNative(scannerClass, "scanDouble", scanner_static_scanDouble, -1);
    defineClassNative(scannerClass, "scanWord", scanner_static_scanWord, -1);
    defineClassNative(scannerClass, "scanLine", scanner_static_scanLine, -1);

    vm->defineModuleValue(mod, "Scanner", Value(s_scannerClassHandle));

    return true;
}
