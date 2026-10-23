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
    std::vector<std::string> parameters{};
};

struct ResolvedModule {
    std::string path;
    std::string targetUri;
    bool isStandard{false};
    std::string doc;
    std::vector<std::string> globalAliases{};
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
    Json handleSignatureHelp(const std::string& uri, int line, int character);
    Json handleDocumentSymbol(const std::string& uri);
    Json handleDefinition(const std::string& uri, int line, int character);
    Json handleSemanticTokens(const std::string& uri);

    std::vector<ImportInfo> parseDocumentImports(const std::string& text);
    ResolvedModule resolveModule(const std::string& currentDocUri, const std::string& importPath);

    // .d.json 動態宣告庫支援與全域別名 (globalAlias)
    std::string workspaceRoot;
    std::unordered_map<std::string, ResolvedModule> djsonModules;
    std::unordered_map<std::string, std::string> globalAliasModuleMap;
    std::unordered_map<std::string, ExportedSymbol> globalAliasSymbolMap;
    std::unordered_map<std::string, int64_t> djsonFileMtimes;

    void scanDjsonModules();
    bool loadDjsonFile(const std::string& filePath);
    void checkDjsonReload();
};

} // namespace lsp
