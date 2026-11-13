// Requires /std:c++20 (MSVC) or -std=c++20 (GCC/Clang)
//
// Core logic is now in library.h. This file contains only the console UI (main).
#include "library.h"
#include <charconv>
#include <sstream>

// ─────────────────────────────────────────────────────────────────────────────
// Console-only helpers (not in library.h — GUI uses Slint instead)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
static inline void clearConsole() { system("cls"); }
#else
static inline void clearConsole() { system("clear"); }
#endif

#ifdef _WIN32
static inline void setColor(WORD color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
#else
static inline void setColor(int color) {
    std::cout << "\033[" << color << "m";
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Console main
// ─────────────────────────────────────────────────────────────────────────────

static void printFileIssues(const std::vector<extraction::FileIssue>& issues) {
    for (const auto& issue : issues) {
        std::cerr << std::format("Skipped {}: {}\n", extraction::pathText(issue.path), issue.message);
    }
}

static std::optional<std::vector<std::string>> readSymbolChoices(const std::vector<std::string>& names) {
    std::string line;
    if (!std::getline(std::cin, line)) return std::nullopt;
    std::ranges::replace(line, ',', ' ');
    std::istringstream input(line);
    std::vector<std::string> selected;
    std::string token;
    while (input >> token) {
        if (token == "0" && selected.empty()) {
            input >> std::ws;
            if (input.eof()) return std::nullopt;
        }
        size_t index = 0;
        const auto end = token.data() + token.size();
        const auto [parsed, error] = std::from_chars(token.data(), end, index);
        if (error != std::errc{} || parsed != end || index == 0 || index > names.size()) {
            return std::vector<std::string>{};
        }
        selected.push_back(names[index - 1]);
    }
    return selected;
}

static void runExtraction(const fs::path& root, const std::vector<std::string>& symbols,
                          extraction::Language language, std::string_view prefix) {
    try {
        auto files = findSourceFiles(root, language);
        printFileIssues(files.issues);
        if (files.paths.empty()) {
            std::cerr << std::format("No source files found in {}.\n", extraction::pathText(root));
            return;
        }
        const auto threads = std::max(1u, std::thread::hardware_concurrency() / 2);
        const auto result = extraction::scan(std::move(files.paths), symbols, language, threads,
            [](size_t completed, size_t total) {
                std::cout << std::format("\rScanning {}/{} files", completed, total) << std::flush;
            });
        std::cout << '\n';
        printFileIssues(result.issues);
        std::cout << std::format("{} files processed, {} read or discovery errors.\n",
                                result.processed, files.issues.size() + result.issues.size());
        if (result.processed != 0) {
            const auto output = extraction::exportRun(result, root, prefix);
            std::cout << std::format("Results: {}\n", extraction::pathText(output));
        }
    } catch (const std::exception& error) {
        std::cerr << std::format("Extraction failed: {}\n", error.what());
    }
}

int main()
{
    bool hasClientHeader = false;
    bool hasServerHeader = false;
    bool hasPythonRoot = false;

    std::string clientHeaderName;
    std::string serverHeaderName;
    std::string chosenPythonRoot;

    fs::path clientPath;
    fs::path serverPath;

    auto subdirs = getSubdirectoriesOfCurrentPath();

    while (true) {
        clearConsole();

        std::cout << "===============================\n";
        std::cout << "     P A T H   S E T T I N G S \n";
        std::cout << "===============================\n\n";

        std::cout << "Client Path: ";
        if (!hasClientHeader) { setColor(12); std::cout << "(not set)\n"; }
        else                  { setColor(10); std::cout << clientPath.string() << "\n"; }
        setColor(7);

        std::cout << "Server Path: ";
        if (!hasServerHeader) { setColor(12); std::cout << "(not set)\n"; }
        else                  { setColor(10); std::cout << serverPath.string() << "\n"; }
        setColor(7);

        std::cout << "Python Root: ";
        if (!hasPythonRoot) { setColor(12); std::cout << "(not set)\n"; }
        else                { setColor(10); std::cout << chosenPythonRoot << "\n"; }
        setColor(7);

        std::cout << "\n1) Select Client Path";
        if (hasClientHeader) { setColor(10); std::cout << " (set)"; } setColor(7); std::cout << "\n";
        std::cout << "2) Select Server Path";
        if (hasServerHeader) { setColor(10); std::cout << " (set)"; } setColor(7); std::cout << "\n";
        std::cout << "3) Select Python Root";
        if (hasPythonRoot)   { setColor(10); std::cout << " (set)"; } setColor(7); std::cout << "\n";
        std::cout << "4) -> Main Menu\n0) Exit\nChoice: ";

        int configChoice;
        std::cin >> configChoice;
        if (!std::cin) { std::cin.clear(); std::cin.ignore(10000, '\n'); continue; }
        std::cin.ignore(10000, '\n');

        if (configChoice == 0) { std::cout << "Exiting.\n"; return 0; }
        else if (configChoice == 4) { /* fall through to main menu */ }
        else if (configChoice >= 1 && configChoice <= 3) {
            clearConsole();
            if (subdirs.empty()) {
                std::cout << "No subdirectories found!\nPress ENTER...\n";
                std::cin.ignore(10000, '\n');
                continue;
            }
            std::cout << "Available subdirectories:\n";
            for (size_t i = 0; i < subdirs.size(); ++i)
                std::cout << std::format("{}) {}\n", i + 1, subdirs[i].filename().string());
            std::cout << "Select index (0=cancel): ";
            int sel;
            std::cin >> sel;
            if (!std::cin || sel <= 0 || sel > static_cast<int>(subdirs.size())) {
                std::cin.clear();
                std::cin.ignore(10000, '\n');
                std::cout << "Cancelled.\n";
            } else {
                if (configChoice == 1) {
                    clientPath = subdirs[sel - 1];
                    hasClientHeader = false; clientHeaderName.clear();
                    findClientHeaderInUserInterface(clientPath, hasClientHeader, clientHeaderName);
                    std::cout << (hasClientHeader
                        ? std::format("Found locale_inc.h: {}\n", clientHeaderName)
                        : "locale_inc.h not found.\n");
                } else if (configChoice == 2) {
                    serverPath = subdirs[sel - 1];
                    hasServerHeader = false; serverHeaderName.clear();
                    findServerHeaderInCommon(serverPath, hasServerHeader, serverHeaderName);
                    std::cout << (hasServerHeader
                        ? std::format("Found service.h/commondefines.h: {}\n", serverHeaderName)
                        : "No service.h/commondefines.h found.\n");
                } else {
                    auto chosenDir = subdirs[sel - 1];
                    std::vector<std::string> pyRoots;
                    findPythonRoots(chosenDir, pyRoots);
                    if (!pyRoots.empty()) {
                        hasPythonRoot = true;
                        chosenPythonRoot = pyRoots.front();
                        std::cout << std::format("Python 'root' found: {}\n", chosenPythonRoot);
                    } else {
                        hasPythonRoot = false; chosenPythonRoot.clear();
                        std::cout << "No 'root' folder found.\n";
                    }
                }
            }
            std::cout << "Press ENTER...\n";
            std::cin.ignore(10000, '\n');
            continue;
        } else {
            continue;
        }

        // ── MAIN MENU ─────────────────────────────────────
        while (true) {
            clearConsole();
            std::cout << "==============================\n";
            std::cout << "        M A I N   M E N U     \n";
            std::cout << "==============================\n\n";

            if (hasClientHeader) setColor(10); else setColor(12);
            std::cout << "1) Client\n";
            if (hasServerHeader) setColor(10); else setColor(12);
            std::cout << "2) Server\n";
            if (hasPythonRoot)   setColor(10); else setColor(12);
            std::cout << "3) Python\n";
            setColor(7);
            std::cout << "4) Back to Path Settings\n0) Exit\nChoice: ";

            int choice;
            std::cin >> choice;
            if (!std::cin) { std::cin.clear(); std::cin.ignore(10000, '\n'); continue; }
            std::cin.ignore(10000, '\n');

            if (choice == 0) { std::cout << "Exiting.\n"; return 0; }
            else if (choice == 4) { break; }
            else if (choice == 1 || choice == 2) {
                clearConsole();
                bool isClient = (choice == 1);
                if (isClient && !hasClientHeader) {
                    std::cerr << "No client header found. Please set Client Path first.\n";
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                if (!isClient && !hasServerHeader) {
                    std::cerr << "No server header found. Please set Server Path first.\n";
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                const std::string& headerName = isClient ? clientHeaderName : serverHeaderName;
                const fs::path&    srcPath    = isClient ? clientPath : serverPath;
                const std::string  prefix     = isClient ? "CLIENT" : "SERVER";

                std::vector<std::string> defines;
                try {
                    defines = readDefines(headerName);
                } catch (const std::exception& error) {
                    std::cerr << std::format("Could not read {}: {}\n", headerName, error.what());
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                if (defines.empty()) {
                    std::cerr << std::format("No #define entries in {}.\n", headerName);
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                while (true) {
                    clearConsole();
                    std::cout << std::format("{} defines in {}:\n", prefix, headerName);
                    for (size_t i = 0; i < defines.size(); ++i)
                        std::cout << std::format("{}) {}\n", i + 1, defines[i]);
                    std::cout << "0) Back\nEnter one or more indices: ";
                    auto selection = readSymbolChoices(defines);
                    if (!selection) break;
                    if (selection->empty()) {
                        std::cerr << "Invalid choice!\n"; continue;
                    }
                    runExtraction(srcPath, *selection, extraction::Language::Cpp, prefix);
                    std::cout << "Press ENTER...\n";
                    std::cin.ignore(10000, '\n');
                }
            }
            else if (choice == 3) {
                clearConsole();
                if (!hasPythonRoot) {
                    std::cerr << "No Python root set.\n";
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                const auto root = extraction::pathFromText(chosenPythonRoot);
                auto pyFiles = findSourceFiles(root, extraction::Language::Python);
                auto paramSet = extraction::collectPythonParameters(pyFiles.paths, pyFiles.issues);
                printFileIssues(pyFiles.issues);
                if (paramSet.empty()) {
                    std::cerr << "No 'if app.xyz' lines found.\n";
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                std::vector<std::string> params(paramSet.begin(), paramSet.end());
                std::ranges::sort(params);

                while (true) {
                    clearConsole();
                    std::cout << "Python app.<param> found:\n";
                    for (size_t i = 0; i < params.size(); ++i)
                        std::cout << std::format("{}) {}\n", i + 1, params[i]);
                    std::cout << "0) Back\nEnter one or more indices: ";
                    auto selection = readSymbolChoices(params);
                    if (!selection) break;
                    if (selection->empty()) {
                        std::cerr << "Invalid choice!\n"; continue;
                    }
                    runExtraction(root, *selection, extraction::Language::Python, "PYTHON");
                    std::cout << "Press ENTER...\n";
                    std::cin.ignore(10000, '\n');
                }
            }
            else { continue; }
        }
    }
}
