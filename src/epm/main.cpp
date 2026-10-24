#include "epm.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        epm::EpmManager::printHelp();
        return 0;
    }

    std::string command = argv[1];

    if (command == "init") {
        return epm::EpmManager::init(argc, argv);
    } else if (command == "add") {
        return epm::EpmManager::add(argc, argv);
    } else if (command == "install" || command == "i") {
        return epm::EpmManager::install(argc, argv);
    } else if (command == "remove" || command == "rm" || command == "uninstall") {
        return epm::EpmManager::remove(argc, argv);
    } else if (command == "run") {
        return epm::EpmManager::run(argc, argv);
    } else if (command == "list" || command == "ls") {
        return epm::EpmManager::list(argc, argv);
    } else if (command == "--help" || command == "-h" || command == "help") {
        epm::EpmManager::printHelp();
        return 0;
    } else if (command == "--version" || command == "-v" || command == "version") {
        epm::EpmManager::printVersion();
        return 0;
    } else {
        std::cerr << "[epm] Unknown command: " << command << "\n\n";
        epm::EpmManager::printHelp();
        return 1;
    }
}
