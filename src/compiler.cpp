#include "compiler.hpp"
#include <iostream>
#include <charconv>
#include <cctype>

namespace {

std::string unescapeString(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char next = raw[++i];
            switch (next) {
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case '\\': result.push_back('\\'); break;
                case '"': result.push_back('"'); break;
                case '\'': result.push_back('\''); break;
                case '0': result.push_back('\0'); break;
                case 'a': result.push_back('\a'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'v': result.push_back('\v'); break;
                case 'e': result.push_back('\033'); break;
                case 'x': { // 16進制位元組 \xHH
                    if (i + 2 < raw.size() && 
                        std::isxdigit(static_cast<unsigned char>(raw[i + 1])) && 
                        std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
                        char hex[3] = {raw[i + 1], raw[i + 2], '\0'};
                        char val = static_cast<char>(std::strtol(hex, nullptr, 16));
                        result.push_back(val);
                        i += 2;
                    } else {
                        result.push_back('\\');
                        result.push_back('x');
                    }
                    break;
                }
                case 'u': { // 16進制 Unicode \uHHHH
                    if (i + 4 < raw.size() &&
                        std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
                        std::isxdigit(static_cast<unsigned char>(raw[i + 2])) &&
                        std::isxdigit(static_cast<unsigned char>(raw[i + 3])) &&
                        std::isxdigit(static_cast<unsigned char>(raw[i + 4]))) {
                        char hex[5] = {raw[i + 1], raw[i + 2], raw[i + 3], raw[i + 4], '\0'};
                        uint32_t codepoint = static_cast<uint32_t>(std::strtoul(hex, nullptr, 16));
                        i += 4;
                        // 轉為 UTF-8 編碼
                        if (codepoint <= 0x7F) {
                            result.push_back(static_cast<char>(codepoint));
                        } else if (codepoint <= 0x7FF) {
                            result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        } else if (codepoint <= 0xFFFF) {
                            result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        } else if (codepoint <= 0x10FFFF) {
                            result.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                        }
                    } else {
                        result.push_back('\\');
                        result.push_back('u');
                    }
                    break;
                }
                default:
                    result.push_back(next);
                    break;
            }
        } else {
            result.push_back(raw[i]);
        }
    }
    return result;
}

} // namespace

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

    if (diagnosticSink) {
        CompilerDiagnostic diag;
        diag.line = token.line;
        diag.column = token.column;
        diag.length = static_cast<int>(token.lexeme.empty() ? 1 : token.lexeme.size());
        diag.severity = 1;
        diag.message = std::string(message);
        diagnosticSink->push_back(std::move(diag));
    } else {
        std::cerr << "[line " << token.line << "] Error";
        if (token.type == TokenType::TOKEN_EOF) {
            std::cerr << " at end";
        } else if (token.type != TokenType::ERROR) {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << message << std::endl;
    }

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

void Compiler::patchJumpTo(int offset, int target) {
    auto* fn = currentFunction().function;
    fn->bytecode[offset] = static_cast<uint8_t>((target >> 8) & 0xff);
    fn->bytecode[offset + 1] = static_cast<uint8_t>(target & 0xff);
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

Type Compiler::parseType() {
    Type baseType = Type::Any();

    if (match(TokenType::TYPE_INT)) {
        baseType = Type::Int();
    } else if (match(TokenType::TYPE_DOUBLE)) {
        baseType = Type::Double();
    } else if (match(TokenType::TYPE_BOOL)) {
        baseType = Type::Bool();
    } else if (match(TokenType::TYPE_STRING)) {
        baseType = Type::String();
    } else if (match(TokenType::TYPE_VOID)) {
        baseType = Type::Void();
    } else if (match(TokenType::TYPE_MAP)) {
        Type keyT = Type::Any();
        Type valT = Type::Any();
        if (match(TokenType::LESS)) {
            keyT = parseType();
            consume(TokenType::COMMA, "Expect ',' between Map key and value types.");
            valT = parseType();
            consume(TokenType::GREATER, "Expect '>' after Map types.");
        }
        baseType = Type::Map(keyT, valT);
    } else if (match(TokenType::IDENTIFIER)) {
        baseType = Type::Class(std::string(previous.lexeme));
    }

    while (match(TokenType::LEFT_BRACKET)) {
        consume(TokenType::RIGHT_BRACKET, "Expect ']' after '[' in array type.");
        baseType = Type::Array(baseType);
    }

    return baseType;
}

Type Compiler::parseTypeAnnotation() {
    if (match(TokenType::COLON) || match(TokenType::ARROW)) {
        return parseType();
    }
    return Type::Any();
}

void Compiler::skipTypeAnnotation() {
    parseTypeAnnotation();
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
    globalTypes[std::string(className.lexeme)] = Type::Class(std::string(className.lexeme));

    ClassCompiler classCompiler;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;

    // 繼承判定 : SuperClass, Interface...
    if (match(TokenType::COLON)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        classHierarchy[std::string(className.lexeme)] = std::string(previous.lexeme);
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
            Type pType = parseTypeAnnotation();
            currentFunction().locals.back().type = pType;
            if (match(TokenType::EQUAL)) {
                Type defType = expression(); // 預設參數
                if (pType.kind != TypeKind::ANY && !isAssignable(pType, defType, classHierarchy)) {
                    error("Default parameter type does not match parameter type.");
                }
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    Type retType = parseTypeAnnotation();
    currentFunction().returnType = retType;

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
    Token varToken = previous;

    Type expectedType = parseTypeAnnotation();

    Type initType = Type::Nil();
    if (match(TokenType::EQUAL)) {
        initType = expression();
        if (expectedType.kind != TypeKind::ANY && !isAssignable(expectedType, initType, classHierarchy)) {
            error("Type mismatch: cannot assign value of type '" + initType.toString() +
                  "' to variable '" + std::string(varToken.lexeme) + "' of type '" + expectedType.toString() + "'.");
        }
    } else {
        emitOp(OpCode::OP_NIL);
    }

    Type assignedType = (expectedType.kind != TypeKind::ANY) ? expectedType : initType;
    if (currentFunction().scopeDepth > 0) {
        currentFunction().locals.back().type = assignedType;
    } else {
        globalTypes[std::string(varToken.lexeme)] = assignedType;
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

    std::vector<Type> paramTypes;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentFunction().function->arity++;
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            Type pType = parseTypeAnnotation();
            currentFunction().locals.back().type = pType;
            paramTypes.push_back(pType);
            if (match(TokenType::EQUAL)) {
                Type defType = expression(); // 預設參數
                if (pType.kind != TypeKind::ANY && !isAssignable(pType, defType, classHierarchy)) {
                    error("Default parameter type does not match parameter type.");
                }
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

    Type retType = parseTypeAnnotation();
    currentFunction().returnType = retType;
    globalTypes[fnName] = Type::Function(paramTypes, retType);

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

        std::vector<Type> paramTypes;
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                currentFunction().function->arity++;
                uint8_t paramConstant = parseVariable("Expect parameter name.");
                Type pType = parseTypeAnnotation();
                currentFunction().locals.back().type = pType;
                paramTypes.push_back(pType);
                if (match(TokenType::EQUAL)) {
                    Type defType = expression();
                    if (pType.kind != TypeKind::ANY && !isAssignable(pType, defType, classHierarchy)) {
                        error("Default parameter type does not match parameter type.");
                    }
                }
                defineVariable(paramConstant);
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

        Type retType = parseTypeAnnotation();
        currentFunction().returnType = retType;
        globalTypes[fnName] = Type::Function(paramTypes, retType);

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
        Token varToken = previous;
        Type expectedType = parseTypeAnnotation();
        Type initType = Type::Nil();
        if (match(TokenType::EQUAL)) {
            initType = expression();
            if (expectedType.kind != TypeKind::ANY && !isAssignable(expectedType, initType, classHierarchy)) {
                error("Type mismatch in export let: cannot assign '" + initType.toString() + "' to '" + expectedType.toString() + "'.");
            }
        } else {
            emitOp(OpCode::OP_NIL);
        }
        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
        defineVariable(global);
        emitBytes(static_cast<uint8_t>(OpCode::OP_EXPORT), global);
        globalTypes[std::string(varToken.lexeme)] = (expectedType.kind != TypeKind::ANY) ? expectedType : initType;
    } else if (match(TokenType::CONST)) {
        uint8_t global = parseVariable("Expect constant name.");
        Token varToken = previous;
        Type expectedType = parseTypeAnnotation();
        consume(TokenType::EQUAL, "Expect '=' after constant name.");
        Type initType = expression();
        if (expectedType.kind != TypeKind::ANY && !isAssignable(expectedType, initType, classHierarchy)) {
            error("Type mismatch in export const: cannot assign '" + initType.toString() + "' to '" + expectedType.toString() + "'.");
        }
        consume(TokenType::SEMICOLON, "Expect ';' after constant declaration.");
        defineVariable(global);
        emitBytes(static_cast<uint8_t>(OpCode::OP_EXPORT), global);
        globalTypes[std::string(varToken.lexeme)] = (expectedType.kind != TypeKind::ANY) ? expectedType : initType;
    } else if (check(TokenType::CLASS)) {
        advance();
        Token className = current;
        classDeclaration();
        uint8_t global = identifierConstant(className);
        emitBytes(static_cast<uint8_t>(OpCode::OP_EXPORT), global);
    } else {
        error("Only functions, variables, constants, or classes can be exported.");
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
        std::string pathText = unescapeString(pathLexeme.substr(1, pathLexeme.size() - 2));
        GcHandle pathHandle = heap.allocateString(pathText);
        uint8_t pathConstant = makeConstant(Value(pathHandle));

        for (const auto& sym : symbols) {
            uint8_t symConstant = identifierConstant(sym);
            emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT_NAME), pathConstant);
            emitByte(symConstant);
        }

        consume(TokenType::SEMICOLON, "Expect ';' after import.");
    } else if (match(TokenType::STRING)) {
        // 支援簡寫形式: import "std/fs"; -> 自動以模組路徑基底名稱 "fs" 註冊
        std::string_view pathLexeme = previous.lexeme;
        std::string pathText = unescapeString(pathLexeme.substr(1, pathLexeme.size() - 2));
        GcHandle pathHandle = heap.allocateString(pathText);
        uint8_t pathConstant = makeConstant(Value(pathHandle));

        std::string modName = pathText;
        size_t slash = modName.find_last_of("/\\");
        if (slash != std::string::npos) modName = modName.substr(slash + 1);
        if (modName.ends_with(".easy") || modName.ends_with(".esc") || modName.ends_with(".ecs") || modName.ends_with(".dll")) {
            modName = modName.substr(0, modName.find_last_of('.'));
        }
        if (modName.starts_with("lib")) modName = modName.substr(3);

        Token nameToken{TokenType::IDENTIFIER, modName, previous.line, previous.column};
        uint8_t nameConstant = identifierConstant(nameToken);

        emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT_MODULE), pathConstant);
        emitByte(nameConstant);

        consume(TokenType::SEMICOLON, "Expect ';' after import.");
    } else if (match(TokenType::IDENTIFIER)) {
        // 模組匯入: import Math from "std/math";
        Token modNameToken = previous;
        uint8_t nameConstant = identifierConstant(modNameToken);

        consume(TokenType::FROM, "Expect 'from' after import name.");
        consume(TokenType::STRING, "Expect module path string.");

        std::string_view pathLexeme = previous.lexeme;
        std::string pathText = unescapeString(pathLexeme.substr(1, pathLexeme.size() - 2));
        GcHandle pathHandle = heap.allocateString(pathText);
        uint8_t pathConstant = makeConstant(Value(pathHandle));

        emitBytes(static_cast<uint8_t>(OpCode::OP_IMPORT_MODULE), pathConstant);
        emitByte(nameConstant);

        consume(TokenType::SEMICOLON, "Expect ';' after import.");
    } else {
        error("Expect '{', string, or module name after 'import'.");
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
    } else if (match(TokenType::BREAK)) {
        breakStatement();
    } else if (match(TokenType::CONTINUE)) {
        continueStatement();
    } else if (match(TokenType::TRY)) {
        tryStatement();
    } else if (match(TokenType::THROW)) {
        throwStatement();
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

    Loop loop;
    loop.enclosing = currentFunction().currentLoop;
    loop.startOffset = loopStart;
    loop.bodyLocalsCount = static_cast<int>(currentFunction().locals.size());
    loop.tryDepth = currentTryDepth;
    currentFunction().currentLoop = &loop;

    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitOp(OpCode::OP_POP);

    for (int breakJump : loop.breakJumps) {
        patchJump(breakJump);
    }

    currentFunction().currentLoop = loop.enclosing;
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

    Loop loop;
    loop.enclosing = currentFunction().currentLoop;
    loop.startOffset = loopStart;
    loop.bodyLocalsCount = static_cast<int>(currentFunction().locals.size());
    loop.tryDepth = currentTryDepth;
    currentFunction().currentLoop = &loop;

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitOp(OpCode::OP_POP);
    }

    for (int breakJump : loop.breakJumps) {
        patchJump(breakJump);
    }

    currentFunction().currentLoop = loop.enclosing;

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

    Loop loop;
    loop.enclosing = currentFunction().currentLoop;
    loop.startOffset = loopStart;
    loop.bodyLocalsCount = static_cast<int>(currentFunction().locals.size());
    loop.tryDepth = currentTryDepth;
    currentFunction().currentLoop = &loop;

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

    for (int breakJump : loop.breakJumps) {
        patchJump(breakJump);
    }

    currentFunction().currentLoop = loop.enclosing;
}

void Compiler::returnStatement() {
    if (currentFunction().type == FunctionType::SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (match(TokenType::SEMICOLON)) {
        if (currentFunction().returnType.kind != TypeKind::ANY &&
            currentFunction().returnType.kind != TypeKind::VOID &&
            currentFunction().returnType.kind != TypeKind::NIL) {
            error("Cannot return void from function with return type '" + currentFunction().returnType.toString() + "'.");
        }
        emitReturn();
    } else {
        if (currentFunction().type == FunctionType::INITIALIZER) {
            error("Can't return a value from an initializer.");
        }
        Type valType = expression();
        if (currentFunction().returnType.kind != TypeKind::ANY &&
            !isAssignable(currentFunction().returnType, valType, classHierarchy)) {
            error("Return type mismatch: expected '" + currentFunction().returnType.toString() +
                  "', got '" + valType.toString() + "'.");
        }
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        emitOp(OpCode::OP_RETURN);
    }
}

void Compiler::breakStatement() {
    if (currentFunction().currentLoop == nullptr) {
        error("Can't use 'break' outside of a loop.");
        return;
    }

    consume(TokenType::SEMICOLON, "Expect ';' after 'break'.");

    Loop* loop = currentFunction().currentLoop;

    for (int t = currentTryDepth; t > loop->tryDepth; --t) {
        emitOp(OpCode::OP_POP_TRY);
    }

    for (int i = static_cast<int>(currentFunction().locals.size()) - 1; i >= loop->bodyLocalsCount; --i) {
        if (currentFunction().locals[i].isCaptured) {
            emitOp(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitOp(OpCode::OP_POP);
        }
    }

    loop->breakJumps.push_back(emitJump(OpCode::OP_JUMP));
}

void Compiler::continueStatement() {
    if (currentFunction().currentLoop == nullptr) {
        error("Can't use 'continue' outside of a loop.");
        return;
    }

    consume(TokenType::SEMICOLON, "Expect ';' after 'continue'.");

    Loop* loop = currentFunction().currentLoop;

    for (int t = currentTryDepth; t > loop->tryDepth; --t) {
        emitOp(OpCode::OP_POP_TRY);
    }

    for (int i = static_cast<int>(currentFunction().locals.size()) - 1; i >= loop->bodyLocalsCount; --i) {
        if (currentFunction().locals[i].isCaptured) {
            emitOp(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitOp(OpCode::OP_POP);
        }
    }

    emitLoop(loop->startOffset);
}

void Compiler::throwStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after throw statement.");
    emitOp(OpCode::OP_THROW);
}

void Compiler::tryStatement() {
    consume(TokenType::LEFT_BRACE, "Expect '{' after 'try'.");

    // 1. 發射 OP_PUSH_TRY 並預留 catchOffset 與 finallyOffset
    emitOp(OpCode::OP_PUSH_TRY);
    emitByte(0xff); emitByte(0xff); // placeholder for catchOffset
    int catchJumpOffset = static_cast<int>(currentFunction().function->bytecode.size() - 2);
    emitByte(0xff); emitByte(0xff); // placeholder for finallyOffset
    int finallyJumpOffset = static_cast<int>(currentFunction().function->bytecode.size() - 2);

    // 2. 編譯 try 區塊
    currentTryDepth++;
    beginScope();
    block();
    endScope();
    currentTryDepth--;

    // try 正常執行完畢：彈出 TryHandler
    emitOp(OpCode::OP_POP_TRY);
    int trySuccessJump = emitJump(OpCode::OP_JUMP);

    bool hasCatch = false;
    int catchSuccessJump = -1;

    // 3. catch 區塊
    if (match(TokenType::CATCH)) {
        hasCatch = true;
        patchJump(catchJumpOffset);

        consume(TokenType::LEFT_PAREN, "Expect '(' after 'catch'.");
        consume(TokenType::IDENTIFIER, "Expect exception variable name in catch.");
        Token errToken = previous;
        skipTypeAnnotation();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after catch variable.");

        consume(TokenType::LEFT_BRACE, "Expect '{' before catch body.");
        beginScope();
        addLocal(errToken);
        markInitialized();

        block();
        endScope();

        catchSuccessJump = emitJump(OpCode::OP_JUMP);
    } else {
        patchJumpTo(catchJumpOffset, 0);
    }

    // 4. finally 區塊
    if (match(TokenType::FINALLY)) {
        patchJump(finallyJumpOffset);

        patchJump(trySuccessJump);
        if (catchSuccessJump != -1) {
            patchJump(catchSuccessJump);
        }

        consume(TokenType::LEFT_BRACE, "Expect '{' before finally body.");
        beginScope();
        block();
        endScope();

        emitOp(OpCode::OP_FINALLY_END);
    } else {
        if (!hasCatch) {
            error("Expect 'catch' or 'finally' after 'try'.");
        }
        patchJumpTo(finallyJumpOffset, 0);

        patchJump(trySuccessJump);
        if (catchSuccessJump != -1) {
            patchJump(catchSuccessJump);
        }
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

Type Compiler::expression() {
    parsePrecedence(Precedence::ASSIGNMENT);
    return lastExpressionType;
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

    if (canAssign && (match(TokenType::EQUAL) ||
                      match(TokenType::PLUS_EQUAL) || match(TokenType::MINUS_EQUAL) ||
                      match(TokenType::STAR_EQUAL) || match(TokenType::SLASH_EQUAL) || match(TokenType::PERCENT_EQUAL))) {
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
        lastExpressionType = Type::Int();
    } else {
        double val = std::strtod(text.data(), nullptr);
        emitConstant(Value(val));
        lastExpressionType = Type::Double();
    }
}

void Compiler::string(bool) {
    std::string_view content = previous.lexeme.substr(1, previous.lexeme.size() - 2);
    GcHandle strHandle = heap.allocateString(unescapeString(content));
    emitConstant(Value(strHandle));
    lastExpressionType = Type::String();
}

void Compiler::literal(bool) {
    switch (previous.type) {
        case TokenType::FALSE: emitOp(OpCode::OP_FALSE); lastExpressionType = Type::Bool(); break;
        case TokenType::TRUE:  emitOp(OpCode::OP_TRUE);  lastExpressionType = Type::Bool(); break;
        case TokenType::NIL:   emitOp(OpCode::OP_NIL);   lastExpressionType = Type::Nil(); break;
        default: return;
    }
}

void Compiler::grouping(bool) {
    lastExpressionType = expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Compiler::unary(bool) {
    TokenType operatorType = previous.type;

    parsePrecedence(Precedence::UNARY);
    Type operandType = lastExpressionType;

    switch (operatorType) {
        case TokenType::BANG:  emitOp(OpCode::OP_NOT);    lastExpressionType = Type::Bool(); break;
        case TokenType::MINUS: emitOp(OpCode::OP_NEGATE); lastExpressionType = (operandType.kind == TypeKind::DOUBLE ? Type::Double() : Type::Int()); break;
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

    std::vector<Type> paramTypes;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentFunction().function->arity++;
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            Type pType = parseTypeAnnotation();
            currentFunction().locals.back().type = pType;
            paramTypes.push_back(pType);
            if (match(TokenType::EQUAL)) {
                Type defType = expression(); // 預設參數
                if (pType.kind != TypeKind::ANY && !isAssignable(pType, defType, classHierarchy)) {
                    error("Default parameter type does not match parameter type.");
                }
            }
            defineVariable(paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

    Type retType = parseTypeAnnotation();
    currentFunction().returnType = retType;

    consume(TokenType::LEFT_BRACE, "Expect '{' before lambda body.");
    block();

    emitReturn();

    FunctionState finished = std::move(functionStack.back());
    functionStack.pop_back();

    emitClosure(finished);
    lastExpressionType = Type::Function(paramTypes, retType);
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
    lastExpressionType = Type::Any();
}

void Compiler::namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(name);
    Type varType = Type::Any();

    if (arg != -1) {
        lastResolvedLocalSlot = arg;
        lastResolvedUpvalueSlot = -1;
        lastResolvedGlobalConstant = -1;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_LOCAL);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_LOCAL);
        varType = currentFunction().locals[arg].type;
    } else if ((arg = resolveUpvalue(functionStack.size() - 1, name)) != -1) {
        lastResolvedLocalSlot = -1;
        lastResolvedUpvalueSlot = arg;
        lastResolvedGlobalConstant = -1;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_UPVALUE);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_UPVALUE);
        varType = Type::Any();
    } else {
        arg = identifierConstant(name);
        lastResolvedLocalSlot = -1;
        lastResolvedUpvalueSlot = -1;
        lastResolvedGlobalConstant = arg;
        getOp = static_cast<uint8_t>(OpCode::OP_GET_GLOBAL);
        setOp = static_cast<uint8_t>(OpCode::OP_SET_GLOBAL);
        auto it = globalTypes.find(std::string(name.lexeme));
        if (it != globalTypes.end()) {
            varType = it->second;
        }
    }

    if (canAssign && match(TokenType::EQUAL)) {
        Type valType = expression();
        if (varType.kind != TypeKind::ANY && !isAssignable(varType, valType, classHierarchy)) {
            error("Type mismatch: cannot assign value of type '" + valType.toString() +
                  "' to variable '" + std::string(name.lexeme) + "' of type '" + varType.toString() + "'.");
        }
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = valType;
    } else if (canAssign && match(TokenType::PLUS_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        Type valType = expression();
        emitOp(OpCode::OP_ADD);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
    } else if (canAssign && match(TokenType::MINUS_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        Type valType = expression();
        emitOp(OpCode::OP_SUB);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
    } else if (canAssign && match(TokenType::STAR_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        Type valType = expression();
        emitOp(OpCode::OP_MUL);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
    } else if (canAssign && match(TokenType::SLASH_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        Type valType = expression();
        emitOp(OpCode::OP_DIV);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
    } else if (canAssign && match(TokenType::PERCENT_EQUAL)) {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        Type valType = expression();
        emitOp(OpCode::OP_MOD);
        emitBytes(setOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
    } else {
        emitBytes(getOp, static_cast<uint8_t>(arg));
        lastExpressionType = varType;
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
    lastExpressionType = Type::Int();
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
    lastExpressionType = Type::Int();
}

void Compiler::arrayLiteral(bool) {
    uint16_t count = 0;
    Type elemType = Type::Any();
    bool first = true;
    if (!check(TokenType::RIGHT_BRACKET)) {
        do {
            Type t = expression();
            if (first) {
                elemType = t;
                first = false;
            } else if (elemType.kind != t.kind) {
                elemType = Type::Any();
            }
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after array elements.");

    emitOp(OpCode::OP_BUILD_ARRAY);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(count & 0xff));
    lastExpressionType = Type::Array(elemType);
}

void Compiler::mapLiteral(bool) {
    uint16_t count = 0;
    Type keyType = Type::Any();
    Type valType = Type::Any();
    bool first = true;
    if (!check(TokenType::RIGHT_BRACE)) {
        do {
            Type kt = expression(); // Key
            consume(TokenType::COLON, "Expect ':' between key and value.");
            Type vt = expression(); // Value
            if (first) {
                keyType = kt;
                valType = vt;
                first = false;
            }
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after map entries.");

    emitOp(OpCode::OP_BUILD_MAP);
    emitByte(static_cast<uint8_t>((count >> 8) & 0xff));
    emitByte(static_cast<uint8_t>(count & 0xff));
    lastExpressionType = Type::Map(keyType, valType);
}

void Compiler::interpolation(bool) {
    std::string_view head = previous.lexeme;
    std::string_view prefix = head.substr(1, head.size() - 3);
    GcHandle headStr = heap.allocateString(unescapeString(prefix));
    emitConstant(Value(headStr));

    expression();
    emitOp(OpCode::OP_STRING_CONCAT);

    while (match(TokenType::INTERPOLATION_MID)) {
        std::string_view mid = previous.lexeme;
        std::string_view midText = mid.substr(1, mid.size() - 3);
        GcHandle midStr = heap.allocateString(unescapeString(midText));
        emitConstant(Value(midStr));
        emitOp(OpCode::OP_STRING_CONCAT);

        expression();
        emitOp(OpCode::OP_STRING_CONCAT);
    }

    if (match(TokenType::INTERPOLATION_TAIL)) {
        std::string_view tail = previous.lexeme;
        std::string_view suffix = tail.substr(1, tail.size() - 2);
        GcHandle tailStr = heap.allocateString(unescapeString(suffix));
        emitConstant(Value(tailStr));
        emitOp(OpCode::OP_STRING_CONCAT);
    }
    lastExpressionType = Type::String();
}

// -----------------------------------------------------------------------------
// 中綴剖析 (Infix Parselets)
// -----------------------------------------------------------------------------

void Compiler::binary(bool) {
    TokenType operatorType = previous.type;
    Type leftType = lastExpressionType;
    const ParseRule& rule = getRule(operatorType);
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule.precedence) + 1));
    Type rightType = lastExpressionType;

    switch (operatorType) {
        case TokenType::PLUS:
            emitOp(OpCode::OP_ADD);
            if (leftType.kind == TypeKind::STRING || rightType.kind == TypeKind::STRING) {
                lastExpressionType = Type::String();
            } else if (leftType.kind == TypeKind::DOUBLE || rightType.kind == TypeKind::DOUBLE) {
                lastExpressionType = Type::Double();
            } else if (leftType.kind == TypeKind::INT && rightType.kind == TypeKind::INT) {
                lastExpressionType = Type::Int();
            } else {
                lastExpressionType = Type::Any();
            }
            break;
        case TokenType::MINUS:
            emitOp(OpCode::OP_SUB);
            if (leftType.kind == TypeKind::DOUBLE || rightType.kind == TypeKind::DOUBLE) {
                lastExpressionType = Type::Double();
            } else if (leftType.kind == TypeKind::INT && rightType.kind == TypeKind::INT) {
                lastExpressionType = Type::Int();
            } else {
                lastExpressionType = Type::Any();
            }
            break;
        case TokenType::STAR:
            emitOp(OpCode::OP_MUL);
            if (leftType.kind == TypeKind::DOUBLE || rightType.kind == TypeKind::DOUBLE) {
                lastExpressionType = Type::Double();
            } else if (leftType.kind == TypeKind::INT && rightType.kind == TypeKind::INT) {
                lastExpressionType = Type::Int();
            } else {
                lastExpressionType = Type::Any();
            }
            break;
        case TokenType::SLASH:
            emitOp(OpCode::OP_DIV);
            if (leftType.kind == TypeKind::DOUBLE || rightType.kind == TypeKind::DOUBLE) {
                lastExpressionType = Type::Double();
            } else if (leftType.kind == TypeKind::INT && rightType.kind == TypeKind::INT) {
                lastExpressionType = Type::Int();
            } else {
                lastExpressionType = Type::Any();
            }
            break;
        case TokenType::PERCENT:
            emitOp(OpCode::OP_MOD);
            lastExpressionType = Type::Int();
            break;
        case TokenType::BANG_EQUAL:
            emitBytes(static_cast<uint8_t>(OpCode::OP_EQUAL), static_cast<uint8_t>(OpCode::OP_NOT));
            lastExpressionType = Type::Bool();
            break;
        case TokenType::EQUAL_EQUAL:
            emitOp(OpCode::OP_EQUAL);
            lastExpressionType = Type::Bool();
            break;
        case TokenType::GREATER:
            emitOp(OpCode::OP_GREATER);
            lastExpressionType = Type::Bool();
            break;
        case TokenType::GREATER_EQUAL:
            emitBytes(static_cast<uint8_t>(OpCode::OP_LESS), static_cast<uint8_t>(OpCode::OP_NOT));
            lastExpressionType = Type::Bool();
            break;
        case TokenType::LESS:
            emitOp(OpCode::OP_LESS);
            lastExpressionType = Type::Bool();
            break;
        case TokenType::LESS_EQUAL:
            emitBytes(static_cast<uint8_t>(OpCode::OP_GREATER), static_cast<uint8_t>(OpCode::OP_NOT));
            lastExpressionType = Type::Bool();
            break;
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
    Type calleeType = lastExpressionType;
    uint8_t argCount = argumentList();
    emitBytes(static_cast<uint8_t>(OpCode::OP_CALL), argCount);
    if (calleeType.kind == TypeKind::CLASS) {
        lastExpressionType = calleeType;
    } else if (calleeType.kind == TypeKind::FUNCTION && calleeType.returnType) {
        lastExpressionType = *calleeType.returnType;
    } else {
        lastExpressionType = Type::Any();
    }
}

void Compiler::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(previous);

    if (canAssign && match(TokenType::EQUAL)) {
        Type valType = expression();
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_PROPERTY), name);
        lastExpressionType = valType;
    } else if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(static_cast<uint8_t>(OpCode::OP_INVOKE), name);
        emitByte(argCount);
        lastExpressionType = Type::Any();
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_PROPERTY), name);
        lastExpressionType = Type::Any();
    }
}

void Compiler::subscript(bool canAssign) {
    Type receiverType = lastExpressionType;
    Type indexType = expression();
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && match(TokenType::EQUAL)) {
        Type valType = expression();
        emitOp(OpCode::OP_SET_INDEX);
        lastExpressionType = valType;
    } else {
        emitOp(OpCode::OP_GET_INDEX);
        if (receiverType.kind == TypeKind::ARRAY && receiverType.elementType) {
            lastExpressionType = *receiverType.elementType;
        } else if (receiverType.kind == TypeKind::MAP && receiverType.valueType) {
            lastExpressionType = *receiverType.valueType;
        } else if (receiverType.kind == TypeKind::STRING) {
            lastExpressionType = Type::String();
        } else {
            lastExpressionType = Type::Any();
        }
    }
}

void Compiler::range(bool) {
    parsePrecedence(Precedence::RANGE);
    emitOp(OpCode::OP_BUILD_RANGE);
    lastExpressionType = Type::Array(Type::Int());
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
