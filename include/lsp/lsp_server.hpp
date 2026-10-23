#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "mini_json.hpp"

namespace lsp {

struct ExportedSymbol {
    std::string name;
    int kind{0};
    int line{0};
    int column{0};
    int length{0};
    std::string signature;
    std::string doc;
    std::string sourceUri;
    std::vector<ExportedSymbol> members{};
};

struct ResolvedModule {
    std::string path;
    std::string targetUri;
    bool isStandard{false};
    std::vector<ExportedSymbol> symbols;
};

struct ImportInfo {
    std::string modulePath;
    std::string alias;
    std::vector<std::string> namedSymbols;
    int line{0};
    int pathColStart{0};
    int pathColEnd{0};
};

class LspServer {
public:
    LspServer();
    void run();

private:
    std::unordered_map<std::string, std::string> documents;
    bool isRunning{true};

    void processMessage(const std::string& rawJson);
    void handleRequest(const Json& msg);
    void handleNotification(const Json& msg);

    void sendJson(const Json& json);
    void sendResponse(const Json& id, const Json& result);
    void sendError(const Json& id, int code, const std::string& message);

    void validateDocument(const std::string& uri, const std::string& text);
    Json handleCompletion(const std::string& uri, int line, int character);
    Json handleHover(const std::string& uri, int line, int character);
    Json handleDocumentSymbol(const std::string& uri);
    Json handleDefinition(const std::string& uri, int line, int character);
    Json handleSemanticTokens(const std::string& uri);

    std::vector<ImportInfo> parseDocumentImports(const std::string& text);
    ResolvedModule resolveModule(const std::string& currentDocUri, const std::string& importPath);
};

} // namespace lsp
