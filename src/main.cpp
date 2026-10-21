#include "vm.hpp"
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

int runFile(VM& vm, const std::string& path) {
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

int main(int argc, char* argv[]) {
    VM vm;

    if (argc == 1) {
        repl(vm);
    } else if (argc == 2) {
        return runFile(vm, argv[1]);
    } else if (argc == 3 && std::string(argv[1]) == "run") {
        return runFile(vm, argv[2]);
    } else {
        std::cerr << "Usage: easyscript [run] [path]" << std::endl;
        return 64;
    }

    return 0;
}
