#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "mini_json.hpp"

namespace lsp {

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
};

} // namespace lsp
