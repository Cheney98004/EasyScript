#include "lsp/lsp_server.hpp"
#include "compiler.hpp"
#include "lexer.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

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
// URI 與路徑轉換輔助
// -----------------------------------------------------------------------------

static std::string uriToPath(const std::string& uri) {
    std::string path = uri;
    if (path.starts_with("file:///")) {
        path = path.substr(8);
    } else if (path.starts_with("file://")) {
        path = path.substr(7);
    }
    size_t colonPos = path.find("%3A");
    if (colonPos != std::string::npos) path.replace(colonPos, 3, ":");
    colonPos = path.find("%3a");
    if (colonPos != std::string::npos) path.replace(colonPos, 3, ":");
    size_t spacePos = 0;
    while ((spacePos = path.find("%20", spacePos)) != std::string::npos) {
        path.replace(spacePos, 3, " ");
        spacePos += 1;
    }
    return path;
}

static std::string pathToUri(const std::string& path) {
    std::string normal = path;
    std::replace(normal.begin(), normal.end(), '\\', '/');
    if (!normal.empty() && normal[0] != '/') {
        return "file:///" + normal;
    }
    return "file://" + normal;
}

// -----------------------------------------------------------------------------
// 輔助函式：文字提取、註解解析與字詞內容分析
// -----------------------------------------------------------------------------

static std::string extractDocComment(const std::string& text, int targetLine0Based) {
    std::istringstream stream(text);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (targetLine0Based <= 0 || targetLine0Based >= static_cast<int>(lines.size())) {
        return "";
    }
    int cur = targetLine0Based - 1;
    std::vector<std::string> commentLines;
    while (cur >= 0) {
        std::string l = lines[cur];
        size_t firstNonBlank = l.find_first_not_of(" \t");
        if (firstNonBlank == std::string::npos) break;
        if (l.compare(firstNonBlank, 2, "//") == 0) {
            std::string content = l.substr(firstNonBlank + 2);
            if (!content.empty() && content.front() == ' ') content.erase(0, 1);
            commentLines.push_back(content);
            cur--;
        } else if (l.compare(firstNonBlank, 2, "*/") == 0) {
            commentLines.push_back(l.substr(firstNonBlank));
            cur--;
            while (cur >= 0) {
                std::string ml = lines[cur];
                size_t fb = ml.find_first_not_of(" \t");
                if (fb != std::string::npos && ml.compare(fb, 2, "/*") == 0) {
                    commentLines.push_back(ml.substr(fb));
                    break;
                }
                commentLines.push_back(ml);
                cur--;
            }
            break;
        } else {
            break;
        }
    }
    if (commentLines.empty()) return "";
    std::reverse(commentLines.begin(), commentLines.end());
    std::string result;
    for (const auto& cl : commentLines) {
        if (!result.empty()) result += "\n";
        result += cl;
    }
    return result;
}

static std::string getLineText(const std::string& text, int line) {
    std::istringstream stream(text);
    std::string curLine;
    int cur = 0;
    while (std::getline(stream, curLine)) {
        if (!curLine.empty() && curLine.back() == '\r') curLine.pop_back();
        if (cur == line) return curLine;
        cur++;
    }
    return "";
}

static std::string getLinePrefix(const std::string& text, int line, int character) {
    std::string curLine = getLineText(text, line);
    if (character >= 0 && character <= static_cast<int>(curLine.size())) {
        return curLine.substr(0, character);
    }
    return curLine;
}

struct WordContext {
    std::string word;
    int startCol{0};
    int endCol{0};
    std::string parentObject;
};

static WordContext getWordContext(const std::string& text, int line, int character) {
    WordContext ctx;
    std::string lineStr = getLineText(text, line);
    if (lineStr.empty() || character < 0 || character > static_cast<int>(lineStr.size())) {
        return ctx;
    }

    int start = character;
    if (start > 0 && start == static_cast<int>(lineStr.size())) {
        start--;
    }
    if (start > 0 && !(std::isalnum(static_cast<unsigned char>(lineStr[start])) || lineStr[start] == '_' || lineStr[start] == '$')) {
        if (std::isalnum(static_cast<unsigned char>(lineStr[start - 1])) || lineStr[start - 1] == '_' || lineStr[start - 1] == '$') {
            start--;
        }
    }

    if (start < static_cast<int>(lineStr.size()) && (std::isalnum(static_cast<unsigned char>(lineStr[start])) || lineStr[start] == '_' || lineStr[start] == '$')) {
        while (start > 0 && (std::isalnum(static_cast<unsigned char>(lineStr[start - 1])) || lineStr[start - 1] == '_' || lineStr[start - 1] == '$')) {
            start--;
        }
        int end = start;
        while (end < static_cast<int>(lineStr.size()) && (std::isalnum(static_cast<unsigned char>(lineStr[end])) || lineStr[end] == '_' || lineStr[end] == '$')) {
            end++;
        }
        ctx.word = lineStr.substr(start, end - start);
        ctx.startCol = start;
        ctx.endCol = end;

        int p = start - 1;
        while (p >= 0 && (lineStr[p] == ' ' || lineStr[p] == '\t')) p--;
        if (p >= 0 && lineStr[p] == '.') {
            p--;
            while (p >= 0 && (lineStr[p] == ' ' || lineStr[p] == '\t')) p--;
            int objEnd = p + 1;
            while (p >= 0 && (std::isalnum(static_cast<unsigned char>(lineStr[p])) || lineStr[p] == '_' || lineStr[p] == '$')) {
                p--;
            }
            int objStart = p + 1;
            if (objEnd > objStart) {
                ctx.parentObject = lineStr.substr(objStart, objEnd - objStart);
            }
        }
    }

    return ctx;
}

// -----------------------------------------------------------------------------
// 模組解析與匯出符號提取 (Module Resolution & Symbol Extraction)
// -----------------------------------------------------------------------------

std::vector<ImportInfo> LspServer::parseDocumentImports(const std::string& text) {
    std::vector<ImportInfo> imports;
    Lexer lexer(text);
    Token tok = lexer.nextToken();

    while (tok.type != TokenType::TOKEN_EOF) {
        if (tok.type == TokenType::IMPORT) {
            Token next = lexer.nextToken();
            if (next.type == TokenType::STRING) {
                // import "path";
                std::string pathStr = std::string(next.lexeme);
                if (pathStr.size() >= 2 && pathStr.front() == '"' && pathStr.back() == '"') {
                    pathStr = pathStr.substr(1, pathStr.size() - 2);
                }
                std::string alias = pathStr;
                size_t slash = alias.find_last_of("/\\");
                if (slash != std::string::npos) alias = alias.substr(slash + 1);
                if (alias.ends_with(".easy") || alias.ends_with(".esc")) {
                    alias = alias.substr(0, alias.find_last_of('.'));
                }
                ImportInfo info;
                info.modulePath = pathStr;
                info.alias = alias;
                info.line = next.line - 1;
                info.pathColStart = next.column - 1;
                info.pathColEnd = next.column - 1 + static_cast<int>(next.lexeme.size());
                imports.push_back(std::move(info));
            } else if (next.type == TokenType::LEFT_BRACE) {
                // import { a, b, c } from "path";
                std::vector<std::string> symbols;
                Token symTok = lexer.nextToken();
                while (symTok.type != TokenType::RIGHT_BRACE && symTok.type != TokenType::TOKEN_EOF) {
                    if (symTok.type == TokenType::IDENTIFIER) {
                        symbols.push_back(std::string(symTok.lexeme));
                    }
                    symTok = lexer.nextToken();
                }
                Token fromTok = lexer.nextToken();
                if (fromTok.type == TokenType::FROM) {
                    Token pathTok = lexer.nextToken();
                    if (pathTok.type == TokenType::STRING) {
                        std::string pathStr = std::string(pathTok.lexeme);
                        if (pathStr.size() >= 2 && pathStr.front() == '"' && pathStr.back() == '"') {
                            pathStr = pathStr.substr(1, pathStr.size() - 2);
                        }
                        std::string alias = pathStr;
                        size_t slash = alias.find_last_of("/\\");
                        if (slash != std::string::npos) alias = alias.substr(slash + 1);
                        if (alias.ends_with(".easy") || alias.ends_with(".esc")) {
                            alias = alias.substr(0, alias.find_last_of('.'));
                        }
                        ImportInfo info;
                        info.modulePath = pathStr;
                        info.alias = alias;
                        info.namedSymbols = std::move(symbols);
                        info.line = pathTok.line - 1;
                        info.pathColStart = pathTok.column - 1;
                        info.pathColEnd = pathTok.column - 1 + static_cast<int>(pathTok.lexeme.size());
                        imports.push_back(std::move(info));
                    }
                }
            } else if (next.type == TokenType::IDENTIFIER) {
                // import alias from "path";
                std::string alias(next.lexeme);
                Token fromTok = lexer.nextToken();
                if (fromTok.type == TokenType::FROM) {
                    Token pathTok = lexer.nextToken();
                    if (pathTok.type == TokenType::STRING) {
                        std::string pathStr = std::string(pathTok.lexeme);
                        if (pathStr.size() >= 2 && pathStr.front() == '"' && pathStr.back() == '"') {
                            pathStr = pathStr.substr(1, pathStr.size() - 2);
                        }
                        ImportInfo info;
                        info.modulePath = pathStr;
                        info.alias = alias;
                        info.line = pathTok.line - 1;
                        info.pathColStart = pathTok.column - 1;
                        info.pathColEnd = pathTok.column - 1 + static_cast<int>(pathTok.lexeme.size());
                        imports.push_back(std::move(info));
                    }
                }
            }
        }
        tok = lexer.nextToken();
    }
    return imports;
}

ResolvedModule LspServer::resolveModule(const std::string& currentDocUri, const std::string& importPath) {
    ResolvedModule mod;
    mod.path = importPath;

    // 1. 標準庫模組判定
    if (importPath == "std/io") {
        mod.isStandard = true;
        mod.symbols.push_back({"write", 3, 0, 0, 5, "write(...args): void", "Prints arguments to stdout without automatic newline."});
        mod.symbols.push_back({"writeln", 3, 0, 0, 7, "writeln(...args): void", "Prints arguments to stdout followed by newline."});
        mod.symbols.push_back({"writeError", 3, 0, 0, 10, "writeError(...args): void", "Prints arguments to stderr without newline."});
        mod.symbols.push_back({"writeErrorln", 3, 0, 0, 12, "writeErrorln(...args): void", "Prints arguments to stderr followed by newline."});
        mod.symbols.push_back({"readLine", 3, 0, 0, 8, "readLine(): string", "Reads a line from stdin (stripping trailing \\r\\n)."});
        mod.symbols.push_back({"readAll", 3, 0, 0, 7, "readAll(): string", "Reads entire standard input until EOF."});
        mod.symbols.push_back({"readChar", 3, 0, 0, 8, "readChar(): string", "Reads a single unbuffered keystroke immediately."});
        mod.symbols.push_back({"flush", 3, 0, 0, 5, "flush(): void", "Flushes stdout buffer."});
        mod.symbols.push_back({"flushError", 3, 0, 0, 10, "flushError(): void", "Flushes stderr buffer."});
        mod.symbols.push_back({"setEncoding", 3, 0, 0, 11, "setEncoding(encoding, setInput = true): bool", "Sets console output and input character encoding."});
        mod.symbols.push_back({"getEncoding", 3, 0, 0, 11, "getEncoding(): string", "Gets current console output character encoding name."});
        mod.symbols.push_back({"Terminal", 7, 0, 0, 8, "class Terminal", "Terminal screen and cursor manipulation class."});
        mod.symbols.push_back({"Color", 7, 0, 0, 5, "class Color", "ANSI terminal colors and text styles class."});
        mod.symbols.push_back({"Prompt", 7, 0, 0, 6, "class Prompt", "Interactive CLI prompt dialog class."});
        return mod;
    } else if (importPath == "std/fs") {
        mod.isStandard = true;
        mod.symbols.push_back({"readText", 3, 0, 0, 8, "readText(path: string): string", "Reads entire file as UTF-8 text."});
        mod.symbols.push_back({"writeText", 3, 0, 0, 9, "writeText(path, text): bool", "Overwrites file with text."});
        mod.symbols.push_back({"appendText", 3, 0, 0, 10, "appendText(path, text): bool", "Appends text to file."});
        mod.symbols.push_back({"exists", 3, 0, 0, 6, "exists(path: string): bool", "Checks whether file or directory exists."});
        mod.symbols.push_back({"remove", 3, 0, 0, 6, "remove(path: string): bool", "Deletes single file or empty directory."});
        mod.symbols.push_back({"removeTree", 3, 0, 0, 10, "removeTree(path: string): bool", "Recursively deletes directory and all contents."});
        mod.symbols.push_back({"makeDir", 3, 0, 0, 7, "makeDir(path: string): bool", "Creates directories along path."});
        mod.symbols.push_back({"listDir", 3, 0, 0, 7, "listDir(path: string): Array<string>", "Lists filenames in directory."});
        mod.symbols.push_back({"stat", 3, 0, 0, 4, "stat(path: string): Map", "Retrieves file metadata map."});
        return mod;
    } else if (importPath == "std/path") {
        mod.isStandard = true;
        mod.symbols.push_back({"join", 3, 0, 0, 4, "join(...parts): string", "Joins path segments with normalized separators."});
        mod.symbols.push_back({"dirname", 3, 0, 0, 7, "dirname(path): string", "Gets directory name of path."});
        mod.symbols.push_back({"basename", 3, 0, 0, 8, "basename(path): string", "Gets filename or last segment of path."});
        mod.symbols.push_back({"extname", 3, 0, 0, 7, "extname(path): string", "Gets extension of path including leading dot."});
        mod.symbols.push_back({"isAbsolute", 3, 0, 0, 10, "isAbsolute(path): bool", "Checks if path is absolute."});
        mod.symbols.push_back({"resolve", 3, 0, 0, 7, "resolve(...paths): string", "Resolves absolute canonical path."});
        mod.symbols.push_back({"normalize", 3, 0, 0, 9, "normalize(path): string", "Normalizes path separators."});
        return mod;
    } else if (importPath == "std/os") {
        mod.isStandard = true;
        mod.symbols.push_back({"platform", 3, 0, 0, 8, "platform(): string", "Returns host OS ('windows', 'linux', 'macos')."});
        mod.symbols.push_back({"arch", 3, 0, 0, 4, "arch(): string", "Returns architecture ('x86_64', 'arm64')."});
        mod.symbols.push_back({"getEnv", 3, 0, 0, 6, "getEnv(name: string): string | nil", "Gets environment variable value."});
        mod.symbols.push_back({"setEnv", 3, 0, 0, 6, "setEnv(name, val): bool", "Sets environment variable value."});
        mod.symbols.push_back({"cwd", 3, 0, 0, 3, "cwd(): string", "Gets current working directory."});
        mod.symbols.push_back({"chdir", 3, 0, 0, 5, "chdir(path): bool", "Changes current working directory."});
        mod.symbols.push_back({"exec", 3, 0, 0, 4, "exec(command: string): int", "Executes shell command and returns exit code."});
        mod.symbols.push_back({"cpuCount", 3, 0, 0, 8, "cpuCount(): int", "Returns hardware concurrency CPU core count."});
        mod.symbols.push_back({"exit", 3, 0, 0, 4, "exit(code: int): void", "Terminates the process."});
        return mod;
    } else if (importPath == "std/math") {
        mod.isStandard = true;
        mod.symbols.push_back({"PI", 21, 0, 0, 2, "const PI: double = 3.14159...", "Mathematical constant Pi."});
        mod.symbols.push_back({"E", 21, 0, 0, 1, "const E: double = 2.71828...", "Mathematical constant E."});
        mod.symbols.push_back({"sqrt", 3, 0, 0, 4, "sqrt(x: double): double", "Square root."});
        mod.symbols.push_back({"pow", 3, 0, 0, 3, "pow(base, exp): double", "Power function."});
        mod.symbols.push_back({"abs", 3, 0, 0, 3, "abs(x): number", "Absolute value."});
        mod.symbols.push_back({"floor", 3, 0, 0, 5, "floor(x: double): int", "Floor."});
        mod.symbols.push_back({"ceil", 3, 0, 0, 4, "ceil(x: double): int", "Ceiling."});
        mod.symbols.push_back({"round", 3, 0, 0, 5, "round(x: double): int", "Round."});
        mod.symbols.push_back({"min", 3, 0, 0, 3, "min(a, b): number", "Minimum."});
        mod.symbols.push_back({"max", 3, 0, 0, 3, "max(a, b): number", "Maximum."});
        mod.symbols.push_back({"clamp", 3, 0, 0, 5, "clamp(val, min, max): number", "Clamp."});
        mod.symbols.push_back({"random", 3, 0, 0, 6, "random(): double", "Random [0, 1)."});
        mod.symbols.push_back({"randomInt", 3, 0, 0, 9, "randomInt(min, max): int", "Random integer."});
        return mod;
    }

    // 2. 使用者腳本檔案解析 (.easy)
    std::string currentPath = uriToPath(currentDocUri);
    fs::path currentDir = fs::path(currentPath).parent_path();
    fs::path targetPath = currentDir / importPath;

    fs::path normPath = targetPath.lexically_normal();
    fs::path normPathWithExt = (targetPath.string().ends_with(".easy") ? normPath : (targetPath.parent_path() / (targetPath.filename().string() + ".easy")).lexically_normal());

    std::string candidateUri = pathToUri(normPath.string());
    std::string candidateUriWithExt = pathToUri(normPathWithExt.string());

    // 先檢查記憶體中開啟的文檔 (未存檔也能即時解析跨模組提示)
    if (documents.find(candidateUri) != documents.end()) {
        mod.targetUri = candidateUri;
    } else if (documents.find(candidateUriWithExt) != documents.end()) {
        mod.targetUri = candidateUriWithExt;
    }

    std::string resolvedFile;
    try {
        if (fs::exists(targetPath) && !fs::is_directory(targetPath)) {
            resolvedFile = fs::canonical(targetPath).string();
        } else if (fs::exists(targetPath.string() + ".easy")) {
            resolvedFile = fs::canonical(targetPath.string() + ".easy").string();
        } else {
            // 嘗試相對工作目錄
            fs::path cwdTarget = fs::current_path() / importPath;
            if (fs::exists(cwdTarget) && !fs::is_directory(cwdTarget)) {
                resolvedFile = fs::canonical(cwdTarget).string();
            } else if (fs::exists(cwdTarget.string() + ".easy")) {
                resolvedFile = fs::canonical(cwdTarget.string() + ".easy").string();
            }
        }
    } catch (...) {}

    if (mod.targetUri.empty() && !resolvedFile.empty()) {
        mod.targetUri = pathToUri(resolvedFile);
    }

    if (mod.targetUri.empty()) {
        return mod;
    }

    // 取得模組原始碼 (優先使用記憶體編輯器緩衝區，次之使用磁碟檔案)
    std::string sourceCode;
    auto docIt = documents.find(mod.targetUri);
    if (docIt != documents.end()) {
        sourceCode = docIt->second;
    } else if (!resolvedFile.empty()) {
        std::ifstream file(resolvedFile);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            sourceCode = ss.str();
        }
    }

    if (sourceCode.empty()) {
        return mod;
    }

    // 解析模組中的匯出與定義
    Lexer lexer(sourceCode);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.nextToken();
        tokens.push_back(tok);
        if (tok.type == TokenType::TOKEN_EOF) break;
    }

    bool hasExplicitExports = false;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::EXPORT) {
            hasExplicitExports = true;
            break;
        }
    }

    int braceDepth = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const Token& tok = tokens[i];
        if (tok.type == TokenType::TOKEN_EOF) break;

        if (tok.type == TokenType::LEFT_BRACE) {
            braceDepth++;
            continue;
        }
        if (tok.type == TokenType::RIGHT_BRACE) {
            if (braceDepth > 0) braceDepth--;
            continue;
        }

        bool isExported = false;
        size_t declIdx = i;

        if (hasExplicitExports) {
            if (tok.type == TokenType::EXPORT && i + 1 < tokens.size()) {
                isExported = true;
                declIdx = i + 1;
            }
        } else {
            // 沒有顯式 export 時，匯出所有頂層宣告 (braceDepth == 0)
            if (braceDepth == 0 && (tok.type == TokenType::FN || tok.type == TokenType::CLASS ||
                                     tok.type == TokenType::CONST || tok.type == TokenType::LET)) {
                isExported = true;
                declIdx = i;
            }
        }

        if (!isExported) continue;

        const Token& declTok = tokens[declIdx];
        if (declTok.type == TokenType::FN && declIdx + 1 < tokens.size() && tokens[declIdx + 1].type == TokenType::IDENTIFIER) {
            const Token& nameTok = tokens[declIdx + 1];
            size_t p = declIdx + 2;
            std::string sig = "fn " + std::string(nameTok.lexeme);
            if (p < tokens.size() && tokens[p].type == TokenType::LEFT_PAREN) {
                sig += "(";
                p++;
                while (p < tokens.size() && tokens[p].type != TokenType::RIGHT_PAREN && tokens[p].type != TokenType::TOKEN_EOF) {
                    if (tokens[p].type == TokenType::COMMA) {
                        sig += ", ";
                    } else if (tokens[p].type == TokenType::COLON) {
                        sig += ": ";
                    } else if (tokens[p].type == TokenType::EQUAL) {
                        sig += " = ";
                    } else {
                        if (!sig.empty() && sig.back() != '(' && sig.back() != ' ') {
                            sig += " ";
                        }
                        sig += std::string(tokens[p].lexeme);
                    }
                    p++;
                }
                sig += ")";
                if (p < tokens.size() && tokens[p].type == TokenType::RIGHT_PAREN) p++;
                if (p < tokens.size() && tokens[p].type == TokenType::COLON) {
                    sig += ": ";
                    p++;
                    while (p < tokens.size() && tokens[p].type != TokenType::LEFT_BRACE && tokens[p].type != TokenType::SEMICOLON && tokens[p].type != TokenType::TOKEN_EOF) {
                        sig += std::string(tokens[p].lexeme);
                        p++;
                    }
                }
            }

            ExportedSymbol sym;
            sym.name = std::string(nameTok.lexeme);
            sym.kind = 3; // Function
            sym.line = nameTok.line - 1;
            sym.column = nameTok.column - 1;
            sym.length = static_cast<int>(nameTok.lexeme.size());
            sym.signature = sig;
            sym.doc = extractDocComment(sourceCode, tok.line - 1);
            sym.sourceUri = mod.targetUri;
            mod.symbols.push_back(std::move(sym));
            i = declIdx;
        } else if (declTok.type == TokenType::CLASS && declIdx + 1 < tokens.size() && tokens[declIdx + 1].type == TokenType::IDENTIFIER) {
            const Token& nameTok = tokens[declIdx + 1];
            ExportedSymbol sym;
            sym.name = std::string(nameTok.lexeme);
            sym.kind = 7; // Class
            sym.line = nameTok.line - 1;
            sym.column = nameTok.column - 1;
            sym.length = static_cast<int>(nameTok.lexeme.size());
            sym.signature = "class " + std::string(nameTok.lexeme);
            sym.doc = extractDocComment(sourceCode, tok.line - 1);
            sym.sourceUri = mod.targetUri;
            mod.symbols.push_back(std::move(sym));
            i = declIdx;
        } else if ((declTok.type == TokenType::LET || declTok.type == TokenType::CONST) && declIdx + 1 < tokens.size() && tokens[declIdx + 1].type == TokenType::IDENTIFIER) {
            const Token& nameTok = tokens[declIdx + 1];
            ExportedSymbol sym;
            sym.name = std::string(nameTok.lexeme);
            sym.kind = (declTok.type == TokenType::CONST) ? 21 : 6; // Constant / Variable
            sym.line = nameTok.line - 1;
            sym.column = nameTok.column - 1;
            sym.length = static_cast<int>(nameTok.lexeme.size());
            sym.signature = (declTok.type == TokenType::CONST ? "const " : "let ") + std::string(nameTok.lexeme);
            sym.doc = extractDocComment(sourceCode, tok.line - 1);
            sym.sourceUri = mod.targetUri;
            mod.symbols.push_back(std::move(sym));
            i = declIdx;
        }
    }

    return mod;
}

// -----------------------------------------------------------------------------
// 代碼自動補全 (Completion)
// -----------------------------------------------------------------------------

static void addCompletion(Json& list, const std::string& label, int kind, const std::string& detail, const std::string& doc = "") {
    Json item = Json::object();
    item["label"] = label;
    item["kind"] = kind;
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
    std::string lineStr = getLineText(text, line);

    Json items = Json::array();
    std::vector<ImportInfo> docImports = parseDocumentImports(text);

    // 1. 模組路徑補全 (import "..." 或 from "...")
    if (prefix.find("import \"") != std::string::npos || prefix.find("from \"") != std::string::npos ||
        prefix.find("import '") != std::string::npos || prefix.find("from '") != std::string::npos) {
        addCompletion(items, "std/io", 9, "module std/io", "Standard I/O, terminal colors, and cursor control.");
        addCompletion(items, "std/fs", 9, "module std/fs", "Filesystem reading, writing, and directory operations.");
        addCompletion(items, "std/path", 9, "module std/path", "Path manipulation, normalization, and joining.");
        addCompletion(items, "std/os", 9, "module std/os", "Operating system environment, platform, and processes.");
        addCompletion(items, "std/math", 9, "module std/math", "Mathematical functions, trigonometry, and constants.");

        // 搜尋當前檔案同層目錄下的 .easy 與 .esc 模組
        std::string currentPath = uriToPath(uri);
        fs::path currentDir = fs::path(currentPath).parent_path();
        try {
            if (fs::exists(currentDir) && fs::is_directory(currentDir)) {
                for (const auto& entry : fs::directory_iterator(currentDir)) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".easy" || ext == ".esc") {
                            std::string filename = entry.path().filename().string();
                            std::string stem = entry.path().stem().string();
                            if (entry.path() != fs::path(currentPath)) {
                                addCompletion(items, "./" + filename, 17, "module " + filename, "Local EasyScript module file");
                                addCompletion(items, "./" + stem, 17, "module " + stem, "Local EasyScript module");
                            }
                        }
                    }
                }
            }
        } catch (...) {}

        return items;
    }

    // 2. 解構匯入提示: import { | } from "module"
    size_t importPos = lineStr.find("import");
    size_t braceStart = lineStr.find('{');
    size_t fromPos = lineStr.find("from");
    if (importPos != std::string::npos && braceStart != std::string::npos && character > static_cast<int>(braceStart)) {
        size_t braceEnd = lineStr.find('}', braceStart);
        if (braceEnd == std::string::npos || character <= static_cast<int>(braceEnd)) {
            if (fromPos != std::string::npos && fromPos > braceStart) {
                size_t q1 = lineStr.find_first_of("\"'", fromPos);
                if (q1 != std::string::npos) {
                    char quote = lineStr[q1];
                    size_t q2 = lineStr.find(quote, q1 + 1);
                    if (q2 != std::string::npos) {
                        std::string modPath = lineStr.substr(q1 + 1, q2 - q1 - 1);
                        ResolvedModule mod = resolveModule(uri, modPath);
                        for (const auto& sym : mod.symbols) {
                            addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                        }
                        return items;
                    }
                }
            }
        }
    }

    // 3. 模組別名或物件點運算子提示 (<alias>.)
    size_t lastDot = prefix.rfind('.');
    if (lastDot != std::string::npos && lastDot + 1 == prefix.size()) {
        size_t idStart = lastDot;
        while (idStart > 0 && (std::isalnum(static_cast<unsigned char>(prefix[idStart - 1])) || prefix[idStart - 1] == '_' || prefix[idStart - 1] == '$')) {
            idStart--;
        }
        std::string objName = prefix.substr(idStart, lastDot - idStart);
        if (!objName.empty()) {
            // 先檢查是否為文檔中匯入的模組別名
            for (const auto& imp : docImports) {
                if (imp.alias == objName) {
                    ResolvedModule mod = resolveModule(uri, imp.modulePath);
                    for (const auto& sym : mod.symbols) {
                        addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                    }
                    return items;
                }
            }

            // 檢查常見內建模組
            if (objName == "io") {
                ResolvedModule mod = resolveModule(uri, "std/io");
                for (const auto& sym : mod.symbols) addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                return items;
            } else if (objName == "fs") {
                ResolvedModule mod = resolveModule(uri, "std/fs");
                for (const auto& sym : mod.symbols) addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                return items;
            } else if (objName == "path") {
                ResolvedModule mod = resolveModule(uri, "std/path");
                for (const auto& sym : mod.symbols) addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                return items;
            } else if (objName == "os") {
                ResolvedModule mod = resolveModule(uri, "std/os");
                for (const auto& sym : mod.symbols) addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                return items;
            } else if (objName == "Math" || objName == "math") {
                ResolvedModule mod = resolveModule(uri, "std/math");
                for (const auto& sym : mod.symbols) addCompletion(items, sym.name, sym.kind, sym.signature, sym.doc);
                return items;
            } else if (objName == "Color") {
                const char* colors[] = {"red", "green", "yellow", "blue", "magenta", "cyan", "white", "gray"};
                for (const char* c : colors) addCompletion(items, c, 3, std::string(c) + "(text: string): string", "ANSI foreground color.");
                const char* bgColors[] = {"bgRed", "bgGreen", "bgYellow", "bgBlue", "bgMagenta", "bgCyan", "bgWhite", "bgGray"};
                for (const char* c : bgColors) addCompletion(items, c, 3, std::string(c) + "(text: string): string", "ANSI background color.");
                const char* styles[] = {"bold", "dim", "italic", "underline", "inverse"};
                for (const char* s : styles) addCompletion(items, s, 3, std::string(s) + "(text: string): string", "ANSI style.");
                addCompletion(items, "rgb", 3, "rgb(r, g, b, text): string", "24-bit TrueColor foreground.");
                addCompletion(items, "bgRgb", 3, "bgRgb(r, g, b, text): string", "24-bit TrueColor background.");
                addCompletion(items, "strip", 3, "strip(text: string): string", "Removes ANSI sequences.");
                addCompletion(items, "RED", 21, "const RED: string", "ANSI Red code.");
                addCompletion(items, "GREEN", 21, "const GREEN: string", "ANSI Green code.");
                addCompletion(items, "RESET", 21, "const RESET: string", "ANSI Reset code.");
                return items;
            } else if (objName == "Terminal") {
                addCompletion(items, "clear", 3, "clear(): void", "Clears terminal screen.");
                addCompletion(items, "clearLine", 3, "clearLine(): void", "Clears current line.");
                addCompletion(items, "clearToEnd", 3, "clearToEnd(): void", "Clears to end of screen.");
                addCompletion(items, "setCursor", 3, "setCursor(x, y): void", "Sets cursor position (1-based).");
                addCompletion(items, "getCursor", 3, "getCursor(): Map {x, y}", "Gets cursor position.");
                addCompletion(items, "moveUp", 3, "moveUp(n = 1): void", "Moves cursor up.");
                addCompletion(items, "moveDown", 3, "moveDown(n = 1): void", "Moves cursor down.");
                addCompletion(items, "moveLeft", 3, "moveLeft(n = 1): void", "Moves cursor left.");
                addCompletion(items, "moveRight", 3, "moveRight(n = 1): void", "Moves cursor right.");
                addCompletion(items, "hideCursor", 3, "hideCursor(): void", "Hides cursor.");
                addCompletion(items, "showCursor", 3, "showCursor(): void", "Shows cursor.");
                addCompletion(items, "getWidth", 3, "getWidth(): int", "Gets terminal width.");
                addCompletion(items, "getHeight", 3, "getHeight(): int", "Gets terminal height.");
                addCompletion(items, "getSize", 3, "getSize(): Map {width, height}", "Gets terminal size.");
                addCompletion(items, "isTerminal", 3, "isTerminal(): bool", "Checks if interactive terminal.");
                addCompletion(items, "beep", 3, "beep(): void", "Emits console bell alert.");
                addCompletion(items, "setEncoding", 3, "setEncoding(encoding, setInput = true): bool", "Sets console encoding.");
                addCompletion(items, "getEncoding", 3, "getEncoding(): string", "Gets console encoding.");
                addCompletion(items, "setCodePage", 3, "setCodePage(cp, setInput = true): bool", "Sets Windows CodePage.");
                addCompletion(items, "getCodePage", 3, "getCodePage(): int", "Gets Windows CodePage.");
                return items;
            } else if (objName == "Prompt") {
                addCompletion(items, "ask", 3, "ask(question, defaultVal = ''): string", "Interactive user prompt.");
                addCompletion(items, "confirm", 3, "confirm(question, defaultVal = false): bool", "Interactive [y/N] prompt.");
                addCompletion(items, "password", 3, "password(question, maskChar = '*'): string", "Hidden password prompt.");
                addCompletion(items, "choose", 3, "choose(question, options): int", "Option chooser dialog.");
                return items;
            }
        }
    }

    // 4. 通用關鍵字補全
    const char* keywords[] = {
        "fn", "function", "class", "extends", "let", "const", "var",
        "if", "else", "while", "for", "in", "break", "continue", "return",
        "try", "catch", "finally", "throw", "import", "export", "from",
        "new", "this", "super", "true", "false", "null", "nil"
    };
    for (const char* kw : keywords) {
        addCompletion(items, kw, 14, "keyword", "");
    }

    // 5. 內建函式與標準命名空間
    addCompletion(items, "print", 3, "print(value: any): void", "Prints value to stdout with newline.");
    addCompletion(items, "println", 3, "println(value: any): void", "Prints value to stdout with newline.");
    addCompletion(items, "assert", 3, "assert(condition: bool, message = ''): void", "Asserts condition is true.");
    addCompletion(items, "Terminal", 7, "class Terminal", "Standard terminal manipulation class.");
    addCompletion(items, "Color", 7, "class Color", "Standard ANSI terminal color class.");
    addCompletion(items, "Prompt", 7, "class Prompt", "Standard CLI prompt dialog class.");
    addCompletion(items, "Math", 7, "module Math", "Standard math module.");

    // 6. 加入所有已匯入的別名與具名符號
    for (const auto& imp : docImports) {
        if (!imp.alias.empty()) {
            addCompletion(items, imp.alias, 9, "module " + imp.alias, "Imported from " + imp.modulePath);
        }
        for (const auto& sym : imp.namedSymbols) {
            addCompletion(items, sym, 3, sym, "Imported from " + imp.modulePath);
        }
    }

    // 7. 掃描當前文檔的所有識別字
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
    if (text.empty()) return Json();

    std::vector<ImportInfo> docImports = parseDocumentImports(text);

    // 1. 檢查游標是否停在 import 路徑字串上
    for (const auto& imp : docImports) {
        if (imp.line == line && character >= imp.pathColStart && character <= imp.pathColEnd) {
            ResolvedModule mod = resolveModule(uri, imp.modulePath);
            std::string doc = "### Module: `" + imp.modulePath + "`\n\n";
            doc += mod.isStandard ? "**Type**: Standard Library Module\n\n" : "**Type**: Local EasyScript Module\n\n";
            if (!mod.symbols.empty()) {
                doc += "**Exports (" + std::to_string(mod.symbols.size()) + "):**\n\n";
                for (const auto& s : mod.symbols) {
                    doc += "- `" + s.signature + "`\n";
                }
            }
            Json result = Json::object();
            Json contents = Json::object();
            contents["kind"] = "markdown";
            contents["value"] = doc;
            result["contents"] = contents;
            return result;
        }
    }

    WordContext ctx = getWordContext(text, line, character);
    if (ctx.word.empty()) return Json();

    // 2. 檢查游標是否在 `<alias>.<member>` 上的 member
    if (!ctx.parentObject.empty()) {
        for (const auto& imp : docImports) {
            if (imp.alias == ctx.parentObject) {
                ResolvedModule mod = resolveModule(uri, imp.modulePath);
                for (const auto& s : mod.symbols) {
                    if (s.name == ctx.word) {
                        std::string doc = "```easyscript\n" + s.signature + "\n```\n\n";
                        doc += "*From module `" + imp.modulePath + "`*\n\n";
                        if (!s.doc.empty()) doc += s.doc + "\n";
                        Json result = Json::object();
                        Json contents = Json::object();
                        contents["kind"] = "markdown";
                        contents["value"] = doc;
                        result["contents"] = contents;
                        return result;
                    }
                }
            }
        }

        // 檢查未顯式 import 的標準命名空間 (io, fs, path, os, Math)
        std::string stdPath;
        if (ctx.parentObject == "io") stdPath = "std/io";
        else if (ctx.parentObject == "fs") stdPath = "std/fs";
        else if (ctx.parentObject == "path") stdPath = "std/path";
        else if (ctx.parentObject == "os") stdPath = "std/os";
        else if (ctx.parentObject == "Math" || ctx.parentObject == "math") stdPath = "std/math";

        if (!stdPath.empty()) {
            ResolvedModule mod = resolveModule(uri, stdPath);
            for (const auto& s : mod.symbols) {
                if (s.name == ctx.word) {
                    std::string doc = "```easyscript\n" + s.signature + "\n```\n\n";
                    doc += "*From standard library `" + stdPath + "`*\n\n";
                    if (!s.doc.empty()) doc += s.doc + "\n";
                    Json result = Json::object();
                    Json contents = Json::object();
                    contents["kind"] = "markdown";
                    contents["value"] = doc;
                    result["contents"] = contents;
                    return result;
                }
            }
        }
    }

    // 3. 檢查游標是否在具名匯入符號上 (import { foo } from "...")
    for (const auto& imp : docImports) {
        if (std::find(imp.namedSymbols.begin(), imp.namedSymbols.end(), ctx.word) != imp.namedSymbols.end()) {
            ResolvedModule mod = resolveModule(uri, imp.modulePath);
            for (const auto& s : mod.symbols) {
                if (s.name == ctx.word) {
                    std::string doc = "```easyscript\n" + s.signature + "\n```\n\n";
                    doc += "*Imported from `" + imp.modulePath + "`*\n\n";
                    if (!s.doc.empty()) doc += s.doc + "\n";
                    Json result = Json::object();
                    Json contents = Json::object();
                    contents["kind"] = "markdown";
                    contents["value"] = doc;
                    result["contents"] = contents;
                    return result;
                }
            }
        }
        if (imp.alias == ctx.word) {
            std::string doc = "### Module `" + imp.alias + "`\n\nImported from `" + imp.modulePath + "`";
            Json result = Json::object();
            Json contents = Json::object();
            contents["kind"] = "markdown";
            contents["value"] = doc;
            result["contents"] = contents;
            return result;
        }
    }

    // 4. 關鍵字與內建型別/函式 hover
    std::string doc;
    if (ctx.word == "fn" || ctx.word == "function") {
        doc = "**fn** (keyword)\n\nDeclares a function in EasyScript.\n\n```easyscript\nfn add(a, b) {\n    return a + b;\n}\n```";
    } else if (ctx.word == "class") {
        doc = "**class** (keyword)\n\nDeclares an object-oriented class with fields and methods.\n\n```easyscript\nclass Greeter {\n    greet(name) {\n        print('Hello ' + name);\n    }\n}\n```";
    } else if (ctx.word == "print" || ctx.word == "println") {
        doc = "**print(value: any): void**\n\nBuilt-in function to print formatted string to standard output.";
    } else if (ctx.word == "assert") {
        doc = "**assert(condition: bool, message = ''): void**\n\nAsserts condition is true, throwing error otherwise.";
    } else if (ctx.word == "Terminal") {
        doc = "**class Terminal** (`std/io`)\n\nProvides cursor position control, screen clearing, and terminal metrics.";
    } else if (ctx.word == "Color") {
        doc = "**class Color** (`std/io`)\n\nProvides ANSI text styling, foreground/background colors, and RGB formatting.";
    } else if (ctx.word == "Prompt") {
        doc = "**class Prompt** (`std/io`)\n\nInteractive CLI prompt utilities for user input, confirmation, and password masking.";
    } else if (ctx.word == "Math") {
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
    if (text.empty()) return Json();

    std::vector<ImportInfo> docImports = parseDocumentImports(text);

    // 1. 檢查游標是否在 import 路徑字串上 -> 跨檔案跳轉到模組檔案開頭
    for (const auto& imp : docImports) {
        if (imp.line == line && character >= imp.pathColStart && character <= imp.pathColEnd) {
            ResolvedModule mod = resolveModule(uri, imp.modulePath);
            if (!mod.targetUri.empty()) {
                Json loc = Json::object();
                loc["uri"] = mod.targetUri;
                Json range = Json::object();
                Json start = Json::object();
                start["line"] = 0;
                start["character"] = 0;
                Json end = Json::object();
                end["line"] = 0;
                end["character"] = 0;
                range["start"] = start;
                range["end"] = end;
                loc["range"] = range;
                return loc;
            }
        }
    }

    WordContext ctx = getWordContext(text, line, character);
    if (ctx.word.empty()) return Json();

    // 2. 跨模組成員跳轉: <alias>.<member> -> 跳轉至模組內的宣告行
    if (!ctx.parentObject.empty()) {
        for (const auto& imp : docImports) {
            if (imp.alias == ctx.parentObject) {
                ResolvedModule mod = resolveModule(uri, imp.modulePath);
                for (const auto& s : mod.symbols) {
                    if (s.name == ctx.word && !s.sourceUri.empty()) {
                        Json loc = Json::object();
                        loc["uri"] = s.sourceUri;
                        Json range = Json::object();
                        Json start = Json::object();
                        start["line"] = s.line;
                        start["character"] = s.column;
                        Json end = Json::object();
                        end["line"] = s.line;
                        end["character"] = s.column + s.length;
                        range["start"] = start;
                        range["end"] = end;
                        loc["range"] = range;
                        return loc;
                    }
                }
            }
        }
    }

    // 3. 具名匯入跳轉: import { foo } from "./mod" -> 跳轉至模組內的宣告行
    for (const auto& imp : docImports) {
        if (std::find(imp.namedSymbols.begin(), imp.namedSymbols.end(), ctx.word) != imp.namedSymbols.end()) {
            ResolvedModule mod = resolveModule(uri, imp.modulePath);
            for (const auto& s : mod.symbols) {
                if (s.name == ctx.word && !s.sourceUri.empty()) {
                    Json loc = Json::object();
                    loc["uri"] = s.sourceUri;
                    Json range = Json::object();
                    Json start = Json::object();
                    start["line"] = s.line;
                    start["character"] = s.column;
                    Json end = Json::object();
                    end["line"] = s.line;
                    end["character"] = s.column + s.length;
                    range["start"] = start;
                    range["end"] = end;
                    loc["range"] = range;
                    return loc;
                }
            }
        }
    }

    // 4. 當前文檔內的定義跳轉 (fn, class, let, const)
    Lexer lexer(text);
    Token prev;
    while (true) {
        Token tok = lexer.nextToken();
        if (tok.type == TokenType::TOKEN_EOF) break;

        if (tok.type == TokenType::IDENTIFIER && tok.lexeme == ctx.word) {
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
