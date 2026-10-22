#include "lsp/lsp_server.hpp"
#include "compiler.hpp"
#include "lexer.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

namespace lsp {

LspServer::LspServer() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

void LspServer::run() {
    std::string headerLine;
    while (isRunning && std::cin.good()) {
        int contentLength = 0;
        while (std::getline(std::cin, headerLine)) {
            if (!headerLine.empty() && headerLine.back() == '\r') {
                headerLine.pop_back();
            }
            if (headerLine.empty()) {
                break;
            }
            if (headerLine.rfind("Content-Length:", 0) == 0) {
                try {
                    contentLength = std::stoi(headerLine.substr(15));
                } catch (...) {
                    contentLength = 0;
                }
            }
        }

        if (contentLength <= 0) {
            continue;
        }

        std::string body(contentLength, '\0');
        std::cin.read(&body[0], contentLength);
        if (!std::cin.good()) {
            break;
        }

        processMessage(body);
    }
}

void LspServer::sendJson(const Json& json) {
    std::string body = json.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void LspServer::sendResponse(const Json& id, const Json& result) {
    Json res = Json::object();
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    res["result"] = result;
    sendJson(res);
}

void LspServer::sendError(const Json& id, int code, const std::string& message) {
    Json res = Json::object();
    res["jsonrpc"] = "2.0";
    res["id"] = id;
    Json err = Json::object();
    err["code"] = code;
    err["message"] = message;
    res["error"] = err;
    sendJson(res);
}

void LspServer::processMessage(const std::string& rawJson) {
    Json msg = JsonParser::parse(rawJson);
    if (!msg.isObject()) return;

    if (msg.contains("id")) {
        handleRequest(msg);
    } else {
        handleNotification(msg);
    }
}

void LspServer::handleRequest(const Json& msg) {
    const Json& id = msg["id"];
    std::string method = msg["method"].asString();
    const Json& params = msg["params"];

    if (method == "initialize") {
        Json result = Json::object();
        Json caps = Json::object();

        // 1 = Full sync
        caps["textDocumentSync"] = 1;

        // Completion
        Json comp = Json::object();
        comp["resolveProvider"] = false;
        Json trig = Json::array();
        trig.push_back(".");
        trig.push_back("\"");
        trig.push_back("'");
        comp["triggerCharacters"] = trig;
        caps["completionProvider"] = comp;

        // Hover, DocumentSymbol, Definition
        caps["hoverProvider"] = true;
        caps["documentSymbolProvider"] = true;
        caps["definitionProvider"] = true;

        // Semantic Tokens (語意上色)
        Json semTokens = Json::object();
        Json legend = Json::object();
        Json tokenTypes = Json::array();
        tokenTypes.push_back("type");       // 0
        tokenTypes.push_back("class");      // 1
        tokenTypes.push_back("function");   // 2
        tokenTypes.push_back("variable");   // 3
        tokenTypes.push_back("parameter");  // 4
        tokenTypes.push_back("property");   // 5
        legend["tokenTypes"] = tokenTypes;

        Json tokenModifiers = Json::array();
        tokenModifiers.push_back("declaration");    // 1 << 0 = 1
        tokenModifiers.push_back("readonly");       // 1 << 1 = 2
        tokenModifiers.push_back("defaultLibrary"); // 1 << 2 = 4
        legend["tokenModifiers"] = tokenModifiers;

        semTokens["legend"] = legend;
        semTokens["full"] = true;
        caps["semanticTokensProvider"] = semTokens;

        result["capabilities"] = caps;

        Json serverInfo = Json::object();
        serverInfo["name"] = "easyscript-lsp";
        serverInfo["version"] = "0.1.0";
        result["serverInfo"] = serverInfo;

        sendResponse(id, result);
    } else if (method == "shutdown") {
        sendResponse(id, Json());
    } else if (method == "textDocument/completion") {
        std::string uri = params["textDocument"]["uri"].asString();
        int line = static_cast<int>(params["position"]["line"].asInt());
        int character = static_cast<int>(params["position"]["character"].asInt());
        sendResponse(id, handleCompletion(uri, line, character));
    } else if (method == "textDocument/hover") {
        std::string uri = params["textDocument"]["uri"].asString();
        int line = static_cast<int>(params["position"]["line"].asInt());
        int character = static_cast<int>(params["position"]["character"].asInt());
        sendResponse(id, handleHover(uri, line, character));
    } else if (method == "textDocument/documentSymbol") {
        std::string uri = params["textDocument"]["uri"].asString();
        sendResponse(id, handleDocumentSymbol(uri));
    } else if (method == "textDocument/definition") {
        std::string uri = params["textDocument"]["uri"].asString();
        int line = static_cast<int>(params["position"]["line"].asInt());
        int character = static_cast<int>(params["position"]["character"].asInt());
        sendResponse(id, handleDefinition(uri, line, character));
    } else if (method == "textDocument/semanticTokens/full") {
        std::string uri = params["textDocument"]["uri"].asString();
        sendResponse(id, handleSemanticTokens(uri));
    } else {
        sendError(id, -32601, "Method not found: " + method);
    }
}

void LspServer::handleNotification(const Json& msg) {
    std::string method = msg["method"].asString();
    const Json& params = msg["params"];

    if (method == "exit") {
        isRunning = false;
    } else if (method == "textDocument/didOpen") {
        std::string uri = params["textDocument"]["uri"].asString();
        std::string text = params["textDocument"]["text"].asString();
        documents[uri] = text;
        validateDocument(uri, text);
    } else if (method == "textDocument/didChange") {
        std::string uri = params["textDocument"]["uri"].asString();
        const Json& changes = params["contentChanges"];
        if (changes.isArray() && !changes.arrVal.empty()) {
            std::string text = changes[0]["text"].asString();
            documents[uri] = text;
            validateDocument(uri, text);
        }
    } else if (method == "textDocument/didClose") {
        std::string uri = params["textDocument"]["uri"].asString();
        documents.erase(uri);

        // 清除該文件的診斷
        Json notification = Json::object();
        notification["jsonrpc"] = "2.0";
        notification["method"] = "textDocument/publishDiagnostics";
        Json p = Json::object();
        p["uri"] = uri;
        p["diagnostics"] = Json::array();
        notification["params"] = p;
        sendJson(notification);
    }
}

// -----------------------------------------------------------------------------
// 即時語法診斷 (Diagnostics)
// -----------------------------------------------------------------------------

void LspServer::validateDocument(const std::string& uri, const std::string& text) {
    Heap heap;
    Compiler compiler(heap, text);
    std::vector<CompilerDiagnostic> diagnostics;
    compiler.setDiagnosticSink(&diagnostics);
    compiler.compile();

    Json diagList = Json::array();
    for (const auto& d : diagnostics) {
        Json diag = Json::object();
        Json range = Json::object();
        Json start = Json::object();
        start["line"] = std::max(0, d.line - 1);
        start["character"] = std::max(0, d.column - 1);
        Json end = Json::object();
        end["line"] = std::max(0, d.line - 1);
        end["character"] = std::max(0, d.column - 1 + std::max(1, d.length));
        range["start"] = start;
        range["end"] = end;

        diag["range"] = range;
        diag["severity"] = d.severity;
        diag["source"] = "easyscript";
        diag["message"] = d.message;
        diagList.push_back(diag);
    }

    Json notification = Json::object();
    notification["jsonrpc"] = "2.0";
    notification["method"] = "textDocument/publishDiagnostics";
    Json params = Json::object();
    params["uri"] = uri;
    params["diagnostics"] = diagList;
    notification["params"] = params;

    sendJson(notification);
}

// -----------------------------------------------------------------------------
// 輔助函式：提取行列中的前綴字串
// -----------------------------------------------------------------------------

static std::string getLinePrefix(const std::string& text, int line, int character) {
    std::istringstream stream(text);
    std::string curLine;
    int cur = 0;
    while (std::getline(stream, curLine)) {
        if (!curLine.empty() && curLine.back() == '\r') curLine.pop_back();
        if (cur == line) {
            if (character >= 0 && character <= static_cast<int>(curLine.size())) {
                return curLine.substr(0, character);
            }
            return curLine;
        }
        cur++;
    }
    return "";
}

static std::string getWordAtPosition(const std::string& text, int line, int character) {
    std::istringstream stream(text);
    std::string curLine;
    int cur = 0;
    while (std::getline(stream, curLine)) {
        if (!curLine.empty() && curLine.back() == '\r') curLine.pop_back();
        if (cur == line) {
            if (character < 0 || character > static_cast<int>(curLine.size())) return "";
            int start = character;
            while (start > 0 && (std::isalnum(static_cast<unsigned char>(curLine[start - 1])) || curLine[start - 1] == '_' || curLine[start - 1] == '$')) {
                start--;
            }
            int end = character;
            while (end < static_cast<int>(curLine.size()) && (std::isalnum(static_cast<unsigned char>(curLine[end])) || curLine[end] == '_' || curLine[end] == '$')) {
                end++;
            }
            return curLine.substr(start, end - start);
        }
        cur++;
    }
    return "";
}

// -----------------------------------------------------------------------------
// 代碼自動補全 (Completion)
// -----------------------------------------------------------------------------

static void addCompletion(Json& list, const std::string& label, int kind, const std::string& detail, const std::string& doc = "") {
    Json item = Json::object();
    item["label"] = label;
    item["kind"] = kind; // 14=Keyword, 3=Function, 7=Class, 9=Module, 6=Variable
    item["detail"] = detail;
    if (!doc.empty()) {
        Json d = Json::object();
        d["kind"] = "markdown";
        d["value"] = doc;
        item["documentation"] = d;
    }
    list.push_back(item);
}

Json LspServer::handleCompletion(const std::string& uri, int line, int character) {
    auto it = documents.find(uri);
    std::string text = (it != documents.end()) ? it->second : "";
    std::string prefix = getLinePrefix(text, line, character);

    Json items = Json::array();

    // 1. 模組/類別點運算子提示
    if (prefix.ends_with("io.") || (prefix.find("io.") != std::string::npos && prefix.back() == '.')) {
        addCompletion(items, "write", 2, "write(...args): void", "Prints arguments to stdout without automatic newline.");
        addCompletion(items, "writeln", 2, "writeln(...args): void", "Prints arguments to stdout followed by newline.");
        addCompletion(items, "writeError", 2, "writeError(...args): void", "Prints arguments to stderr without newline.");
        addCompletion(items, "writeErrorln", 2, "writeErrorln(...args): void", "Prints arguments to stderr followed by newline.");
        addCompletion(items, "readLine", 2, "readLine(): string", "Reads a line from stdin (stripping trailing \\r\\n).");
        addCompletion(items, "readAll", 2, "readAll(): string", "Reads entire standard input until EOF.");
        addCompletion(items, "readChar", 2, "readChar(): string", "Reads a single unbuffered keystroke immediately.");
        addCompletion(items, "flush", 2, "flush(): void", "Flushes the stdout buffer.");
        addCompletion(items, "flushError", 2, "flushError(): void", "Flushes the stderr buffer.");
        addCompletion(items, "setEncoding", 2, "setEncoding(encoding: string | int, setInput = true): bool", "Sets terminal character encoding (e.g. 'utf-8', 'big5', 'gbk').");
        addCompletion(items, "getEncoding", 2, "getEncoding(): string", "Gets current terminal character encoding name.");
        addCompletion(items, "Terminal", 7, "class Terminal", "Terminal screen and cursor manipulation class.");
        addCompletion(items, "Color", 7, "class Color", "ANSI terminal colors and text styles class.");
        addCompletion(items, "Prompt", 7, "class Prompt", "Interactive CLI user dialog and prompt class.");
        return items;
    }

    if (prefix.ends_with("Color.") || (prefix.find("Color.") != std::string::npos && prefix.back() == '.')) {
        const char* colors[] = {"red", "green", "yellow", "blue", "magenta", "cyan", "white", "gray"};
        for (const char* c : colors) {
            addCompletion(items, c, 2, std::string(c) + "(text: string): string", "Wraps text with ANSI foreground color.");
        }
        const char* bgColors[] = {"bgRed", "bgGreen", "bgYellow", "bgBlue", "bgMagenta", "bgCyan", "bgWhite", "bgGray"};
        for (const char* c : bgColors) {
            addCompletion(items, c, 2, std::string(c) + "(text: string): string", "Wraps text with ANSI background color.");
        }
        const char* styles[] = {"bold", "dim", "italic", "underline", "inverse"};
        for (const char* s : styles) {
            addCompletion(items, s, 2, std::string(s) + "(text: string): string", "Wraps text with ANSI style.");
        }
        addCompletion(items, "rgb", 2, "rgb(r, g, b, text): string", "24-bit TrueColor foreground wrapper.");
        addCompletion(items, "bgRgb", 2, "bgRgb(r, g, b, text): string", "24-bit TrueColor background wrapper.");
        addCompletion(items, "strip", 2, "strip(text: string): string", "Removes all ANSI escape sequences from string.");
        addCompletion(items, "RED", 6, "const RED: string", "ANSI escape code for Red.");
        addCompletion(items, "GREEN", 6, "const GREEN: string", "ANSI escape code for Green.");
        addCompletion(items, "RESET", 6, "const RESET: string", "ANSI escape code for Reset.");
        return items;
    }

    if (prefix.ends_with("Terminal.") || (prefix.find("Terminal.") != std::string::npos && prefix.back() == '.')) {
        addCompletion(items, "clear", 2, "clear(): void", "Clears the terminal screen and moves cursor to (1, 1).");
        addCompletion(items, "clearLine", 2, "clearLine(): void", "Clears current line and returns cursor to beginning.");
        addCompletion(items, "clearToEnd", 2, "clearToEnd(): void", "Clears from cursor to the end of the screen.");
        addCompletion(items, "setCursor", 2, "setCursor(x, y): void", "Sets terminal cursor position (1-based).");
        addCompletion(items, "getCursor", 2, "getCursor(): Map {x, y}", "Gets current cursor position coordinates.");
        addCompletion(items, "moveUp", 2, "moveUp(n = 1): void", "Moves cursor up by n rows.");
        addCompletion(items, "moveDown", 2, "moveDown(n = 1): void", "Moves cursor down by n rows.");
        addCompletion(items, "moveLeft", 2, "moveLeft(n = 1): void", "Moves cursor left by n columns.");
        addCompletion(items, "moveRight", 2, "moveRight(n = 1): void", "Moves cursor right by n columns.");
        addCompletion(items, "hideCursor", 2, "hideCursor(): void", "Hides the terminal cursor.");
        addCompletion(items, "showCursor", 2, "showCursor(): void", "Shows the terminal cursor.");
        addCompletion(items, "getWidth", 2, "getWidth(): int", "Gets terminal column width.");
        addCompletion(items, "getHeight", 2, "getHeight(): int", "Gets terminal row height.");
        addCompletion(items, "getSize", 2, "getSize(): Map {width, height}", "Gets terminal dimensions.");
        addCompletion(items, "isTerminal", 2, "isTerminal(): bool", "Returns true if running in interactive terminal.");
        addCompletion(items, "beep", 2, "beep(): void", "Emits console audio bell alert.");
        addCompletion(items, "setEncoding", 2, "setEncoding(encoding, setInput = true): bool", "Sets terminal encoding.");
        addCompletion(items, "getEncoding", 2, "getEncoding(): string", "Gets current terminal encoding name.");
        addCompletion(items, "setCodePage", 2, "setCodePage(cp: int, setInput = true): bool", "Sets Windows CodePage.");
        addCompletion(items, "getCodePage", 2, "getCodePage(): int", "Gets current Windows CodePage integer.");
        return items;
    }

    if (prefix.ends_with("Prompt.") || (prefix.find("Prompt.") != std::string::npos && prefix.back() == '.')) {
        addCompletion(items, "ask", 2, "ask(question, defaultVal = ''): string", "Interactive user input prompt with default value.");
        addCompletion(items, "confirm", 2, "confirm(question, defaultVal = false): bool", "Interactive [y/N] confirmation prompt.");
        addCompletion(items, "password", 2, "password(question, maskChar = '*'): string", "Secure hidden/masked password input.");
        addCompletion(items, "choose", 2, "choose(question, options): int", "Interactive numbered list chooser.");
        return items;
    }

    if (prefix.ends_with("Math.") || (prefix.find("Math.") != std::string::npos && prefix.back() == '.')) {
        addCompletion(items, "PI", 6, "const PI: double = 3.14159...", "Mathematical constant Pi.");
        addCompletion(items, "E", 6, "const E: double = 2.71828...", "Mathematical constant Euler's number.");
        addCompletion(items, "sqrt", 2, "sqrt(x: double): double", "Square root of x.");
        addCompletion(items, "pow", 2, "pow(base, exp): double", "Calculates base raised to power exp.");
        addCompletion(items, "abs", 2, "abs(x): number", "Absolute value of x.");
        addCompletion(items, "floor", 2, "floor(x: double): int", "Rounds down to nearest integer.");
        addCompletion(items, "ceil", 2, "ceil(x: double): int", "Rounds up to nearest integer.");
        addCompletion(items, "round", 2, "round(x: double): int", "Rounds to nearest integer.");
        addCompletion(items, "min", 2, "min(a, b): number", "Returns smaller of a and b.");
        addCompletion(items, "max", 2, "max(a, b): number", "Returns larger of a and b.");
        addCompletion(items, "clamp", 2, "clamp(val, min, max): number", "Clamps value between min and max.");
        addCompletion(items, "random", 2, "random(): double", "Random floating-point in [0, 1).");
        addCompletion(items, "randomInt", 2, "randomInt(min, max): int", "Random integer between min and max inclusive.");
        return items;
    }

    // 2. 模組路徑補全 (import "...")
    if (prefix.find("import \"") != std::string::npos || prefix.find("from \"") != std::string::npos) {
        addCompletion(items, "std/io", 9, "module std/io", "Standard I/O, terminal colors, and cursor control.");
        addCompletion(items, "std/fs", 9, "module std/fs", "Filesystem reading, writing, and directory operations.");
        addCompletion(items, "std/path", 9, "module std/path", "Path manipulation, normalization, and joining.");
        addCompletion(items, "std/os", 9, "module std/os", "Operating system environment, platform, and processes.");
        addCompletion(items, "std/math", 9, "module std/math", "Mathematical functions, trigonometry, and constants.");
        return items;
    }

    // 3. 通用關鍵字補全
    const char* keywords[] = {
        "fn", "function", "class", "extends", "let", "const", "var",
        "if", "else", "while", "for", "in", "break", "continue", "return",
        "try", "catch", "finally", "throw", "import", "export", "from",
        "new", "this", "super", "true", "false", "null", "nil"
    };
    for (const char* kw : keywords) {
        addCompletion(items, kw, 14, "keyword", "");
    }

    // 4. 內建函式與標準命名空間
    addCompletion(items, "print", 3, "print(value: any): void", "Prints value to stdout with newline.");
    addCompletion(items, "println", 3, "println(value: any): void", "Prints value to stdout with newline.");
    addCompletion(items, "assert", 3, "assert(condition: bool, message = ''): void", "Asserts condition is true.");
    addCompletion(items, "Terminal", 7, "class Terminal", "Standard terminal manipulation class.");
    addCompletion(items, "Color", 7, "class Color", "Standard ANSI terminal color class.");
    addCompletion(items, "Prompt", 7, "class Prompt", "Standard CLI prompt dialog class.");
    addCompletion(items, "Math", 7, "module Math", "Standard math module.");

    // 5. 掃描當前文檔的所有識別字
    Lexer lexer(text);
    std::vector<std::string> seen;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.type == TokenType::TOKEN_EOF) break;
        if (tok.type == TokenType::IDENTIFIER) {
            std::string idStr(tok.lexeme);
            if (idStr.size() >= 2 && std::find(seen.begin(), seen.end(), idStr) == seen.end()) {
                seen.push_back(idStr);
                addCompletion(items, idStr, 6, "identifier");
            }
        }
    }

    return items;
}

// -----------------------------------------------------------------------------
// 懸停資訊 (Hover Tooltips)
// -----------------------------------------------------------------------------

Json LspServer::handleHover(const std::string& uri, int line, int character) {
    auto it = documents.find(uri);
    std::string text = (it != documents.end()) ? it->second : "";
    std::string word = getWordAtPosition(text, line, character);
    if (word.empty()) return Json();

    std::string doc;
    if (word == "fn" || word == "function") {
        doc = "**fn** (keyword)\n\nDeclares a function in EasyScript.\n\n```easyscript\nfn add(a, b) {\n    return a + b;\n}\n```";
    } else if (word == "class") {
        doc = "**class** (keyword)\n\nDeclares an object-oriented class with fields and methods.\n\n```easyscript\nclass Greeter {\n    greet(name) {\n        print('Hello ' + name);\n    }\n}\n```";
    } else if (word == "print" || word == "println") {
        doc = "**print(value: any): void**\n\nBuilt-in function to print formatted string to standard output.";
    } else if (word == "write") {
        doc = "**io.write(...args): void**\n\nWrites arguments directly to standard output without automatic newline.";
    } else if (word == "writeln") {
        doc = "**io.writeln(...args): void**\n\nWrites arguments to standard output followed by a newline.";
    } else if (word == "readLine") {
        doc = "**io.readLine(): string**\n\nReads a line from standard input, stripping trailing newline characters.";
    } else if (word == "Terminal") {
        doc = "**class Terminal** (`std/io`)\n\nProvides cursor position control, screen clearing, and terminal metrics.";
    } else if (word == "Color") {
        doc = "**class Color** (`std/io`)\n\nProvides ANSI text styling, foreground/background colors, and RGB formatting.";
    } else if (word == "Prompt") {
        doc = "**class Prompt** (`std/io`)\n\nInteractive CLI prompt utilities for user input, confirmation, and password masking.";
    } else if (word == "setEncoding") {
        doc = "**Terminal.setEncoding(encoding: string | int, setInput = true): bool**\n\nSets output and input character encoding (e.g. `'utf-8'`, `'big5'`, `'gbk'` or CodePage integer like `65001`).";
    } else if (word == "getEncoding") {
        doc = "**Terminal.getEncoding(): string**\n\nReturns current terminal character encoding name.";
    } else if (word == "Math") {
        doc = "**module Math** (`std/math`)\n\nStandard mathematical functions and constants (`PI`, `E`, `sqrt`, `pow`, `random`).";
    }

    if (doc.empty()) return Json();

    Json result = Json::object();
    Json contents = Json::object();
    contents["kind"] = "markdown";
    contents["value"] = doc;
    result["contents"] = contents;
    return result;
}

// -----------------------------------------------------------------------------
// 文件符號大綱 (Document Symbols)
// -----------------------------------------------------------------------------

Json LspServer::handleDocumentSymbol(const std::string& uri) {
    auto it = documents.find(uri);
    std::string text = (it != documents.end()) ? it->second : "";

    Json symbols = Json::array();
    Lexer lexer(text);
    Token prev;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.type == TokenType::TOKEN_EOF) break;

        if (tok.type == TokenType::IDENTIFIER) {
            int kind = 0;
            if (prev.type == TokenType::FN) {
                kind = 12; // Function
            } else if (prev.type == TokenType::CLASS) {
                kind = 5; // Class
            } else if (prev.type == TokenType::LET || prev.type == TokenType::CONST) {
                kind = 13; // Variable
            }

            if (kind != 0) {
                Json sym = Json::object();
                sym["name"] = std::string(tok.lexeme);
                sym["kind"] = kind;

                Json range = Json::object();
                Json start = Json::object();
                start["line"] = std::max(0, tok.line - 1);
                start["character"] = std::max(0, tok.column - 1);
                Json end = Json::object();
                end["line"] = std::max(0, tok.line - 1);
                end["character"] = std::max(0, tok.column - 1 + static_cast<int>(tok.lexeme.size()));
                range["start"] = start;
                range["end"] = end;

                sym["range"] = range;
                sym["selectionRange"] = range;
                symbols.push_back(sym);
            }
        }
        prev = tok;
    }

    return symbols;
}

// -----------------------------------------------------------------------------
// 跳轉至定義 (Go to Definition)
// -----------------------------------------------------------------------------

Json LspServer::handleDefinition(const std::string& uri, int line, int character) {
    auto it = documents.find(uri);
    std::string text = (it != documents.end()) ? it->second : "";
    std::string word = getWordAtPosition(text, line, character);
    if (word.empty()) return Json();

    Lexer lexer(text);
    Token prev;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.type == TokenType::TOKEN_EOF) break;

        if (tok.type == TokenType::IDENTIFIER && tok.lexeme == word) {
            if (prev.type == TokenType::FN || prev.type == TokenType::CLASS ||
                prev.type == TokenType::LET || prev.type == TokenType::CONST) {
                Json loc = Json::object();
                loc["uri"] = uri;
                Json range = Json::object();
                Json start = Json::object();
                start["line"] = std::max(0, tok.line - 1);
                start["character"] = std::max(0, tok.column - 1);
                Json end = Json::object();
                end["line"] = std::max(0, tok.line - 1);
                end["character"] = std::max(0, tok.column - 1 + static_cast<int>(tok.lexeme.size()));
                range["start"] = start;
                range["end"] = end;
                loc["range"] = range;
                return loc;
            }
        }
        prev = tok;
    }

    return Json();
}

// -----------------------------------------------------------------------------
// 語意上色標記 (Semantic Tokens Highlighting)
// -----------------------------------------------------------------------------

struct RawTokenInfo {
    int line;
    int character;
    int length;
    int tokenType;
    int tokenModifiers;
};

Json LspServer::handleSemanticTokens(const std::string& uri) {
    auto it = documents.find(uri);
    std::string text = (it != documents.end()) ? it->second : "";

    std::vector<RawTokenInfo> rawTokens;
    Lexer lexer(text);
    std::vector<Token> allTokens;
    while (true) {
        Token tok = lexer.nextToken();
        allTokens.push_back(tok);
        if (tok.type == TokenType::TOKEN_EOF) break;
    }

    for (size_t i = 0; i < allTokens.size(); ++i) {
        const Token& tok = allTokens[i];
        if (tok.type == TokenType::TOKEN_EOF) break;

        Token prevTok = (i > 0) ? allTokens[i - 1] : Token();
        Token nextTok = (i + 1 < allTokens.size()) ? allTokens[i + 1] : Token();

        // 1. 基礎型別 (type: 0)
        if (tok.type == TokenType::TYPE_INT || tok.type == TokenType::TYPE_DOUBLE ||
            tok.type == TokenType::TYPE_BOOL || tok.type == TokenType::TYPE_STRING ||
            tok.type == TokenType::TYPE_MAP || tok.type == TokenType::TYPE_VOID) {
            rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 0, 0});
        } else if (tok.type == TokenType::IDENTIFIER) {
            if (tok.lexeme == "any" || tok.lexeme == "number" || tok.lexeme == "Array" || tok.lexeme == "Function") {
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 0, 0});
            } else if (tok.lexeme == "Terminal" || tok.lexeme == "Color" || tok.lexeme == "Prompt" || tok.lexeme == "Math") {
                // 標準庫類別/模組 (class: 1, defaultLibrary: 4)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 1, 4});
            } else if (prevTok.type == TokenType::FN) {
                // 函式宣告 (function: 2, declaration: 1)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 2, 1});
            } else if (prevTok.type == TokenType::CLASS) {
                // 類別宣告 (class: 1, declaration: 1)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 1, 1});
            } else if (prevTok.type == TokenType::LET) {
                // 變數宣告 (variable: 3, declaration: 1)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 3, 1});
            } else if (prevTok.type == TokenType::CONST) {
                // 常數宣告 (variable: 3, declaration | readonly: 3)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 3, 3});
            } else if (nextTok.type == TokenType::LEFT_PAREN) {
                // 函式調用 (function: 2)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 2, 0});
            } else if (prevTok.type == TokenType::DOT) {
                // 物件屬性 (property: 5)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 5, 0});
            } else {
                // 一般變數存取 (variable: 3)
                rawTokens.push_back({tok.line - 1, tok.column - 1, static_cast<int>(tok.lexeme.size()), 3, 0});
            }
        }
    }

    // 依行與列遞增排序
    std::sort(rawTokens.begin(), rawTokens.end(), [](const RawTokenInfo& a, const RawTokenInfo& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.character < b.character;
    });

    // 轉換為 LSP 差量五元組 [deltaLine, deltaStartChar, length, tokenType, tokenModifiers]
    Json data = Json::array();
    int prevLine = 0;
    int prevChar = 0;
    for (const auto& item : rawTokens) {
        int deltaLine = item.line - prevLine;
        int deltaChar = (deltaLine == 0) ? (item.character - prevChar) : item.character;

        data.push_back(Json(deltaLine));
        data.push_back(Json(deltaChar));
        data.push_back(Json(item.length));
        data.push_back(Json(item.tokenType));
        data.push_back(Json(item.tokenModifiers));

        prevLine = item.line;
        prevChar = item.character;
    }

    Json result = Json::object();
    result["data"] = data;
    return result;
}

} // namespace lsp
