#include "token.hpp"

std::string_view tokenTypeName(TokenType type) noexcept {
    switch (type) {
        case TokenType::LEFT_PAREN:         return "(";
        case TokenType::RIGHT_PAREN:        return ")";
        case TokenType::LEFT_BRACKET:       return "[";
        case TokenType::RIGHT_BRACKET:      return "]";
        case TokenType::LEFT_BRACE:         return "{";
        case TokenType::RIGHT_BRACE:        return "}";
        case TokenType::COMMA:              return ",";
        case TokenType::DOT:                return ".";
        case TokenType::COLON:              return ":";
        case TokenType::SEMICOLON:          return ";";
        case TokenType::QUESTION:           return "?";
        case TokenType::PLUS:               return "+";
        case TokenType::MINUS:              return "-";
        case TokenType::STAR:               return "*";
        case TokenType::SLASH:              return "/";
        case TokenType::PERCENT:            return "%";
        case TokenType::ARROW:              return "->";
        case TokenType::DOT_DOT:            return "..";
        case TokenType::BANG:               return "!";
        case TokenType::BANG_EQUAL:         return "!=";
        case TokenType::EQUAL:              return "=";
        case TokenType::EQUAL_EQUAL:        return "==";
        case TokenType::GREATER:            return ">";
        case TokenType::GREATER_EQUAL:      return ">=";
        case TokenType::LESS:               return "<";
        case TokenType::LESS_EQUAL:         return "<=";
        case TokenType::PLUS_PLUS:          return "++";
        case TokenType::PLUS_EQUAL:         return "+=";
        case TokenType::MINUS_MINUS:        return "--";
        case TokenType::MINUS_EQUAL:        return "-=";
        case TokenType::STAR_EQUAL:         return "*=";
        case TokenType::SLASH_EQUAL:        return "/=";
        case TokenType::PERCENT_EQUAL:      return "%=";
        case TokenType::AMP_AMP:            return "&&";
        case TokenType::PIPE_PIPE:          return "||";
        case TokenType::IDENTIFIER:         return "IDENTIFIER";
        case TokenType::STRING:             return "STRING";
        case TokenType::INT:                return "INT";
        case TokenType::DOUBLE:             return "DOUBLE";
        case TokenType::INTERPOLATION_HEAD: return "INTERPOLATION_HEAD";
        case TokenType::INTERPOLATION_MID:  return "INTERPOLATION_MID";
        case TokenType::INTERPOLATION_TAIL: return "INTERPOLATION_TAIL";
        case TokenType::LET:                return "let";
        case TokenType::CONST:              return "const";
        case TokenType::IF:                 return "if";
        case TokenType::ELSE:               return "else";
        case TokenType::FOR:                return "for";
        case TokenType::IN:                 return "in";
        case TokenType::WHILE:              return "while";
        case TokenType::RETURN:             return "return";
        case TokenType::BREAK:              return "break";
        case TokenType::CONTINUE:           return "continue";
        case TokenType::FN:                 return "fn";
        case TokenType::CLASS:              return "class";
        case TokenType::INIT:               return "init";
        case TokenType::SUPER:              return "super";
        case TokenType::THIS:               return "this";
        case TokenType::PUBLIC:             return "public";
        case TokenType::PRIVATE:            return "private";
        case TokenType::PROTECTED:          return "protected";
        case TokenType::OVERRIDE:           return "override";
        case TokenType::IMPORT:             return "import";
        case TokenType::EXPORT:             return "export";
        case TokenType::FROM:               return "from";
        case TokenType::TRUE:               return "true";
        case TokenType::FALSE:              return "false";
        case TokenType::NIL:                return "null";
        case TokenType::TYPE_INT:           return "int";
        case TokenType::TYPE_DOUBLE:        return "double";
        case TokenType::TYPE_BOOL:          return "bool";
        case TokenType::TYPE_STRING:        return "string";
        case TokenType::TYPE_MAP:           return "Map";
        case TokenType::TYPE_VOID:          return "void";
        case TokenType::ERROR:              return "ERROR";
        case TokenType::TOKEN_EOF:          return "EOF";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
    os << "[" << token.line << ":" << token.column << " "
       << tokenTypeName(token.type) << " '" << token.lexeme << "']";
    return os;
}
