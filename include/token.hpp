#pragma once

#include <string>
#include <string_view>
#include <iostream>
#include <cstdint>

enum class TokenType : uint16_t {
    // -------------------------------------------------------------------------
    // 單字元符號
    // -------------------------------------------------------------------------
    LEFT_PAREN,         // (
    RIGHT_PAREN,        // )
    LEFT_BRACKET,       // [
    RIGHT_BRACKET,      // ]
    LEFT_BRACE,         // {
    RIGHT_BRACE,        // }
    COMMA,              // ,
    DOT,                // .
    COLON,              // :
    SEMICOLON,          // ;
    QUESTION,           // ?
    PLUS,               // +
    MINUS,              // -
    STAR,               // *
    SLASH,              // /
    PERCENT,            // %

    // -------------------------------------------------------------------------
    // 一或二字元符號
    // -------------------------------------------------------------------------
    ARROW,              // ->
    DOT_DOT,            // .. (Range 區間運算子)
    BANG,               // !
    BANG_EQUAL,         // !=
    EQUAL,              // =
    EQUAL_EQUAL,        // ==
    GREATER,            // >
    GREATER_EQUAL,      // >=
    LESS,               // <
    LESS_EQUAL,         // <=
    PLUS_PLUS,          // ++
    PLUS_EQUAL,         // +=
    MINUS_MINUS,        // --
    MINUS_EQUAL,        // -=
    STAR_EQUAL,         // *=
    SLASH_EQUAL,        // /=
    PERCENT_EQUAL,      // %=
    AMP_AMP,            // &&
    PIPE_PIPE,          // ||

    // -------------------------------------------------------------------------
    // 字面量 (Literals)
    // -------------------------------------------------------------------------
    IDENTIFIER,         // 識別碼變數名/函式名
    STRING,             // 純字串 "..."
    INT,                // 整數 42
    DOUBLE,             // 浮點數 3.14

    // -------------------------------------------------------------------------
    // 字串內插 (String Interpolation)
    // "prefix ${ expr } middle ${ expr } suffix"
    // -------------------------------------------------------------------------
    INTERPOLATION_HEAD,   // "prefix ${
    INTERPOLATION_MID,    // } middle ${
    INTERPOLATION_TAIL,   // } suffix"

    // -------------------------------------------------------------------------
    // 關鍵字 (Keywords)
    // -------------------------------------------------------------------------
    LET,                // let
    CONST,              // const
    IF,                 // if
    ELSE,               // else
    FOR,                // for
    IN,                 // in
    WHILE,              // while
    RETURN,             // return
    BREAK,              // break
    CONTINUE,           // continue
    FN,                 // fn
    CLASS,              // class
    INIT,               // init
    SUPER,              // super
    THIS,               // this
    PUBLIC,             // public
    PRIVATE,            // private
    PROTECTED,          // protected
    OVERRIDE,           // override
    IMPORT,             // import
    EXPORT,             // export
    FROM,               // from
    TRUE,               // true
    FALSE,              // false
    NIL,                // null / nil

    // -------------------------------------------------------------------------
    // 內建型別關鍵字 (Built-in Types)
    // -------------------------------------------------------------------------
    TYPE_INT,           // int
    TYPE_DOUBLE,        // double
    TYPE_BOOL,          // bool
    TYPE_STRING,        // string
    TYPE_MAP,           // Map
    TYPE_VOID,          // void

    // -------------------------------------------------------------------------
    // 特殊 Token
    // -------------------------------------------------------------------------
    ERROR,              // 詞法解析錯誤
    TOKEN_EOF,          // 檔案結束 (End of File)
};

std::string_view tokenTypeName(TokenType type) noexcept;

struct Token {
    TokenType type{TokenType::ERROR};
    std::string_view lexeme; // 指向原始碼的視圖（零記憶體拷貝）
    int line{1};
    int column{1};

    Token() = default;
    Token(TokenType type, std::string_view lexeme, int line, int column)
        : type(type), lexeme(lexeme), line(line), column(column) {}

    bool is(TokenType t) const noexcept { return type == t; }
};

std::ostream& operator<<(std::ostream& os, const Token& token);
