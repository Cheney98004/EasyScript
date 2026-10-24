#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include "lsp/mini_json.hpp"

namespace fs = std::filesystem;
using Json = lsp::Json;
using JsonType = lsp::JsonType;
using JsonParser = lsp::JsonParser;

namespace epm {

inline std::string dumpPretty(const Json& j, int indentLevel = 0, int spacesPerIndent = 2) {
    std::string indent(indentLevel * spacesPerIndent, ' ');
    std::string nextIndent((indentLevel + 1) * spacesPerIndent, ' ');

    switch (j.type) {
        case JsonType::Null: return "null";
        case JsonType::Bool: return j.boolVal ? "true" : "false";
        case JsonType::Number:
            if (j.isInteger) return std::to_string(j.intVal);
            {
                std::ostringstream ss;
                ss << j.numVal;
                return ss.str();
            }
        case JsonType::String: {
            std::ostringstream ss;
            ss << '"';
            for (char c : j.strVal) {
                switch (c) {
                    case '"': ss << "\\\""; break;
                    case '\\': ss << "\\\\"; break;
                    case '\b': ss << "\\b"; break;
                    case '\f': ss << "\\f"; break;
                    case '\n': ss << "\\n"; break;
                    case '\r': ss << "\\r"; break;
                    case '\t': ss << "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                            ss << buf;
                        } else {
                            ss << c;
                        }
                        break;
                }
            }
            ss << '"';
            return ss.str();
        }
        case JsonType::Array: {
            if (j.arrVal.empty()) return "[]";
            std::ostringstream ss;
            ss << "[\n";
            for (size_t i = 0; i < j.arrVal.size(); ++i) {
                ss << nextIndent << dumpPretty(j.arrVal[i], indentLevel + 1, spacesPerIndent);
                if (i + 1 < j.arrVal.size()) ss << ",";
                ss << "\n";
            }
            ss << indent << "]";
            return ss.str();
        }
        case JsonType::Object: {
            if (j.objVal.empty()) return "{}";
            std::ostringstream ss;
            ss << "{\n";
            size_t idx = 0;
            for (const auto& [k, v] : j.objVal) {
                ss << nextIndent << '"' << k << "\": " << dumpPretty(v, indentLevel + 1, spacesPerIndent);
                if (++idx < j.objVal.size()) ss << ",";
                ss << "\n";
            }
            ss << indent << "}";
            return ss.str();
        }
    }
    return "null";
}

class EpmManager {
public:
    static fs::path findProjectRoot(const fs::path& start = fs::current_path()) {
        fs::path p = fs::absolute(start);
        std::error_code ec;
        while (true) {
            if (fs::exists(p / "easy.json", ec)) {
                return p;
            }
            if (!p.has_parent_path() || p.parent_path() == p) {
                break;
            }
            p = p.parent_path();
        }
        return fs::current_path();
    }

    static bool copyDirRecursive(const fs::path& source, const fs::path& target) {
        std::error_code ec;
        if (!fs::exists(source, ec) || !fs::is_directory(source, ec)) {
            return false;
        }

        fs::create_directories(target, ec);

        for (const auto& entry : fs::recursive_directory_iterator(source, fs::directory_options::skip_permission_denied, ec)) {
            const auto& item = entry.path();
            auto rel = fs::relative(item, source, ec);
            std::string relStr = rel.generic_string();

            // 過濾暫存、版本控制與構建目錄
            if (relStr.starts_with(".git") || relStr.starts_with("build") ||
                relStr.starts_with("easy_modules") || relStr.starts_with(".vscode") ||
                relStr.starts_with(".idea")) {
                continue;
            }

            fs::path dest = target / rel;
            if (entry.is_directory(ec)) {
                fs::create_directories(dest, ec);
            } else if (entry.is_regular_file(ec)) {
                if (dest.has_parent_path()) {
                    fs::create_directories(dest.parent_path(), ec);
                }
                fs::copy_file(item, dest, fs::copy_options::overwrite_existing, ec);
            }
        }
        return true;
    }

    static Json readJson(const fs::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) return Json();
        std::stringstream ss;
        ss << file.rdbuf();
        try {
            return JsonParser::parse(ss.str());
        } catch (...) {
            return Json();
        }
    }

    static bool writeJson(const fs::path& path, const Json& j) {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << dumpPretty(j, 0, 2) << "\n";
        return true;
    }

    static int init(int argc, char* argv[]) {
        bool autoYes = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-y" || arg == "--yes") {
                autoYes = true;
            }
        }
        (void)autoYes;

        fs::path easyJsonPath = fs::current_path() / "easy.json";
        std::error_code ec;
        if (fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: easy.json already exists in " << fs::current_path().string() << std::endl;
            return 1;
        }

        std::string pkgName = fs::current_path().filename().string();
        if (pkgName.empty() || pkgName == ".") {
            pkgName = "my-easyscript-app";
        }

        Json root = Json::object();
        root["name"] = Json(pkgName);
        root["version"] = Json("1.0.0");
        root["description"] = Json("An EasyScript project");
        root["main"] = Json("src/main.easy");

        Json scripts = Json::object();
        scripts["start"] = Json("easyscript src/main.easy");
        scripts["test"] = Json("echo \"No tests specified\"");
        root["scripts"] = scripts;

        root["dependencies"] = Json::object();
        root["devDependencies"] = Json::object();

        if (!writeJson(easyJsonPath, root)) {
            std::cerr << "[epm] Failed to write easy.json" << std::endl;
            return 1;
        }

        fs::path srcDir = fs::current_path() / "src";
        if (!fs::exists(srcDir, ec)) {
            fs::create_directories(srcDir, ec);
        }

        fs::path mainEasy = srcDir / "main.easy";
        if (!fs::exists(mainEasy, ec)) {
            std::ofstream mf(mainEasy);
            mf << "// EasyScript Application Entry Point\n";
            mf << "print(\"Hello from EasyScript project: " << pkgName << "!\");\n";
            mf.close();
        }

        std::cout << "[epm] Success! Initialized EasyScript package in " << fs::current_path().string() << "\n";
        std::cout << "      + Created easy.json\n";
        std::cout << "      + Created src/main.easy\n\n";
        std::cout << "You can run your project with:\n";
        std::cout << "  epm run start\n";

        return 0;
    }

    static int add(int argc, char* argv[]) {
        if (argc < 3) {
            std::cerr << "Usage: epm add <package-name> [--path <path>] [--dev]" << std::endl;
            return 1;
        }

        std::string rawTarget = argv[2];
        std::string localPath;
        bool isDev = false;

        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--path" || arg == "-p") && i + 1 < argc) {
                localPath = argv[++i];
            } else if (arg == "--dev" || arg == "-D") {
                isDev = true;
            }
        }

        fs::path projectRoot = findProjectRoot();
        fs::path easyJsonPath = projectRoot / "easy.json";
        std::error_code ec;

        if (!fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: No easy.json found. Run 'epm init' first." << std::endl;
            return 1;
        }

        Json manifest = readJson(easyJsonPath);
        if (!manifest.isObject()) {
            std::cerr << "[epm] Error: Invalid easy.json manifest." << std::endl;
            return 1;
        }

        // 判斷 target 是否本身即為本地路徑
        std::string pkgName = rawTarget;
        fs::path sourcePath;

        if (!localPath.empty()) {
            sourcePath = fs::absolute(projectRoot / localPath, ec);
        } else {
            // 嘗試檢查是否為路徑格式 (如 ../pkg 或 ./sub/pkg)
            fs::path asPath(rawTarget);
            if (rawTarget.find('/') != std::string::npos || rawTarget.find('\\') != std::string::npos || fs::exists(projectRoot / asPath, ec)) {
                sourcePath = fs::absolute(projectRoot / asPath, ec);
                pkgName = asPath.filename().string();
                localPath = rawTarget;
            }
        }

        if (sourcePath.empty() || !fs::exists(sourcePath, ec)) {
            std::cerr << "[epm] Error: Dependency source path not found: " 
                      << (localPath.empty() ? rawTarget : localPath) << "\n"
                      << "       (Phase 1 MVP currently supports local path dependencies with --path <path>)" << std::endl;
            return 1;
        }

        // 讀取套件自身資訊
        std::string pkgVersion = "1.0.0";
        fs::path pkgEasyJson = sourcePath / "easy.json";
        if (fs::exists(pkgEasyJson, ec)) {
            Json subManifest = readJson(pkgEasyJson);
            if (subManifest.contains("name") && subManifest["name"].isString()) {
                pkgName = subManifest["name"].asString();
            }
            if (subManifest.contains("version") && subManifest["version"].isString()) {
                pkgVersion = subManifest["version"].asString();
            }
        }

        // 安裝至 easy_modules/<pkgName>
        fs::path destDir = projectRoot / "easy_modules" / pkgName;
        std::cout << "[epm] Installing " << pkgName << "@" << pkgVersion << " -> ./easy_modules/" << pkgName << " ...\n";

        if (!copyDirRecursive(sourcePath, destDir)) {
            std::cerr << "[epm] Failed to copy files from " << sourcePath.string() << " to " << destDir.string() << std::endl;
            return 1;
        }

        // 更新 easy.json
        std::string depKey = isDev ? "devDependencies" : "dependencies";
        if (!manifest.contains(depKey) || !manifest[depKey].isObject()) {
            manifest[depKey] = Json::object();
        }

        Json depEntry = Json::object();
        fs::path relPath = fs::relative(sourcePath, projectRoot, ec);
        depEntry["path"] = Json(relPath.generic_string());
        depEntry["version"] = Json(pkgVersion);

        manifest[depKey][pkgName] = depEntry;
        writeJson(easyJsonPath, manifest);

        // 更新 easy-lock.json
        updateLockfile(projectRoot, pkgName, pkgVersion, "path:" + relPath.generic_string());

        std::cout << "[epm] ✔ Added \"" << pkgName << "\": " << pkgVersion << " to " << depKey << "\n";
        return 0;
    }

    static int install(int, char*[]) {
        fs::path projectRoot = findProjectRoot();
        fs::path easyJsonPath = projectRoot / "easy.json";
        std::error_code ec;

        if (!fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: No easy.json found. Run 'epm init' first." << std::endl;
            return 1;
        }

        Json manifest = readJson(easyJsonPath);
        if (!manifest.isObject()) {
            std::cerr << "[epm] Error: Invalid easy.json manifest." << std::endl;
            return 1;
        }

        std::cout << "[epm] Resolving dependencies for " 
                  << (manifest.contains("name") ? manifest["name"].asString() : "project") << " ...\n";

        fs::path modulesDir = projectRoot / "easy_modules";
        fs::create_directories(modulesDir, ec);

        int installCount = 0;
        auto processSection = [&](const std::string& sectionName) {
            if (!manifest.contains(sectionName) || !manifest[sectionName].isObject()) return;
            for (const auto& [pkgName, spec] : manifest[sectionName].objVal) {
                fs::path sourcePath;
                std::string version = "1.0.0";

                if (spec.isObject() && spec.contains("path") && spec["path"].isString()) {
                    sourcePath = fs::absolute(projectRoot / spec["path"].asString(), ec);
                    if (spec.contains("version") && spec["version"].isString()) {
                        version = spec["version"].asString();
                    }
                } else if (spec.isString()) {
                    version = spec.asString();
                    // 檢查鄰近目錄中是否有同名套件 (例如 ../pkgName)
                    fs::path cand = projectRoot.parent_path() / pkgName;
                    if (fs::exists(cand, ec)) {
                        sourcePath = cand;
                    }
                }

                if (!sourcePath.empty() && fs::exists(sourcePath, ec)) {
                    fs::path dest = modulesDir / pkgName;
                    std::cout << "  + [" << sectionName << "] " << pkgName << "@" << version << "\n";
                    if (copyDirRecursive(sourcePath, dest)) {
                        installCount++;
                        fs::path rel = fs::relative(sourcePath, projectRoot, ec);
                        updateLockfile(projectRoot, pkgName, version, "path:" + rel.generic_string());
                    }
                } else {
                    std::cerr << "  ! Warning: Unable to resolve source for package '" << pkgName << "'\n";
                }
            }
        };

        processSection("dependencies");
        processSection("devDependencies");

        std::cout << "[epm] ✔ Successfully installed " << installCount << " package(s) into ./easy_modules/\n";
        return 0;
    }

    static int remove(int argc, char* argv[]) {
        if (argc < 3) {
            std::cerr << "Usage: epm remove <package-name>" << std::endl;
            return 1;
        }

        std::string pkgName = argv[2];
        fs::path projectRoot = findProjectRoot();
        fs::path easyJsonPath = projectRoot / "easy.json";
        std::error_code ec;

        if (!fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: No easy.json found." << std::endl;
            return 1;
        }

        Json manifest = readJson(easyJsonPath);
        if (!manifest.isObject()) {
            std::cerr << "[epm] Error: Invalid easy.json manifest." << std::endl;
            return 1;
        }

        bool removed = false;
        if (manifest.contains("dependencies") && manifest["dependencies"].contains(pkgName)) {
            manifest["dependencies"].objVal.erase(pkgName);
            removed = true;
        }
        if (manifest.contains("devDependencies") && manifest["devDependencies"].contains(pkgName)) {
            manifest["devDependencies"].objVal.erase(pkgName);
            removed = true;
        }

        if (removed) {
            writeJson(easyJsonPath, manifest);
            fs::path modPath = projectRoot / "easy_modules" / pkgName;
            if (fs::exists(modPath, ec)) {
                fs::remove_all(modPath, ec);
            }
            removeFromLockfile(projectRoot, pkgName);
            std::cout << "[epm] ✔ Removed \"" << pkgName << "\" from dependencies and ./easy_modules/" << std::endl;
        } else {
            std::cout << "[epm] Package \"" << pkgName << "\" was not listed in dependencies." << std::endl;
        }

        return 0;
    }

    static int run(int argc, char* argv[]) {
        fs::path projectRoot = findProjectRoot();
        fs::path easyJsonPath = projectRoot / "easy.json";
        std::error_code ec;

        if (!fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: No easy.json found in current or parent directories." << std::endl;
            return 1;
        }

        Json manifest = readJson(easyJsonPath);
        if (!manifest.isObject() || !manifest.contains("scripts") || !manifest["scripts"].isObject()) {
            std::cerr << "[epm] Error: No \"scripts\" section found in easy.json." << std::endl;
            return 1;
        }

        const auto& scripts = manifest["scripts"].objVal;

        if (argc < 3) {
            std::cout << "Available scripts in " << (manifest.contains("name") ? manifest["name"].asString() : "project") << ":\n";
            for (const auto& [name, cmd] : scripts) {
                std::cout << "  " << name << "\n    " << (cmd.isString() ? cmd.asString() : "") << "\n";
            }
            std::cout << "\nUsage: epm run <script-name> [-- extra args]\n";
            return 0;
        }

        std::string scriptName = argv[2];
        auto it = scripts.find(scriptName);
        if (it == scripts.end() || !it->second.isString()) {
            std::cerr << "[epm] Error: Missing script \"" << scriptName << "\".\nAvailable scripts:\n";
            for (const auto& [name, _] : scripts) {
                std::cerr << "  - " << name << "\n";
            }
            return 1;
        }

        std::string cmd = it->second.asString();
        for (int i = 3; i < argc; ++i) {
            cmd += " ";
            cmd += argv[i];
        }

        // 自動將專案之 easy_modules, lib, plugins 設定至環境變數 EASYSCRIPT_PATH
        std::string appendPath = (projectRoot / "easy_modules").generic_string() + ";" +
                                 (projectRoot / "lib").generic_string() + ";" +
                                 (projectRoot / "plugins").generic_string();

        const char* currentEnv = std::getenv("EASYSCRIPT_PATH");
        std::string newEnv = currentEnv ? (std::string(currentEnv) + ";" + appendPath) : appendPath;

        // 若 epm 所在目錄含有 easyscript 執行檔，自動加進 PATH 以便執行 scripts
        std::error_code ecSelf;
        fs::path selfDir = fs::absolute(fs::path(argv[0]), ecSelf).parent_path();
        const char* currentSysPath = std::getenv("PATH");
        std::string newSysPath = (!ecSelf && !selfDir.empty()) ? (selfDir.string() + ";" + (currentSysPath ? std::string(currentSysPath) : "")) : (currentSysPath ? std::string(currentSysPath) : "");

#if defined(_WIN32)
        _putenv_s("EASYSCRIPT_PATH", newEnv.c_str());
        if (!newSysPath.empty()) {
            _putenv_s("PATH", newSysPath.c_str());
        }
#else
        setenv("EASYSCRIPT_PATH", newEnv.c_str(), 1);
        if (!newSysPath.empty()) {
            setenv("PATH", newSysPath.c_str(), 1);
        }
#endif

        std::cout << "> " << cmd << "\n\n";
        int ret = std::system(cmd.c_str());
        return ret;
    }

    static int list(int, char*[]) {
        fs::path projectRoot = findProjectRoot();
        fs::path easyJsonPath = projectRoot / "easy.json";
        std::error_code ec;

        if (!fs::exists(easyJsonPath, ec)) {
            std::cerr << "[epm] Error: No easy.json found." << std::endl;
            return 1;
        }

        Json manifest = readJson(easyJsonPath);
        std::string name = manifest.contains("name") ? manifest["name"].asString() : "unnamed";
        std::string ver = manifest.contains("version") ? manifest["version"].asString() : "0.0.0";

        std::cout << name << "@" << ver << " " << projectRoot.string() << "\n";

        auto showSection = [&](const std::string& title, const std::string& key) {
            if (manifest.contains(key) && manifest[key].isObject() && !manifest[key].objVal.empty()) {
                std::cout << "├── " << title << ":\n";
                for (const auto& [pkg, spec] : manifest[key].objVal) {
                    fs::path modPath = projectRoot / "easy_modules" / pkg;
                    bool installed = fs::exists(modPath, ec);
                    std::cout << "│   ├── " << pkg;
                    if (spec.isObject() && spec.contains("version")) {
                        std::cout << "@" << spec["version"].asString();
                    } else if (spec.isString()) {
                        std::cout << "@" << spec.asString();
                    }
                    if (installed) {
                        std::cout << " (installed)";
                    } else {
                        std::cout << " (MISSING - run 'epm install')";
                    }
                    std::cout << "\n";
                }
            }
        };

        showSection("dependencies", "dependencies");
        showSection("devDependencies", "devDependencies");
        return 0;
    }

    static void printHelp() {
        std::cout << "EasyScript Package Manager (epm) - Version 0.1.0\n\n";
        std::cout << "Usage:\n";
        std::cout << "  epm <command> [options]\n\n";
        std::cout << "Commands:\n";
        std::cout << "  init [-y]                   Initialize a new EasyScript project with easy.json\n";
        std::cout << "  add <pkg> [--path <p>] [-D] Add and install a module dependency\n";
        std::cout << "  install, i                  Install all dependencies specified in easy.json\n";
        std::cout << "  remove, rm <pkg>            Remove a module dependency\n";
        std::cout << "  run <script>                Run a script defined in easy.json\n";
        std::cout << "  list, ls                    List installed package dependencies\n";
        std::cout << "  --version, -v               Print epm version\n";
        std::cout << "  --help, -h                  Show this help message\n\n";
        std::cout << "Examples:\n";
        std::cout << "  epm init -y\n";
        std::cout << "  epm add my_utils --path ../my_utils\n";
        std::cout << "  epm run start\n";
    }

    static void printVersion() {
        std::cout << "epm version 0.1.0 (EasyScript Package Manager)\n";
    }

private:
    static void updateLockfile(const fs::path& root, const std::string& name, const std::string& version, const std::string& resolved) {
        fs::path lockPath = root / "easy-lock.json";
        Json lock = readJson(lockPath);
        if (!lock.isObject()) {
            lock = Json::object();
            lock["lockfileVersion"] = Json(1);
            lock["packages"] = Json::object();
        }
        if (!lock.contains("packages") || !lock["packages"].isObject()) {
            lock["packages"] = Json::object();
        }

        Json entry = Json::object();
        entry["version"] = Json(version);
        entry["resolved"] = Json(resolved);

        lock["packages"][name] = entry;
        writeJson(lockPath, lock);
    }

    static void removeFromLockfile(const fs::path& root, const std::string& name) {
        fs::path lockPath = root / "easy-lock.json";
        Json lock = readJson(lockPath);
        if (lock.isObject() && lock.contains("packages") && lock["packages"].isObject()) {
            lock["packages"].objVal.erase(name);
            writeJson(lockPath, lock);
        }
    }
};

} // namespace epm

