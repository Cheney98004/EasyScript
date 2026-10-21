#include "lexer.hpp"
#include <unordered_map>
#include <cctype>

namespace {

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool isAlpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isAlphaNumeric(char c) noexcept {
    return isAlpha(c) || isDigit(c);
}

} // namespace

Lexer::Lexer(std::string_view source)
    : source(source) {}

void Lexer::reset(std::string_view newSource) {
    source = newSource;
    start = 0;
    current = 0;
    line = 1;
    column = 1;
    tokenStartColumn = 1;
    interpolationDepthStack.clear();
}

bool Lexer::isAtEnd() const noexcept {
    return current >= source.size();
}

char Lexer::advance() noexcept {
    char c = source[current++];
    column++;
    return c;
}

bool Lexer::match(char expected) noexcept {
    if (isAtEnd() || source[current] != expected) return false;
    current++;
    column++;
    return true;
}

char Lexer::peek() const noexcept {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Lexer::peekNext() const noexcept {
    if (current + 1 >= source.size()) return '\0';
    return source[current + 1];
}

void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line++;
                column = 0;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // 單行註解：跳至行末
                    while (peek() != '\n' && !isAtEnd()) {
                        advance();
                    }
                } else if (peekNext() == '*') {
                    // 多行註解：/* ... */
                    advance(); // 吃掉 '/'
                    advance(); // 吃掉 '*'
                    while (!isAtEnd()) {
                        if (peek() == '\n') {
                            line++;
                            column = 0;
                        }
                        if (peek() == '*' && peekNext() == '/') {
                            advance(); // 吃掉 '*'
                            advance(); // 吃掉 '/'
                            break;
                        }
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Lexer::makeToken(TokenType type) const {
    std::string_view lexeme = source.substr(start, current - start);
    return Token(type, lexeme, line, tokenStartColumn);
}

Token Lexer::errorToken(std::string_view message) const {
    return Token(TokenType::ERROR, message, line, tokenStartColumn);
}

Token Lexer::scanIdentifierOrKeyword() {
    while (isAlphaNumeric(peek())) {
        advance();
    }
    std::string_view text = source.substr(start, current - start);
    return makeToken(identifierType(text));
}

TokenType Lexer::identifierType(std::string_view text) noexcept {
    static const std::unordered_map<std::string_view, TokenType> keywords = {
        {"let",       TokenType::LET},
        {"const",     TokenType::CONST},
        {"if",        TokenType::IF},
        {"else",      TokenType::ELSE},
        {"for",       TokenType::FOR},
        {"in",        TokenType::IN},
        {"while",     TokenType::WHILE},
        {"return",    TokenType::RETURN},
        {"break",     TokenType::BREAK},
        {"continue",  TokenType::CONTINUE},
        {"fn",        TokenType::FN},
        {"class",     TokenType::CLASS},
        {"init",      TokenType::INIT},
        {"super",     TokenType::SUPER},
        {"this",      TokenType::THIS},
        {"public",    TokenType::PUBLIC},
        {"private",   TokenType::PRIVATE},
        {"protected", TokenType::PROTECTED},
        {"override",  TokenType::OVERRIDE},
        {"import",    TokenType::IMPORT},
        {"export",    TokenType::EXPORT},
        {"from",      TokenType::FROM},
        {"true",      TokenType::TRUE},
        {"false",     TokenType::FALSE},
        {"null",      TokenType::NIL},
        {"nil",       TokenType::NIL},
        {"int",       TokenType::TYPE_INT},
        {"double",    TokenType::TYPE_DOUBLE},
        {"bool",      TokenType::TYPE_BOOL},
        {"string",    TokenType::TYPE_STRING},
        {"Map",       TokenType::TYPE_MAP},
        {"void",      TokenType::TYPE_VOID},
    };

    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenType::IDENTIFIER;
}

Token Lexer::scanNumber() {
    if (source[start] == '0') {
        if (peek() == 'x' || peek() == 'X') {
            advance(); // 'x'
            while (isxdigit(static_cast<unsigned char>(peek()))) advance();
            return makeToken(TokenType::INT);
        } else if (peek() == 'b' || peek() == 'B') {
            advance(); // 'b'
            while (peek() == '0' || peek() == '1') advance();
            return makeToken(TokenType::INT);
        }
    }

    while (isDigit(peek())) advance();

    // 判斷是否為小數點：
    // 若為 '..' (Range 運算子)，則不可視為浮點數
    if (peek() == '.' && peekNext() != '.' && isDigit(peekNext())) {
        advance(); // 吃掉 '.'
        while (isDigit(peek())) advance();
        return makeToken(TokenType::DOUBLE);
    }

    return makeToken(TokenType::INT);
}

Token Lexer::scanString(bool isResumingInterpolation) {
    while (!isAtEnd()) {
        char c = peek();
        if (c == '"') {
            advance(); // 吃掉結尾 '"'
            if (isResumingInterpolation) {
                return makeToken(TokenType::INTERPOLATION_TAIL);
            }
            return makeToken(TokenType::STRING);
        } else if (c == '$' && peekNext() == '{') {
            // 遇見字串內插 ${
            advance(); // 吃掉 '$'
            advance(); // 吃掉 '{'
            interpolationDepthStack.push_back(1);
            if (isResumingInterpolation) {
                return makeToken(TokenType::INTERPOLATION_MID);
            }
            return makeToken(TokenType::INTERPOLATION_HEAD);
        } else if (c == '\\') {
            advance(); // 跳過轉義反斜線
            if (!isAtEnd()) advance();
        } else {
            if (c == '\n') {
                line++;
                column = 0;
            }
            advance();
        }
    }

    return errorToken("Unterminated string.");
}

Token Lexer::nextToken() {
    skipWhitespace();

    start = current;
    tokenStartColumn = column;

    if (isAtEnd()) {
        return makeToken(TokenType::TOKEN_EOF);
    }

    char c = advance();

    // 識別碼或關鍵字
    if (isAlpha(c)) {
        return scanIdentifierOrKeyword();
    }

    // 數字
    if (isDigit(c)) {
        return scanNumber();
    }

    switch (c) {
        // 單字元符號
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '[': return makeToken(TokenType::LEFT_BRACKET);
        case ']': return makeToken(TokenType::RIGHT_BRACKET);
        case '{': {
            if (!interpolationDepthStack.empty()) {
                interpolationDepthStack.back()++;
            }
            return makeToken(TokenType::LEFT_BRACE);
        }
        case '}': {
            if (!interpolationDepthStack.empty()) {
                interpolationDepthStack.back()--;
                if (interpolationDepthStack.back() == 0) {
                    interpolationDepthStack.pop_back();
                    // 內插表達式結束，繼續掃描剩餘字串
                    return scanString(true);
                }
            }
            return makeToken(TokenType::RIGHT_BRACE);
        }
        case ',': return makeToken(TokenType::COMMA);
        case ':': return makeToken(TokenType::COLON);
        case ';': return makeToken(TokenType::SEMICOLON);
        case '?': return makeToken(TokenType::QUESTION);

        // 運算子與一或二字元組合
        case '.':
            if (match('.')) return makeToken(TokenType::DOT_DOT); // '..' 區間
            return makeToken(TokenType::DOT);

        case '+':
            if (match('+')) return makeToken(TokenType::PLUS_PLUS);
            if (match('=')) return makeToken(TokenType::PLUS_EQUAL);
            return makeToken(TokenType::PLUS);

        case '-':
            if (match('>')) return makeToken(TokenType::ARROW);    // '->' 回傳型別
            if (match('-')) return makeToken(TokenType::MINUS_MINUS);
            if (match('=')) return makeToken(TokenType::MINUS_EQUAL);
            return makeToken(TokenType::MINUS);

        case '*':
            if (match('=')) return makeToken(TokenType::STAR_EQUAL);
            return makeToken(TokenType::STAR);

        case '/':
            if (match('=')) return makeToken(TokenType::SLASH_EQUAL);
            return makeToken(TokenType::SLASH);

        case '%':
            if (match('=')) return makeToken(TokenType::PERCENT_EQUAL);
            return makeToken(TokenType::PERCENT);

        case '!':
            if (match('=')) return makeToken(TokenType::BANG_EQUAL);
            return makeToken(TokenType::BANG);

        case '=':
            if (match('=')) return makeToken(TokenType::EQUAL_EQUAL);
            return makeToken(TokenType::EQUAL);

        case '<':
            if (match('=')) return makeToken(TokenType::LESS_EQUAL);
            return makeToken(TokenType::LESS);

        case '>':
            if (match('=')) return makeToken(TokenType::GREATER_EQUAL);
            return makeToken(TokenType::GREATER);

        case '&':
            if (match('&')) return makeToken(TokenType::AMP_AMP);
            return errorToken("Expected '&' after '&'.");

        case '|':
            if (match('|')) return makeToken(TokenType::PIPE_PIPE);
            return errorToken("Expected '|' after '|'.");

        // 字串字面量
        case '"':
            return scanString(false);

        default:
            return errorToken("Unexpected character.");
    }
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> tokens;
    while (true) {
        Token token = nextToken();
        tokens.push_back(token);
        if (token.type == TokenType::TOKEN_EOF) {
            break;
        }
    }
    return tokens;
}
