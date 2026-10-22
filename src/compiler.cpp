#include "compiler.hpp"
#include <iostream>
#include <charconv>

Compiler::Compiler(Heap& heap, std::string_view source)
    : heap(heap), lexer(source) {
    GcHandle scriptHandle = heap.allocate<ObjFunction>("<script>");
    ObjFunction* scriptFn = heap.getAs<ObjFunction>(scriptHandle);

    FunctionState topState;
    topState.function = scriptFn;
    topState.functionHandle = scriptHandle;
    topState.type = FunctionType::SCRIPT;
    topState.locals.push_back(Local{Token(), 0, false}); // Slot 0 預留

    functionStack.push_back(std::move(topState));
}

ObjFunction* Compiler::compile() {
    advance();

    while (!match(TokenType::TOKEN_EOF)) {
        declaration();
    }

    emitReturn();
    return hadError ? nullptr : currentFunction().function;
}

// -----------------------------------------------------------------------------
// Token 前進與錯誤處理
// -----------------------------------------------------------------------------

void Compiler::advance() {
    previous = current;

    while (true) {
        current = lexer.nextToken();
        if (current.type != TokenType::ERROR) break;

        errorAtCurrent(current.lexeme);
    }
}

void Compiler::consume(TokenType type, std::string_view message) {
    if (current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

bool Compiler::check(TokenType type) const noexcept {
    return current.type == type;
}

bool Compiler::match(TokenType type) noexcept {
    if (!check(type)) return false;
    advance();
    return true;
}

void Compiler::errorAt(const Token& token, std::string_view message) {
    if (panicMode) return;
    panicMode = true;

    std::cerr << "[line " << token.line << "] Error";
    if (token.type == TokenType::TOKEN_EOF) {
        std::cerr << " at end";
    } else if (token.type != TokenType::ERROR) {
        std::cerr << " at '" << token.lexeme << "'";
    }
    std::cerr << ": " << message << std::endl;

    hadError = true;
}

void Compiler::error(std::string_view message) {
    errorAt(previous, message);
}

void Compiler::errorAtCurrent(std::string_view message) {
    errorAt(current, message);
}

// -----------------------------------------------------------------------------
// Bytecode 發射輔助
// -----------------------------------------------------------------------------

void Compiler::emitByte(uint8_t byte) {
    currentFunction().function->bytecode.push_back(byte);
}

void Compiler::emitOp(OpCode op) {
    emitByte(static_cast<uint8_t>(op));
}

void Compiler::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void Compiler::emitReturn() {
    if (currentFunction().type == FunctionType::INITIALIZER) {
        // 建構子自動返回 this (Slot 0)
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_LOCAL), 0);
    } else {
        emitOp(OpCode::OP_NIL);
    }
    emitOp(OpCode::OP_RETURN);
}

void Compiler::emitClosure(const FunctionState& finished) {
    uint8_t fnConstant = makeConstant(Value(finished.functionHandle));
    emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), fnConstant);
    emitByte(static_cast<uint8_t>(finished.upvalues.size()));
    for (const auto& u : finished.upvalues) {
        emitByte(u.isLocal ? 1 : 0);
        emitByte(u.index);
    }
}

uint8_t Compiler::makeConstant(Value value) {
    auto* fn = currentFunction().function;
    for (size_t i = 0; i < fn->constants.size(); ++i) {
        if (fn->constants[i] == value) {
            return static_cast<uint8_t>(i);
        }
    }
    fn->constants.push_back(value);
    if (fn->constants.size() > UINT8_MAX) {
        error("Too many constants in one function.");
        return 0;
    }
    return static_cast<uint8_t>(fn->constants.size() - 1);
}

void Compiler::emitConstant(Value value) {
    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), makeConstant(value));
}

int Compiler::emitJump(OpCode instruction) {
    emitOp(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return static_cast<int>(currentFunction().function->bytecode.size() - 2);
}

void Compiler::patchJump(int offset) {
    auto* fn = currentFunction().function;
    int jump = static_cast<int>(fn->bytecode.size() - offset - 2);
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    fn->bytecode[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
    fn->bytecode[offset + 1] = static_cast<uint8_t>(jump & 0xff);
}

void Compiler::emitLoop(int loopStart) {
    emitOp(OpCode::OP_LOOP);
    auto* fn = currentFunction().function;
    int offset = static_cast<int>(fn->bytecode.size() - loopStart + 2);
    if (offset > UINT16_MAX) {
        error("Loop body too large.");
    }
    emitByte(static_cast<uint8_t>((offset >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(offset & 0xff));
}

// -----------------------------------------------------------------------------
// 作用域與區域變數
// -----------------------------------------------------------------------------

void Compiler::beginScope() {
    currentFunction().scopeDepth++;
}

void Compiler::endScope() {
    auto& state = currentFunction();
    state.scopeDepth--;
    while (!state.locals.empty() && state.locals.back().depth > state.scopeDepth) {
        if (state.locals.back().isCaptured) {
            emitOp(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitOp(OpCode::OP_POP);
        }
        state.locals.pop_back();
    }
}

int Compiler::addLocal(Token name) {
    auto& locals = currentFunction().locals;
    if (locals.size() >= UINT8_MAX) {
        error("Too many local variables in function.");
        return -1;
    }
    locals.push_back(Local{name, -1, false});
    return static_cast<int>(locals.size() - 1);
}

int Compiler::resolveLocal(const Token& name) {
    return resolveLocal(currentFunction(), name);
}

int Compiler::resolveLocal(const FunctionState& state, const Token& name) {
    for (int i = static_cast<int>(state.locals.size()) - 1; i >= 0; --i) {
        const auto& local = state.locals[i];
        if (local.name.lexeme == name.lexeme) {
            if (local.depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

int Compiler::resolveUpvalue(size_t stateIndex, const Token& name) {
    if (stateIndex == 0) return -1;

    // 1. 先查外層第一層函式的區域變數
    int local = resolveLocal(functionStack[stateIndex - 1], name);
    if (local != -1) {
        functionStack[stateIndex - 1].locals[local].isCaptured = true;
        return addUpvalue(functionStack[stateIndex], static_cast<uint8_t>(local), true);
    }

    // 2. 若無則往更外層遞迴尋找 Upvalue
    int upvalue = resolveUpvalue(stateIndex - 1, name);
    if (upvalue != -1) {
        return addUpvalue(functionStack[stateIndex], static_cast<uint8_t>(upvalue), false);
    }

    return -1;
}

int Compiler::addUpvalue(FunctionState& state, uint8_t index, bool isLocal) {
    for (size_t i = 0; i < state.upvalues.size(); ++i) {
        const auto& u = state.upvalues[i];
        if (u.index == index && u.isLocal == isLocal) {
            return static_cast<int>(i);
        }
    }
    if (state.upvalues.size() >= UINT8_MAX) {
        error("Too many closure variables in function.");
        return 0;
    }
    state.upvalues.push_back(CompilerUpvalue{index, isLocal});
    return static_cast<int>(state.upvalues.size() - 1);
}

void Compiler::declareVariable() {
    if (currentFunction().scopeDepth == 0) return; // 全域變數延遲綁定

    Token name = previous;
    const auto& locals = currentFunction().locals;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
        const auto& local = locals[i];
        if (local.depth != -1 && local.depth < currentFunction().scopeDepth) break;

        if (local.name.lexeme == name.lexeme) {
            error("Already a variable with this name in this scope.");
        }
    }
    addLocal(name);
}

uint8_t Compiler::identifierConstant(const Token& name) {
    GcHandle strHandle = heap.internString(name.lexeme);
    return makeConstant(Value(strHandle));
}

uint8_t Compiler::parseVariable(std::string_view errorMessage) {
    consume(TokenType::IDENTIFIER, errorMessage);
    declareVariable();
    if (currentFunction().scopeDepth > 0) return 0;
    return identifierConstant(previous);
}

void Compiler::markInitialized() {
    if (currentFunction().scopeDepth == 0) return;
    currentFunction().locals.back().depth = currentFunction().scopeDepth;
}

void Compiler::defineVariable(uint8_t global) {
    if (currentFunction().scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(static_cast<uint8_t>(OpCode::OP_DEFINE_GLOBAL), global);
}

void Compiler::skipModifiers() {
    while (match(TokenType::PUBLIC) || match(TokenType::PRIVATE) ||
           match(TokenType::PROTECTED) || match(TokenType::OVERRIDE)) {
        // 跳過存取與覆寫修飾詞
    }
}

void Compiler::skipTypeAnnotation() {
    if (match(TokenType::COLON) || match(TokenType::ARROW)) {
        advance();
        if (match(TokenType::LESS)) {
            int depth = 1;
            while (depth > 0 && !check(TokenType::TOKEN_EOF)) {
                if (match(TokenType::LESS)) depth++;
                else if (match(TokenType::GREATER)) depth--;
                else advance();
            }
        }
        while (match(TokenType::LEFT_BRACKET)) {
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after '[' in type.");
        }
    }
}

uint8_t Compiler::argumentList() {
    uint8_t argCount = 0;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

// -----------------------------------------------------------------------------
// 語法與陳述句 (Statements)
// -----------------------------------------------------------------------------

void Compiler::declaration() {
    skipModifiers();

    if (match(TokenType::IMPORT)) {
        importDeclaration();
    } else if (match(TokenType::EXPORT)) {
        exportDeclaration();
    } else if (match(TokenType::CLASS)) {
        classDeclaration();
    } else if (match(TokenType::LET)) {
        varDeclaration();
    } else if (match(TokenType::FN)) {
        fnDeclaration();
    } else {
        statement();
    }

    if (panicMode) {
        panicMode = false;
        while (!check(TokenType::TOKEN_EOF)) {
            if (previous.type == TokenType::SEMICOLON) return;
            switch (current.type) {
                case TokenType::CLASS:
                case TokenType::FN:
                case TokenType::LET:
                case TokenType::FOR:
                case TokenType::IF:
                case TokenType::WHILE:
                case TokenType::RETURN:
                    return;
                default:
                    advance();
            }
        }
    }
}

// 類別宣告編譯
void Compiler::classDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect class name.");
    Token className = previous;
    uint8_t nameConstant = identifierConstant(className);
    declareVariable();

    emitBytes(static_cast<uint8_t>(OpCode::OP_CLASS), nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    // 繼承判定 : SuperClass, Interface...
    if (match(TokenType::COLON)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        variable(false);

        if (className.lexeme == previous.lexeme) {
            error("A class can't inherit from itself.");
        }

        beginScope();
        addLocal(Token{TokenType::SUPER, "super", previous.line, previous.column});
        markInitialized();

        namedVariable(className, false);
        emitOp(OpCode::OP_INHERIT);
        classCompiler.hasSuperclass = true;

        while (match(TokenType::COMMA)) {
            consume(TokenType::IDENTIFIER, "Expect interface name.");
        }
    }

    namedVariable(className, false); // 將類別放上棧頂以供綁定方法

    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");

    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::TOKEN_EOF)) {
        skipModifiers();

        if (match(TokenType::INIT)) {
            method(FunctionType::INITIALIZER);
        } else if (match(TokenType::FN)) {
            method(FunctionType::METHOD);
        } else if (match(TokenType::SEMICOLON)) {
            // 略過空分號
        } else {
            error("Expect method declaration in class body.");
            advance();
        }
    }

    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");
    emitOp(OpCode::OP_POP); // 彈出類別物件

    if (classCompiler.hasSuperclass) {
        endScope();
    }

    currentClass = currentClass->enclosing;
}

// 類別方法與建構子編譯
void Compiler::method(FunctionType type) {
    Token methodNameToken;
    if (type == FunctionType::INITIALIZER) {
        methodNameToken = Token{TokenType::IDENTIFIER, "init", previous.line, previous.column};
    } else {
        consume(TokenType::IDENTIFIER, "Expect method name.");
        methodNameToken = previous;
    }
    uint8_t constant = identifierConstant(methodNameToken);

    // 建立新方法函式狀態
    GcHandle fnHandle = heap.allocate<ObjFunction>(std::string(methodNameToken.lexeme));
    ObjFunction* fnObj = heap.getAs<ObjFunction>(fnHandle);

    FunctionState subState;
    subState.function = fnObj;
    subState.functionHandle = fnHandle;
    subState.type = type;
    // Slot 0 自動注入 this
    subState.locals.push_back(Local{Token{TokenType::THIS, "this", methodNameToken.line, methodNameToken.column}, 0, false});

    functionStack.push_back(std::move(subState));

    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after method name.");

    std::vector<Token> autoProperties; // 記錄 Parameter Properties

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentFunction().function->arity++;
            bool isProperty = (match(TokenType::PUBLIC) || match(TokenType::PRIVATE) || match(TokenType::PROTECTED));
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            if (isProperty) {
                autoProperties.push_back(previous);
            }
            skipTypeAnnotation();
            if (match(TokenType::EQUAL)) {
                expression(); // 預設參數
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    skipTypeAnnotation(); // 跳過回傳型別

    consume(TokenType::LEFT_BRACE, "Expect '{' before method body.");

    // 建構子參數屬性自動賦值注入：this.prop = prop;
    for (const auto& prop : autoProperties) {
        Token thisToken{TokenType::THIS, "this", prop.line, prop.column};
        namedVariable(thisToken, false); // 載入 this
        namedVariable(prop, false);      // 載入參數值
        uint8_t propConst = identifierConstant(prop);
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), propConst);
        emitOp(OpCode::OP_POP);
    }

    block();

    emitReturn();

    FunctionState finished = std::move(functionStack.back());
    functionStack.pop_back();

    emitClosure(finished);
    emitBytes(static_cast<uint8_t>(OpCode::OP_METHOD), constant);
}

void Compiler::varDeclaration() {
    uint8_t global = parseVariable("Expect variable name.");

    skipTypeAnnotation();

    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        emitOp(OpCode::OP_NIL);
    }

    if (match(TokenType::SEMICOLON)) {
        // 消耗標準分號
    } else if (previous.type == TokenType::RIGHT_BRACE) {
        // 賦值區塊/lambda 結尾允許省略分號
    } else {
        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
    }

    defineVariable(global);
}

void Compiler::fnDeclaration() {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();

    std::string fnName(previous.lexeme);

    GcHandle fnHandle = heap.allocate<ObjFunction>(fnName);
    ObjFunction* fnObj = heap.getAs<ObjFunction>(fnHandle);

    FunctionState subState;
    subState.function = fnObj;
    subState.functionHandle = fnHandle;
    subState.type = FunctionType::FUNCTION;
    subState.locals.push_back(Local{Token(), 0, false});

    functionStack.push_back(std::move(subState));

    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after function name.");

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentFunction().function->arity++;
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            skipTypeAnnotation();
            if (match(TokenType::EQUAL)) {
                expression(); // 預設參數
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

    skipTypeAnnotation();

    consume(TokenType::LEFT_BRACE, "Expect '{' before function body.");
    block();

    emitReturn();

    FunctionState finished = std::move(functionStack.back());
    functionStack.pop_back();

    emitClosure(finished);
    defineVariable(global);
}

void Compiler::exportDeclaration() {
    if (match(TokenType::FN)) {
        uint8_t global = parseVariable("Expect function name.");
        markInitialized();
        std::string fnName(previous.lexeme);

        GcHandle fnHandle = heap.allocate<ObjFunction>(fnName);
        ObjFunction* fnObj = heap.getAs<ObjFunction>(fnHandle);

        FunctionState subState;
        subState.function = fnObj;
        subState.functionHandle = fnHandle;
        subState.type = FunctionType::FUNCTION;
        subState.locals.push_back(Local{Token(), 0, false});

        functionStack.push_back(std::move(subState));

        beginScope();
        consume(TokenType::LEFT_PAREN, "Expect '(' after function name.");

        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                currentFunction().function->arity++;
                uint8_t paramConstant = parseVariable("Expect parameter name.");
                skipTypeAnnotation();
                if (match(TokenType::EQUAL)) {
                    expression();
                }
                defineVariable(paramConstant);
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
        skipTypeAnnotation();

        consume(TokenType::LEFT_BRACE, "Expect '{' before function body.");
        block();

        emitReturn();

        FunctionState finished = std::move(functionStack.back());
        functionStack.pop_back();

        emitClosure(finished);
        defineVariable(global);
        emitBytes(static_cast<uint8_t>(OpCode::OP_EXPORT), global);
    } else if (match(TokenType::LET)) {
        uint8_t global = parseVariable("Expect variable name.");
        skipTypeAnnotation();
        if (match(TokenType::EQUAL)) {
            expression();
        } else {
            emitOp(OpCode::OP_NIL);
        }
        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
        defineVariable(global);
        emitBytes(static_cast<uint8_t>(OpCode::OP_EXPORT), global);
    } else {
        error("Only functions or variables can be exported.");
    }
}

void Compiler::importDeclaration() {
    if (match(TokenType::LEFT_BRACE)) {
        // 具名匯入: import { a, b } from "module";
        std::vector<Token> symbols;
        do {
            consume(TokenType::IDENTIFIER, "Expect imported symbol name.");
            symbols.push_back(previous);
        } while (match(TokenType::COMMA));
        consume(TokenType::RIGHT_BRACE, "Expect '}' after import list.");

        consume(TokenType::FROM, "Expect 'from' after import symbols.");
        consume(TokenType::STRING, "Expect module path string.");

        std::string_view pathLexeme = previous.lexeme;
        std::string pathText(pathLexeme.substr(1, pathLexeme.size() - 2));
        GcHandle pathHandle = heap.allocateString(pathText);
        uint8_t pathConstant = makeConstant(Value(pathHandle));

        for (const auto& sym : symbols) {
            uint8_t symConstant = identifierConstant(sym);
            emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT_NAME), pathConstant);
            emitByte(symConstant);
        }

        consume(TokenType::SEMICOLON, "Expect ';' after import.");
    } else if (match(TokenType::IDENTIFIER)) {
        // 模組匯入: import Math from "std/math";
        Token modNameToken = previous;
        uint8_t nameConstant = identifierConstant(modNameToken);

        consume(TokenType::FROM, "Expect 'from' after import name.");
        consume(TokenType::STRING, "Expect module path string.");

        std::string_view pathLexeme = previous.lexeme;
        std::string pathText(pathLexeme.substr(1, pathLexeme.size() - 2));
        GcHandle pathHandle = heap.allocateString(pathText);
        uint8_t pathConstant = makeConstant(Value(pathHandle));

        emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT_MODULE), pathConstant);
        emitByte(nameConstant);

        consume(TokenType::SEMICOLON, "Expect ';' after import.");
    } else {
        error("Expect '{' or module name after 'import'.");
    }
}

void Compiler::statement() {
    if (match(TokenType::IF)) {
        ifStatement();
    } else if (match(TokenType::WHILE)) {
        whileStatement();
    } else if (match(TokenType::FOR)) {
        forStatement();
    } else if (match(TokenType::RETURN)) {
        returnStatement();
    } else if (match(TokenType::LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

void Compiler::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOp(OpCode::OP_POP); // 彈出 condition 判定值

    statement();

    int elseJump = emitJump(OpCode::OP_JUMP);
    patchJump(thenJump);
    emitOp(OpCode::OP_POP);

    if (match(TokenType::ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

void Compiler::ifExpression(bool) {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOp(OpCode::OP_POP); // 彈出 condition 判定值

    // 評估 then 分支表達式
    if (match(TokenType::LEFT_BRACE)) {
        beginScope();
        expression();
        consume(TokenType::RIGHT_BRACE, "Expect '}' after then-expression block.");
        endScope();
    } else {
        expression();
    }

    int elseJump = emitJump(OpCode::OP_JUMP);
    patchJump(thenJump);
    emitOp(OpCode::OP_POP); // 彈出 condition 判定值

    if (match(TokenType::ELSE)) {
        if (match(TokenType::LEFT_BRACE)) {
            beginScope();
            expression();
            consume(TokenType::RIGHT_BRACE, "Expect '}' after else-expression block.");
            endScope();
        } else {
            expression();
        }
    } else {
        emitOp(OpCode::OP_NIL);
    }

    patchJump(elseJump);
}

void Compiler::whileStatement() {
    int loopStart = static_cast<int>(currentFunction().function->bytecode.size());
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOp(OpCode::OP_POP);

    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitOp(OpCode::OP_POP);
}

void Compiler::forStatement() {
    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");

    // 檢查是否為 for (var in iterable) 形式
    if (check(TokenType::IDENTIFIER)) {
        Token varToken = current;
        advance();
        if (match(TokenType::IN)) {
            forInStatement(varToken);
            endScope();
            return;
        } else {
            // 普通識別碼賦值，如 for (i = 0; ...)
            namedVariable(varToken, true);
            consume(TokenType::SEMICOLON, "Expect ';' after expression.");
            emitOp(OpCode::OP_POP);
        }
    } else if (match(TokenType::LET)) {
        // C-style 宣告: for (let i = 0; ...)
        varDeclaration();
    } else if (match(TokenType::SEMICOLON)) {
        // 空初始化
    } else {
        expressionStatement();
    }

    int loopStart = static_cast<int>(currentFunction().function->bytecode.size());
    int exitJump = -1;

    if (!match(TokenType::SEMICOLON)) {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitOp(OpCode::OP_POP);
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        int bodyJump = emitJump(OpCode::OP_JUMP);
        int incrementStart = static_cast<int>(currentFunction().function->bytecode.size());
        expression();
        emitOp(OpCode::OP_POP);
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitOp(OpCode::OP_POP);
    }

    endScope();
}

void Compiler::forInStatement(Token varToken) {
    // 1. 評估可迭代物件 (例如 numbers 或 0..5)
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after for-in expression.");
    addLocal(Token{TokenType::IDENTIFIER, "$iter", previous.line, previous.column});
    markInitialized();

    // 2. 初始化迭代狀態 (在棧上推入 index 0)
    emitOp(OpCode::OP_ITER_INIT);
    addLocal(Token{TokenType::IDENTIFIER, "$index", previous.line, previous.column});
    markInitialized();

    // 3. 記錄迴圈頭部位置
    int loopStart = static_cast<int>(currentFunction().function->bytecode.size());

    // 4. 發射 OP_ITER_NEXT
    int exitJump = emitJump(OpCode::OP_ITER_NEXT);

    // 5. 宣告本輪迭代變數 (棧頂正是當前元素)
    beginScope();
    addLocal(varToken);
    markInitialized();

    // 6. 執行迴圈體
    statement();

    // 7. 彈出本輪迭代變數
    endScope();

    // 8. 回跳至 OP_ITER_NEXT
    emitLoop(loopStart);

    // 9. 回填結束跳轉
    patchJump(exitJump);
}

void Compiler::returnStatement() {
    if (currentFunction().type == FunctionType::SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TokenType::SEMICOLON)) {
        emitReturn();
    } else {
        if (currentFunction().type == FunctionType::INITIALIZER) {
            error("Can't return a value from an initializer.");
        }
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        emitOp(OpCode::OP_RETURN);
    }
}

void Compiler::block() {
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::TOKEN_EOF)) {
        declaration();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
}

void Compiler::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    emitOp(OpCode::OP_POP);
}

// -----------------------------------------------------------------------------
// Pratt Parser 運算式解析核心
// -----------------------------------------------------------------------------

void Compiler::expression() {
    parsePrecedence(Precedence::ASSIGNMENT);
}

void Compiler::parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(previous.type).prefix;
    if (prefixRule == nullptr) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (precedence <= getRule(current.type).precedence) {
        advance();
        ParseFn infixRule = getRule(previous.type).infix;
        (this->*infixRule)(canAssign);
    }

    if (canAssign && match(TokenType::EQUAL)) {
        error("Invalid assignment target.");
    }
}

// -----------------------------------------------------------------------------
// 前綴剖析 (Prefix Parselets)
// -----------------------------------------------------------------------------

void Compiler::number(bool) {
    std::string_view text = previous.lexeme;
    if (previous.type == TokenType::INT) {
        int64_t val = 0;
        if (text.starts_with("0x") || text.starts_with("0X")) {
            val = std::strtoll(text.data() + 2, nullptr, 16);
        } else if (text.starts_with("0b") || text.starts_with("0B")) {
            val = std::strtoll(text.data() + 2, nullptr, 2);
        } else {
            val = std::strtoll(text.data(), nullptr, 10);
        }
        emitConstant(Value(val));
    } else {
        double val = std::strtod(text.data(), nullptr);
        emitConstant(Value(val));
    }
}

void Compiler::string(bool) {
    std::string_view content = previous.lexeme.substr(1, previous.lexeme.size() - 2);
    GcHandle strHandle = heap.allocateString(std::string(content));
    emitConstant(Value(strHandle));
}

void Compiler::literal(bool) {
    switch (previous.type) {
        case TokenType::FALSE: emitOp(OpCode::OP_FALSE); break;
        case TokenType::TRUE:  emitOp(OpCode::OP_TRUE);  break;
        case TokenType::NIL:   emitOp(OpCode::OP_NIL);   break;
        default: return;
    }
}

void Compiler::grouping(bool) {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::unary(bool) {
    TokenType operatorType = previous.type;

    parsePrecedence(Precedence::UNARY);

    switch (operatorType) {
        case TokenType::BANG:  emitOp(OpCode::OP_NOT);    break;
        case TokenType::MINUS: emitOp(OpCode::OP_NEGATE); break;
        default: return;
    }
}

void Compiler::variable(bool canAssign) {
    namedVariable(previous, canAssign);
}

void Compiler::lambda(bool) {
    GcHandle fnHandle = heap.allocate<ObjFunction>("<anonymous>");
    ObjFunction* fnObj = heap.getAs<ObjFunction>(fnHandle);

    FunctionState subState;
    subState.function = fnObj;
    subState.functionHandle = fnHandle;
    subState.type = FunctionType::FUNCTION;
    subState.locals.push_back(Local{Token(), 0, false});

    functionStack.push_back(std::move(subState));

    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'fn'.");

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentFunction().function->arity++;
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            skipTypeAnnotation();
            if (match(TokenType::EQUAL)) {
                expression(); // 預設參數
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

    skipTypeAnnotation(); // 跳過 `-> returnType`

    consume(TokenType::LEFT_BRACE, "Expect '{' before lambda body.");
    block();

    emitReturn();

    FunctionState finished = std::move(functionStack.back());
    functionStack.pop_back();

    emitClosure(finished);
}

void Compiler::this_(bool) {
    if (currentClass == nullptr) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

void Compiler::super_(bool) {
    if (currentClass == nullptr) {
        error("Can't use 'super' outside of a class.");
        return;
    } else if (!currentClass->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
        return;
    }

    if (match(TokenType::LEFT_PAREN)) {
        // super(args...) 呼叫父類建構子
        Token thisToken{TokenType::THIS, "this", previous.line, previous.column};
        namedVariable(thisToken, false); // 載入 this
        uint8_t argCount = argumentList();

        Token initToken{TokenType::IDENTIFIER, "init", previous.line, previous.column};
        uint8_t initConst = identifierConstant(initToken);
        emitBytes(static_cast<uint8_t>(OpCode::OP_SUPER_INVOKE), initConst);
        emitByte(argCount);
    } else {
        consume(TokenType::DOT, "Expect '.' or '(' after 'super'.");
        consume(TokenType::IDENTIFIER, "Expect superclass method name.");
        uint8_t nameConst = identifierConstant(previous);

        Token thisToken{TokenType::THIS, "this", previous.line, previous.column};
        namedVariable(thisToken, false);

        if (match(TokenType::LEFT_PAREN)) {
            uint8_t argCount = argumentList();
            emitBytes(static_cast<uint8_t>(OpCode::OP_SUPER_INVOKE), nameConst);
            emitByte(argCount);
        } else {
            emitBytes(static_cast<uint8_t>(OpCode::OP_GET_SUPER), nameConst);
        }
    }
}

void Compiler::namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(name);

    if (arg != -1) {
        lastResolvedLocalSlot = arg;
        lastResolvedUpvalueSlot = -1;
        lastResolvedGlobalConstant = -1;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_LOCAL);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_LOCAL);
    } else if ((arg = resolveUpvalue(functionStack.size() - 1, name)) != -1) {
        lastResolvedLocalSlot = -1;
        lastResolvedUpvalueSlot = arg;
        lastResolvedGlobalConstant = -1;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_UPVALUE);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_UPVALUE);
    } else {
        arg = identifierConstant(name);
        lastResolvedLocalSlot = -1;
        lastResolvedUpvalueSlot = -1;
        lastResolvedGlobalConstant = arg;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_GLOBAL);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_GLOBAL);
    }

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes(setOp, static_cast<uint8_t>(arg));
    } else if (canAssign && match(TokenType::PLUS_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        expression();
        emitOp(OpCode::OP_ADD);
        emitBytes(setOp, static_cast<uint8_t>(arg));
    } else if (canAssign && match(TokenType::MINUS_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        expression();
        emitOp(OpCode::OP_SUB);
        emitBytes(setOp, static_cast<uint8_t>(arg));
    } else {
        emitBytes(getOp, static_cast<uint8_t>(arg));
    }
}

void Compiler::postfixInc(bool) {
    emitConstant(Value(int64_t(1)));
    emitOp(OpCode::OP_ADD);
    if (lastResolvedLocalSlot != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(lastResolvedLocalSlot));
    } else if (lastResolvedUpvalueSlot != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE), static_cast<uint8_t>(lastResolvedUpvalueSlot));
    } else if (lastResolvedGlobalConstant != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(lastResolvedGlobalConstant));
    }
}

void Compiler::postfixDec(bool) {
    emitConstant(Value(int64_t(1)));
    emitOp(OpCode::OP_SUB);
    if (lastResolvedLocalSlot != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_LOCAL), static_cast<uint8_t>(lastResolvedLocalSlot));
    } else if (lastResolvedUpvalueSlot != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_UPVALUE), static_cast<uint8_t>(lastResolvedUpvalueSlot));
    } else if (lastResolvedGlobalConstant != -1) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_GLOBAL), static_cast<uint8_t>(lastResolvedGlobalConstant));
    }
}

void Compiler::arrayLiteral(bool) {
    uint16_t count = 0;
    if (!check(TokenType::RIGHT_BRACKET)) {
        do {
            expression();
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after array elements.");

    emitOp(OpCode::OP_BUILD_ARRAY);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(count & 0xff));
}

void Compiler::mapLiteral(bool) {
    uint16_t count = 0;
    if (!check(TokenType::RIGHT_BRACE)) {
        do {
            expression(); // Key
            consume(TokenType::COLON, "Expect ':' between key and value.");
            expression(); // Value
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after map entries.");

    emitOp(OpCode::OP_BUILD_MAP);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(count & 0xff));
}

void Compiler::interpolation(bool) {
    std::string_view head = previous.lexeme;
    std::string_view prefix = head.substr(1, head.size() - 3);
    GcHandle headStr = heap.allocateString(std::string(prefix));
    emitConstant(Value(headStr));

    expression();
    emitOp(OpCode::OP_STRING_CONCAT);

    while (match(TokenType::INTERPOLATION_MID)) {
        std::string_view mid = previous.lexeme;
        std::string_view midText = mid.substr(1, mid.size() - 3);
        GcHandle midStr = heap.allocateString(std::string(midText));
        emitConstant(Value(midStr));
        emitOp(OpCode::OP_STRING_CONCAT);

        expression();
        emitOp(OpCode::OP_STRING_CONCAT);
    }

    if (match(TokenType::INTERPOLATION_TAIL)) {
        std::string_view tail = previous.lexeme;
        std::string_view suffix = tail.substr(1, tail.size() - 2);
        GcHandle tailStr = heap.allocateString(std::string(suffix));
        emitConstant(Value(tailStr));
        emitOp(OpCode::OP_STRING_CONCAT);
    }
}

// -----------------------------------------------------------------------------
// 中綴剖析 (Infix Parselets)
// -----------------------------------------------------------------------------

void Compiler::binary(bool) {
    TokenType operatorType = previous.type;
    const ParseRule& rule = getRule(operatorType);
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule.precedence) + 1));

    switch (operatorType) {
        case TokenType::PLUS:          emitOp(OpCode::OP_ADD); break;
        case TokenType::MINUS:         emitOp(OpCode::OP_SUB); break;
        case TokenType::STAR:          emitOp(OpCode::OP_MUL); break;
        case TokenType::SLASH:         emitOp(OpCode::OP_DIV); break;
        case TokenType::PERCENT:       emitOp(OpCode::OP_MOD); break;
        case TokenType::BANG_EQUAL:    emitBytes(static_cast<uint8_t>(OpCode::OP_EQUAL), static_cast<uint8_t>(OpCode::OP_NOT)); break;
        case TokenType::EQUAL_EQUAL:   emitOp(OpCode::OP_EQUAL); break;
        case TokenType::GREATER:       emitOp(OpCode::OP_GREATER); break;
        case TokenType::GREATER_EQUAL: emitBytes(static_cast<uint8_t>(OpCode::OP_LESS), static_cast<uint8_t>(OpCode::OP_NOT)); break;
        case TokenType::LESS:          emitOp(OpCode::OP_LESS); break;
        case TokenType::LESS_EQUAL:    emitBytes(static_cast<uint8_t>(OpCode::OP_GREATER), static_cast<uint8_t>(OpCode::OP_NOT)); break;
        default: return;
    }
}

void Compiler::and_(bool) {
    int endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOp(OpCode::OP_POP);
    parsePrecedence(Precedence::AND);
    patchJump(endJump);
}

void Compiler::or_(bool) {
    int elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    int endJump = emitJump(OpCode::OP_JUMP);

    patchJump(elseJump);
    emitOp(OpCode::OP_POP);

    parsePrecedence(Precedence::OR);
    patchJump(endJump);
}

void Compiler::call(bool) {
    uint8_t argCount = argumentList();
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), argCount);
}

void Compiler::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(previous);

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), name);
    } else if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(static_cast<uint8_t>(OpCode::OP_INVOKE), name);
        emitByte(argCount);
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), name);
    }
}

void Compiler::subscript(bool canAssign) {
    expression();
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitOp(OpCode::OP_SET_INDEX);
    } else {
        emitOp(OpCode::OP_GET_INDEX);
    }
}

void Compiler::range(bool) {
    parsePrecedence(Precedence::RANGE);
    emitOp(OpCode::OP_BUILD_RANGE);
}

// -----------------------------------------------------------------------------
// Pratt Parser 剖析規則表 (ParseRule Table)
// -----------------------------------------------------------------------------

const ParseRule& Compiler::getRule(TokenType type) const {
    static const ParseRule emptyRule = {nullptr, nullptr, Precedence::NONE};

    #define RULE(t, prefix, infix, prec) \
        case t: { static const ParseRule r = {prefix, infix, prec}; return r; }

    switch (type) {
        RULE(TokenType::LEFT_PAREN,         &Compiler::grouping,      &Compiler::call,       Precedence::CALL)
        RULE(TokenType::LEFT_BRACKET,       &Compiler::arrayLiteral,  &Compiler::subscript,  Precedence::CALL)
        RULE(TokenType::LEFT_BRACE,         &Compiler::mapLiteral,    nullptr,               Precedence::NONE)
        RULE(TokenType::DOT,                nullptr,                  &Compiler::dot,        Precedence::CALL)
        RULE(TokenType::PLUS,               nullptr,                  &Compiler::binary,     Precedence::TERM)
        RULE(TokenType::MINUS,              &Compiler::unary,         &Compiler::binary,     Precedence::TERM)
        RULE(TokenType::STAR,               nullptr,                  &Compiler::binary,     Precedence::FACTOR)
        RULE(TokenType::SLASH,              nullptr,                  &Compiler::binary,     Precedence::FACTOR)
        RULE(TokenType::PERCENT,            nullptr,                  &Compiler::binary,     Precedence::FACTOR)
        RULE(TokenType::DOT_DOT,            nullptr,                  &Compiler::range,      Precedence::RANGE)
        RULE(TokenType::PLUS_PLUS,          nullptr,                  &Compiler::postfixInc, Precedence::CALL)
        RULE(TokenType::MINUS_MINUS,        nullptr,                  &Compiler::postfixDec, Precedence::CALL)
        RULE(TokenType::BANG,               &Compiler::unary,         nullptr,               Precedence::NONE)
        RULE(TokenType::BANG_EQUAL,         nullptr,                  &Compiler::binary,     Precedence::EQUALITY)
        RULE(TokenType::EQUAL_EQUAL,        nullptr,                  &Compiler::binary,     Precedence::EQUALITY)
        RULE(TokenType::GREATER,            nullptr,                  &Compiler::binary,     Precedence::COMPARISON)
        RULE(TokenType::GREATER_EQUAL,      nullptr,                  &Compiler::binary,     Precedence::COMPARISON)
        RULE(TokenType::LESS,               nullptr,                  &Compiler::binary,     Precedence::COMPARISON)
        RULE(TokenType::LESS_EQUAL,         nullptr,                  &Compiler::binary,     Precedence::COMPARISON)
        RULE(TokenType::AMP_AMP,            nullptr,                  &Compiler::and_,       Precedence::AND)
        RULE(TokenType::PIPE_PIPE,          nullptr,                  &Compiler::or_,        Precedence::OR)
        RULE(TokenType::IDENTIFIER,         &Compiler::variable,      nullptr,               Precedence::NONE)
        RULE(TokenType::FN,                 &Compiler::lambda,        nullptr,               Precedence::NONE)
        RULE(TokenType::IF,                 &Compiler::ifExpression,  nullptr,               Precedence::NONE)
        RULE(TokenType::THIS,               &Compiler::this_,         nullptr,               Precedence::NONE)
        RULE(TokenType::SUPER,              &Compiler::super_,        nullptr,               Precedence::NONE)
        RULE(TokenType::STRING,             &Compiler::string,        nullptr,               Precedence::NONE)
        RULE(TokenType::INT,                &Compiler::number,        nullptr,               Precedence::NONE)
        RULE(TokenType::DOUBLE,             &Compiler::number,        nullptr,               Precedence::NONE)
        RULE(TokenType::INTERPOLATION_HEAD, &Compiler::interpolation, nullptr,               Precedence::NONE)
        RULE(TokenType::TRUE,               &Compiler::literal,       nullptr,               Precedence::NONE)
        RULE(TokenType::FALSE,              &Compiler::literal,       nullptr,               Precedence::NONE)
        RULE(TokenType::NIL,                &Compiler::literal,       nullptr,               Precedence::NONE)
        default:
            return emptyRule;
    }
    #undef RULE
}
