#include "vm.hpp"
#include "compiler.hpp"
#include "serializer.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

void repl(VM& vm) {
    std::cout << "EasyScript 0.1.0 Interactive REPL" << std::endl;
    std::cout << "Type 'exit' to quit.\n" << std::endl;

    std::string line;
    while (true) {
        std::cout << "easy> ";
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;
        }
        if (line == "exit" || line == "quit") {
            break;
        }
        if (line.empty()) {
            continue;
        }
        vm.interpret(line, "<repl>");
    }
}

static bool isBytecodeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    char magic[4];
    file.read(magic, 4);
    return file.gcount() == 4 && magic[0] == 'E' && magic[1] == 'S' && magic[2] == 'C' && magic[3] == '\x01';
}

int runFile(VM& vm, const std::string& path) {
    if (isBytecodeFile(path)) {
        ObjFunction* fn = BytecodeDeserializer::deserializeFromFile(vm.getHeap(), path);
        if (fn == nullptr) {
            std::cerr << "Failed to load bytecode file: " << path << std::endl;
            return 65;
        }
        InterpretResult result = vm.execute(fn, path);
        if (result == InterpretResult::COMPILE_ERROR) return 65;
        if (result == InterpretResult::RUNTIME_ERROR) return 70;
        return 0;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << path << std::endl;
        return 74;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    InterpretResult result = vm.interpret(source, path);
    if (result == InterpretResult::COMPILE_ERROR) return 65;
    if (result == InterpretResult::RUNTIME_ERROR) return 70;
    return 0;
}

int compileFile(const std::string& sourcePath, const std::string& outputPath) {
    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << sourcePath << std::endl;
        return 74;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    Heap heap;
    Compiler compiler(heap, source);
    ObjFunction* fn = compiler.compile();
    if (fn == nullptr) {
        std::cerr << "Compilation failed for " << sourcePath << std::endl;
        return 65;
    }

    if (!BytecodeSerializer::serializeToFile(fn, heap, outputPath)) {
        std::cerr << "Failed to write bytecode to " << outputPath << std::endl;
        return 74;
    }

    std::cout << "Successfully compiled " << sourcePath << " -> " << outputPath << std::endl;
    return 0;
}

static std::string defaultEscPath(const std::string& path) {
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        return path.substr(0, dotPos) + ".esc";
    }
    return path + ".esc";
}

int main(int argc, char* argv[]) {
    VM vm;

    if (argc == 1) {
        repl(vm);
    } else if (argc == 2) {
        return runFile(vm, argv[1]);
    } else if (argc >= 3) {
        std::string command = argv[1];
        if (command == "run") {
            return runFile(vm, argv[2]);
        } else if (command == "compile" || command == "-c") {
            std::string sourcePath = argv[2];
            std::string outputPath;
            if (argc >= 5 && std::string(argv[3]) == "-o") {
                outputPath = argv[4];
            } else {
                outputPath = defaultEscPath(sourcePath);
            }
            return compileFile(sourcePath, outputPath);
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            std::cerr << "Usage: easyscript [run|compile] [options] <path>" << std::endl;
            return 64;
        }
    } else {
        std::cerr << "Usage: easyscript [run|compile] [options] <path>" << std::endl;
        return 64;
    }

    return 0;
}
