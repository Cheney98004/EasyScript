#pragma once

#include "token.hpp"
#include "lexer.hpp"
#include "heap.hpp"
#include "object.hpp"
#include "opcode.hpp"
#include <string_view>
#include <vector>
#include <optional>

// -----------------------------------------------------------------------------
// 運算子優先權 (由低到高，供 Pratt Parser 使用)
// -----------------------------------------------------------------------------
enum class Precedence : uint8_t {
    NONE,
    ASSIGNMENT,     // = += -=
    OR,             // ||
    AND,            // &&
    EQUALITY,       // == !=
    COMPARISON,     // < > <= >=
    RANGE,          // ..
    TERM,           // + -
    FACTOR,         // * / %
    UNARY,          // ! -
    CALL,           // . () []
    PRIMARY
};

// 編譯器函式型態
enum class FunctionType {
    SCRIPT,         // 頂層腳本主體
    FUNCTION,       // 使用者定義函式
    METHOD,         // 類別成員方法
    INITIALIZER,    // 類別建構子 (init)
};

// 區域變數槽位資訊
struct Local {
    Token name;
    int depth{-1};  // -1 表示正在宣告中（尚未初始化完成）
    bool isCaptured{false};
};

// 單一函式的編譯狀態
struct FunctionState {
    ObjFunction* function{nullptr};
    GcHandle functionHandle;
    FunctionType type{FunctionType::SCRIPT};
    std::vector<Local> locals;
    int scopeDepth{0};
};

// 類別編譯期上下文
struct ClassCompiler {
    ClassCompiler* enclosing{nullptr};
    bool hasSuperclass{false};
};

class Compiler;
using ParseFn = void (Compiler::*)(bool canAssign);

struct ParseRule {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
};

// -----------------------------------------------------------------------------
// Pratt Parser & Bytecode Compiler
// -----------------------------------------------------------------------------
class Compiler {
public:
    Compiler(Heap& heap, std::string_view source);

    // 核心編譯入口：將原始碼編譯為 ObjFunction
    ObjFunction* compile();

private:
    Heap& heap;
    Lexer lexer;
    Token previous;
    Token current;
    bool hadError{false};
    bool panicMode{false};

    // 函式編譯棧 (支援巢狀函式/方法編譯)
    std::vector<FunctionState> functionStack;
    ClassCompiler* currentClass{nullptr};

    int lastResolvedLocalSlot{-1};
    int lastResolvedGlobalConstant{-1};

    FunctionState& currentFunction() noexcept { return functionStack.back(); }
    const FunctionState& currentFunction() const noexcept { return functionStack.back(); }

    // -------------------------------------------------------------------------
    // Token 消耗與前進
    // -------------------------------------------------------------------------
    void advance();
    void consume(TokenType type, std::string_view message);
    bool check(TokenType type) const noexcept;
    bool match(TokenType type) noexcept;
    void errorAt(const Token& token, std::string_view message);
    void error(std::string_view message);
    void errorAtCurrent(std::string_view message);

    // -------------------------------------------------------------------------
    // Bytecode 指令發射輔助
    // -------------------------------------------------------------------------
    void emitByte(uint8_t byte);
    void emitOp(OpCode op);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitReturn();
    uint8_t makeConstant(Value value);
    void emitConstant(Value value);

    // 跳轉指令與回填 (Backpatching)
    int emitJump(OpCode instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);

    // -------------------------------------------------------------------------
    // 作用域與區域變數管理
    // -------------------------------------------------------------------------
    void beginScope();
    void endScope();
    int addLocal(Token name);
    int resolveLocal(const Token& name);
    void declareVariable();
    uint8_t parseVariable(std::string_view errorMessage);
    void markInitialized();
    void defineVariable(uint8_t global);

    // -------------------------------------------------------------------------
    // 語法與語句解析 (Grammar & Statements)
    // -------------------------------------------------------------------------
    void declaration();
    void classDeclaration();
    void varDeclaration();
    void fnDeclaration();
    void importDeclaration();
    void exportDeclaration();
    void method(FunctionType type);
    void statement();
    void ifStatement();
    void whileStatement();
    void forStatement();
    void forInStatement(Token varToken);
    void returnStatement();
    void block();
    void expressionStatement();
    void skipTypeAnnotation();
    void skipModifiers();

    // -------------------------------------------------------------------------
    // Pratt Parser 運算式解析核心
    // -------------------------------------------------------------------------
    void expression();
    void parsePrecedence(Precedence precedence);
    const ParseRule& getRule(TokenType type) const;

    // 前綴處理函式 (Prefix Parselet)
    void number(bool canAssign);
    void string(bool canAssign);
    void literal(bool canAssign);
    void grouping(bool canAssign);
    void unary(bool canAssign);
    void variable(bool canAssign);
    void ifExpression(bool canAssign);
    void this_(bool canAssign);
    void super_(bool canAssign);
    void lambda(bool canAssign);
    void arrayLiteral(bool canAssign);
    void mapLiteral(bool canAssign);
    void interpolation(bool canAssign);

    // 中綴處理函式 (Infix Parselet)
    void binary(bool canAssign);
    void and_(bool canAssign);
    void or_(bool canAssign);
    void call(bool canAssign);
    void dot(bool canAssign);
    void subscript(bool canAssign);
    void range(bool canAssign);
    void postfixInc(bool canAssign);
    void postfixDec(bool canAssign);

    // 輔助變數存取
    void namedVariable(Token name, bool canAssign);
    uint8_t identifierConstant(const Token& name);
    uint8_t argumentList();
};
