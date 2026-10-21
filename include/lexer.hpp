#pragma once

#include "token.hpp"
#include <string_view>
#include <vector>

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // 核心流式介面：按需產出下一個 Token (適合 Bytecode VM 單遍編譯)
    Token nextToken();

    // 批次掃描所有 Token 直到 EOF
    std::vector<Token> tokenizeAll();

    // 重設原始碼
    void reset(std::string_view newSource);

private:
    std::string_view source;
    size_t start{0};
    size_t current{0};
    int line{1};
    int column{1};
    int tokenStartColumn{1};

    // 字串內插狀態堆疊：記錄每個內插區塊的大括號深度
    std::vector<int> interpolationDepthStack;

    // 輔助檢查
    bool isAtEnd() const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    char peek() const noexcept;
    char peekNext() const noexcept;

    // 跳過空白與註解
    void skipWhitespace();

    // 各類型 Token 掃描
    Token makeToken(TokenType type) const;
    Token errorToken(std::string_view message) const;
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString(bool isResumingInterpolation = false);

    // 關鍵字識別
    static TokenType identifierType(std::string_view text) noexcept;
};
