// Requires /std:c++20 (MSVC) or -std=c++20 (GCC/Clang)
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <map>
#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <unordered_map>
#include <format>
#include "helpers.h"
#ifdef _WIN32
#include <windows.h>
#endif
#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "No filesystem support detected"
#endif

using namespace std::chrono;

/*******************************************************
 * PLATFORM-SPECIFIC: clearConsole()
 *******************************************************/
#ifdef _WIN32
static inline void clearConsole() { system("cls"); }
#else
static inline void clearConsole() { system("clear"); }
#endif

static std::mutex consoleMutex;
static std::mutex resultsMutex; // separate from consoleMutex to reduce contention

#ifdef _WIN32
static inline void setColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
#else
static inline void setColor(int color) {
    std::cout << "\033[" << color << "m";
}
#endif

/*******************************************************
 * printProgress(): race-free via atomic CAS on timestamp
 *******************************************************/
void printProgress(size_t current, size_t total, int width = 50) {
    static std::atomic<int64_t> lastUpdateMs{0};
    int64_t nowMs = duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()).count();
    int64_t old = lastUpdateMs.load(std::memory_order_relaxed);
    if (nowMs - old < 100) return;
    // Only one thread wins the CAS; others return immediately
    if (!lastUpdateMs.compare_exchange_weak(old, nowMs, std::memory_order_relaxed)) return;

    if (total == 0) return;
    float ratio = static_cast<float>(current) / static_cast<float>(total);
    if (ratio > 1.0f) ratio = 1.0f;
    int c = static_cast<int>(ratio * width);

    std::lock_guard lock(consoleMutex);
    std::cout << "[";
    for (int i = 0; i < width; ++i)
        std::cout << (i < c ? '#' : ' ');
    std::cout << "] " << static_cast<int>(ratio * 100.0f) << " %\r" << std::flush;
}

/*******************************************************
 * readBufferedFile():
 *   Windows: zero-copy via MapViewOfFile.
 *   Other:   binary getline fallback.
 *******************************************************/
void readBufferedFile(const std::string& filename, std::vector<std::string>& lines) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << std::format("Error: Unable to open file: {}\n", filename);
        return;
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        return;
    }
    HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap) { CloseHandle(hFile); return; }
    const char* data = static_cast<const char*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
    if (!data) { CloseHandle(hMap); CloseHandle(hFile); return; }

    const size_t sz = static_cast<size_t>(fileSize.QuadPart);
    lines.reserve(sz / 32 + 1);
    const char* p = data;
    const char* end = data + sz;
    while (p < end) {
        const char* nl = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
        if (!nl) nl = end;
        size_t len = static_cast<size_t>(nl - p);
        if (len > 0 && p[len - 1] == '\r') --len;
        lines.emplace_back(p, len);
        p = (nl < end) ? nl + 1 : end;
    }

    UnmapViewOfFile(data);
    CloseHandle(hMap);
    CloseHandle(hFile);
#else
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << std::format("Error: Unable to open file: {}\n", filename);
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
#endif
}

/*******************************************************
 * Data Structures
 *******************************************************/
struct CodeBlock {
    std::string filename;
    std::string content;
};

/*******************************************************
 * Line-count cache with reader-writer locking
 *******************************************************/
static std::shared_mutex lineCountCacheMutex;
static std::unordered_map<std::string, size_t> lineCountCache;

size_t getFileLineCount(const std::string& filename)
{
    {
        std::shared_lock lk(lineCountCacheMutex);
        auto it = lineCountCache.find(filename);
        if (it != lineCountCache.end()) return it->second;
    }

    std::ifstream ifs(filename);
    size_t count = 0;
    if (ifs.is_open()) {
        std::string tmp;
        while (std::getline(ifs, tmp)) ++count;
    }

    std::unique_lock lk(lineCountCacheMutex);
    lineCountCache.emplace(filename, count);
    return count;
}

size_t getTotalLineCount(const std::vector<std::string>& files)
{
    size_t total = 0;
    for (const auto& f : files) total += getFileLineCount(f);
    return total;
}

/*******************************************************
 * Regex patterns — defined once, globally
 *******************************************************/
// anyIfStartRegex removed — replaced by isAnyIfStart() from helpers.h (no regex overhead)

std::regex createConditionalRegex(const std::string& define) {
    // {0} repeated: positional std::format arg
    std::string pattern = std::format(
        R"((^\s*#(ifdef|ifndef)\s+{0}\b)|)"
        R"((^\s*#(if|elif)\s+defined\s*\(\s*{0}\s*\))|)"
        R"((^\s*#(if|elif)\s+defined\s+{0})|)"
        R"((^\s*#(if|elif)\s+\(?\s*{0}\s*\)?))",
        define);
    return std::regex(pattern, std::regex_constants::ECMAScript | std::regex_constants::optimize);
}

/*******************************************************
 * isFunctionHead(): manual heuristic replacing the catastrophically
 *   backtracking functionHeadRegex.
 *   Returns: 0=no match  1=definition({)  2=declaration(;)  3=potential multiline
 *******************************************************/
static int isFunctionHead(const std::string& line) {
    size_t parenOpen = line.find('(');
    if (parenOpen == std::string::npos) return 0;

    // Word char or '>' must immediately precede '('
    size_t nameEnd = parenOpen;
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(line[nameEnd - 1]))) --nameEnd;
    if (nameEnd == 0) return 0;
    char bc = line[nameEnd - 1];
    if (!std::isalnum(static_cast<unsigned char>(bc)) && bc != '_' && bc != '>') return 0;

    size_t parenClose = line.rfind(')');
    if (parenClose == std::string::npos || parenClose < parenOpen) return 0;

    size_t pos = parenClose + 1;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;

    if (pos >= line.size()) return 3; // no trailing char — potential multiline head
    if (line[pos] == '{') return 1;
    if (line[pos] == ';') return 2;
    return 0;
}

static const std::regex pythonIfAppRegex(
    R"((?:if|elif)\s*\(?\s*app\.(\w+))",
    std::regex_constants::ECMAScript | std::regex_constants::optimize);
static const std::regex defRegex(R"(^\s*def\s+[\w_]+)");

// getIndent() moved to helpers.h — early-stop version, no full-line scan

/*******************************************************
 * writeOutputPerFile()
 *******************************************************/
void writeOutputPerFile(const std::string& prefix,
    const std::string& defineName,
    const std::vector<CodeBlock>& blocks)
{
    fs::create_directory("Output");
    std::string outDir = std::format("Output/{}_{}_files", prefix, defineName);
    fs::create_directory(outDir);

    std::map<std::string, std::vector<std::string>> fileToContents;
    for (const auto& block : blocks)
        fileToContents[block.filename].push_back(block.content);

    for (const auto& [srcFile, textBlocks] : fileToContents) {
        std::string baseName = fs::path(srcFile).filename().string();
        std::string outFileName = std::format("{}/{}.txt", outDir, baseName);
        std::ofstream ofs(outFileName, std::ios::app);
        if (!ofs.is_open()) {
            std::cerr << std::format("Error when opening {}\n", outFileName);
            continue;
        }
        for (const auto& content : textBlocks) ofs << content << "\n";
        ofs << std::format("\n--- SUMMARY: {} Block(s) in {} ---\n\n",
            textBlocks.size(), baseName);
    }
}

/*******************************************************
 * parseFileSinglePass():
 *   - No in-loop tls_counter: progress updated once per file at return
 *     (fixes the double-counting bug from the old tls_counter + worker flush)
 *   - std::string instead of std::ostringstream for block building
 *   - Global anyIfStartRegex used (no shadowing static locals)
 *   - isFunctionHead() replaces backtracking functionHeadRegex
 *******************************************************/
std::pair<std::vector<CodeBlock>, std::vector<CodeBlock>>
parseFileSinglePass(const std::string& filename,
    const std::regex& startDefineRegex,
    const std::string& defineName,
    std::atomic<size_t>& processed,
    size_t totalLines,
    size_t& outLineCount)
{
    std::vector<std::string> lines;
    readBufferedFile(filename, lines);

    std::vector<CodeBlock> defineBlocks;
    std::vector<CodeBlock> functionBlocks;

    bool insideDefineBlock = false;
    int  defineNesting = 0;
    std::string currentDefineBlock;

    bool inFunction = false;
    int  braceCount = 0;
    bool functionRelevant = false;
    std::string currentFunc;

    bool potentialFunctionHead = false;
    std::string potentialHeadBuffer;

    const size_t lineCount = lines.size();
    outLineCount = lineCount;

    for (size_t i = 0; i < lineCount; ++i)
    {
        const std::string& line = lines[i];

        // ── define block tracking ─────────────────────────
        if (!insideDefineBlock)
        {
            bool isIfLine = (line.find("#if ") != std::string::npos ||
                line.find("#ifdef ") != std::string::npos ||
                line.find("#ifndef ") != std::string::npos ||
                line.find("#elif") != std::string::npos);
            if (isIfLine && std::regex_search(line, startDefineRegex))
            {
                int snippetStart = std::max(static_cast<int>(i) - 2, 0);
                currentDefineBlock.clear();
                for (int s = snippetStart; s <= static_cast<int>(i); ++s) {
                    currentDefineBlock += lines[s];
                    currentDefineBlock += '\n';
                }
                insideDefineBlock = true;
                defineNesting = 1;
            }
        }
        else
        {
            currentDefineBlock += line;
            currentDefineBlock += '\n';

            if (isAnyIfStart(line)) {
                ++defineNesting;
            } else if (line.find("#endif") != std::string::npos) {
                if (--defineNesting <= 0) {
                    defineBlocks.push_back({filename,
                        "##########\n" + filename + "\n##########\n" + currentDefineBlock});
                    insideDefineBlock = false;
                    defineNesting = 0;
                    currentDefineBlock.clear();
                }
            }
        }

        // Pre-filter: only run the expensive regex when '#' AND define name are present
        bool lineMatchesDefine = shouldRunDefineRegex(line, defineName)
                              && std::regex_search(line, startDefineRegex);

        // ── function tracking ─────────────────────────────
        if (!inFunction)
        {
            if (potentialFunctionHead)
            {
                potentialHeadBuffer += '\n';
                potentialHeadBuffer += line;
                if (line.find('{') != std::string::npos) {
                    inFunction = true;
                    braceCount = 0;
                    functionRelevant = lineMatchesDefine;
                    currentFunc = std::move(potentialHeadBuffer);
                    currentFunc += '\n';
                    for (char c : line) {
                        if (c == '{') ++braceCount;
                        else if (c == '}') --braceCount;
                    }
                    potentialFunctionHead = false;
                    potentialHeadBuffer.clear();
                } else if (line.find(';') != std::string::npos) {
                    potentialFunctionHead = false;
                    potentialHeadBuffer.clear();
                }
            }
            else
            {
                int h = isFunctionHead(line);
                if (h == 1) {
                    inFunction = true;
                    braceCount = 0;
                    functionRelevant = lineMatchesDefine;
                    currentFunc = line;
                    currentFunc += '\n';
                    for (char c : line) {
                        if (c == '{') ++braceCount;
                        else if (c == '}') --braceCount;
                    }
                } else if (h == 3) {
                    potentialFunctionHead = true;
                    potentialHeadBuffer = line;
                }
            }
        }
        else
        {
            currentFunc += line;
            currentFunc += '\n';
            if (lineMatchesDefine) functionRelevant = true;
            for (char c : line) {
                if (c == '{') ++braceCount;
                else if (c == '}') --braceCount;
            }
            if (braceCount <= 0) {
                if (functionRelevant) {
                    functionBlocks.push_back({filename,
                        "##########\n" + filename + "\n##########\n" + currentFunc});
                }
                inFunction = false;
                braceCount = 0;
                currentFunc.clear();
                functionRelevant = false;
            }
        }
    }

    // Single progress update per file.
    // When totalLines==0 the caller (parseWorkerDynamic) handles progress itself.
    processed.fetch_add(lineCount, std::memory_order_relaxed);
    if (totalLines > 0)
        printProgress(processed.load(std::memory_order_relaxed), totalLines);

    return { std::move(defineBlocks), std::move(functionBlocks) };
}

/*******************************************************
 * parsePythonFileSinglePass():
 *   Index-based (uses readBufferedFile) to avoid seekg + double-counting.
 *   The inner if-block scan reads ahead with indices; the outer loop
 *   still processes all lines for function tracking (same semantics as
 *   original seekg approach).
 *******************************************************/
std::pair<std::vector<CodeBlock>, std::vector<CodeBlock>>
parsePythonFileSinglePass(const std::string& filename,
    const std::string& param,
    std::atomic<size_t>& processed,
    size_t totalLines,
    size_t& outLineCount)
{
    std::vector<CodeBlock> ifBlocks;
    std::vector<CodeBlock> funcBlocks;

    std::vector<std::string> lines;
    readBufferedFile(filename, lines);
    const size_t n = lines.size();
    outLineCount = n;

    std::string ifParamPattern = std::format(R"((?:if|elif)\s*\(?\s*app\.{}\b)", param);
    std::regex ifParamRegex(ifParamPattern,
        std::regex_constants::ECMAScript | std::regex_constants::optimize);

    bool insideFunc = false;
    int  funcIndent = 0;
    bool functionRelevant = false;
    std::string currentFunc;

    for (size_t i = 0; i < n; ++i)
    {
        const std::string& line = lines[i];

        if (std::regex_search(line, defRegex)) {
            if (insideFunc && functionRelevant)
                funcBlocks.push_back({filename,
                    "##########\n" + filename + "\n##########\n" + currentFunc});
            insideFunc = true;
            funcIndent = getIndent(line);
            functionRelevant = false;
            currentFunc = line;
            currentFunc += '\n';
            continue;
        }

        if (insideFunc) {
            int currentIndent = getIndent(line);
            if (!line.empty() && currentIndent <= funcIndent) {
                if (functionRelevant)
                    funcBlocks.push_back({filename,
                        "##########\n" + filename + "\n##########\n" + currentFunc});
                insideFunc = false;
                currentFunc.clear();
                functionRelevant = false;
                // fall through: this line may match ifParamRegex
            } else {
                currentFunc += line;
                currentFunc += '\n';
            }
        }

        if (std::regex_search(line, ifParamRegex)) {
            int ifIndent = getIndent(line);
            std::string blockContent = line;
            blockContent += '\n';

            // Scan ahead for indented block body (read-only lookahead, no seekg)
            for (size_t j = i + 1; j < n; ++j) {
                const std::string& nextLine = lines[j];
                if (!nextLine.empty() && getIndent(nextLine) <= ifIndent) break;
                blockContent += nextLine;
                blockContent += '\n';
            }

            ifBlocks.push_back({filename,
                "##########\n" + filename + "\n##########\n" + blockContent});
            if (insideFunc) functionRelevant = true;
        }
    }

    if (insideFunc && functionRelevant)
        funcBlocks.push_back({filename,
            "##########\n" + filename + "\n##########\n" + currentFunc});

    processed.fetch_add(n, std::memory_order_relaxed);
    printProgress(processed.load(std::memory_order_relaxed), totalLines);

    return { std::move(ifBlocks), std::move(funcBlocks) };
}

/*******************************************************
 * collectPythonParameters()
 *******************************************************/
std::unordered_set<std::string> collectPythonParameters(const std::vector<std::string>& pyFiles) {
    std::unordered_set<std::string> params;

    static const std::unordered_set<std::string> blacklist = {
        "loggined", "VK_UP", "VK_RIGHT", "VK_LEFT", "VK_HOME", "VK_END",
        "VK_DOWN", "VK_DELETE", "TARGET", "SELL", "BUY", "DIK_DOWN",
        "DIK_F1", "DIK_F2", "DIK_F3", "DIK_F4", "DIK_H", "DIK_LALT",
        "DIK_LCONTROL", "DIK_RETURN", "DIK_SYSRQ", "DIK_UP", "DIK_V",
        "GetGlobalTime","GetTime","IsDevStage","IsEnableTestServerFlag",
        "IsExistFile","IsPressed","IsWebPageMode",
    };

    for (const auto& f : pyFiles) {
        std::ifstream ifs(f);
        if (!ifs.is_open()) continue;

        std::string line;
        while (std::getline(ifs, line)) {
            size_t pos = line.find("if app.");
            if (pos != std::string::npos) {
                size_t start = pos + 7;
                size_t end = line.find_first_of(" ():", start);
                std::string p = line.substr(start, end - start);
                if (!p.empty() && !blacklist.count(p))
                    params.insert(std::move(p));
            } else {
                std::smatch m;
                if (std::regex_search(line, m, pythonIfAppRegex) && m.size() > 1) {
                    std::string p = m[1].str();
                    if (!blacklist.count(p))
                        params.insert(std::move(p));
                }
            }
        }
    }
    return params;
}

/*******************************************************
 * Multi-threaded C++ parsing
 *******************************************************/
static std::atomic<size_t> nextFileIndex{0};

void parseWorkerDynamic(const std::vector<std::string>& files,
    const std::regex& startDefineRegex,
    const std::string& defineName,
    std::atomic<size_t>& processed,
    std::atomic<size_t>& totalLinesAccum,   // accumulates as files are parsed
    std::vector<CodeBlock>& defineBlocksOut,
    std::vector<CodeBlock>& functionBlocksOut)
{
    std::vector<CodeBlock> localDefine;
    std::vector<CodeBlock> localFunc;

    while (true) {
        size_t idx = nextFileIndex.fetch_add(1, std::memory_order_relaxed);
        if (idx >= files.size()) break;

        size_t lineCountThisFile = 0;
        // totalLines passed as 0 — progress bar uses processed/totalLinesAccum ratio
        auto [def, func] = parseFileSinglePass(files[idx], startDefineRegex,
            defineName, processed, 0, lineCountThisFile);

        totalLinesAccum.fetch_add(lineCountThisFile, std::memory_order_relaxed);
        printProgress(processed.load(std::memory_order_relaxed),
                      totalLinesAccum.load(std::memory_order_relaxed));

        localDefine.insert(localDefine.end(),
            std::make_move_iterator(def.begin()), std::make_move_iterator(def.end()));
        localFunc.insert(localFunc.end(),
            std::make_move_iterator(func.begin()), std::make_move_iterator(func.end()));
    }

    // Merge into shared output — single lock per worker lifetime
    std::lock_guard lock(resultsMutex);
    defineBlocksOut.insert(defineBlocksOut.end(),
        std::make_move_iterator(localDefine.begin()), std::make_move_iterator(localDefine.end()));
    functionBlocksOut.insert(functionBlocksOut.end(),
        std::make_move_iterator(localFunc.begin()), std::make_move_iterator(localFunc.end()));
}

std::pair<std::vector<CodeBlock>, std::vector<CodeBlock>>
parseAllFilesMultiThread(const std::vector<std::string>& files, const std::string& define)
{
    auto startDefineRegex = createConditionalRegex(define);

    // FIX: No pre-scan of all files for line count — eliminates the sequential
    //      double-read that previously blocked before any thread started.
    //      totalLinesAccum grows atomically as workers parse files; the progress
    //      bar shows processed/accum which is always a valid ratio [0,1].
    std::atomic<size_t> totalLinesAccum{1}; // start at 1 to avoid div-by-zero

    unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 2;
    size_t numThreads = std::min<size_t>(hwThreads, files.size());
    std::cout << std::format("Starting {} thread(s)...\n", numThreads);

    std::atomic<size_t> processed{0};
    std::vector<CodeBlock> allDefineBlocks;
    std::vector<CodeBlock> allFunctionBlocks;

    nextFileIndex.store(0);

    auto startTime = high_resolution_clock::now();
    {
        // jthread auto-joins on destruction — no manual join loop needed
        std::vector<std::jthread> threads;
        threads.reserve(numThreads);
        for (size_t t = 0; t < numThreads; ++t) {
            threads.emplace_back(parseWorkerDynamic,
                std::cref(files), std::cref(startDefineRegex), std::cref(define),
                std::ref(processed), std::ref(totalLinesAccum),
                std::ref(allDefineBlocks), std::ref(allFunctionBlocks));
        }
    } // threads join here

    size_t finalLines = totalLinesAccum.load();
    printProgress(finalLines, finalLines);
    std::cout << "\n";
    std::cout << std::format("Total lines read: {}\n", finalLines);

    auto ms = duration_cast<milliseconds>(high_resolution_clock::now() - startTime).count();
    std::cout << std::format("Parsing define '{}' finished in {} ms\n", define, ms);

    return { std::move(allDefineBlocks), std::move(allFunctionBlocks) };
}

/*******************************************************
 * Multi-threaded Python parsing
 *******************************************************/
static std::atomic<size_t> nextPyFileIndex{0};

void parsePythonWorkerDynamic(const std::vector<std::string>& files,
    const std::string& param,
    std::atomic<size_t>& processed,
    size_t totalLines,
    std::vector<CodeBlock>& ifBlocksOut,
    std::vector<CodeBlock>& funcBlocksOut)
{
    std::vector<CodeBlock> localIf;
    std::vector<CodeBlock> localFunc;

    while (true) {
        size_t idx = nextPyFileIndex.fetch_add(1, std::memory_order_relaxed);
        if (idx >= files.size()) break;

        size_t lineCountThisFile = 0;
        auto [ifb, fb] = parsePythonFileSinglePass(files[idx], param,
            processed, totalLines, lineCountThisFile);

        localIf.insert(localIf.end(),
            std::make_move_iterator(ifb.begin()), std::make_move_iterator(ifb.end()));
        localFunc.insert(localFunc.end(),
            std::make_move_iterator(fb.begin()), std::make_move_iterator(fb.end()));
    }

    std::lock_guard lock(resultsMutex);
    ifBlocksOut.insert(ifBlocksOut.end(),
        std::make_move_iterator(localIf.begin()), std::make_move_iterator(localIf.end()));
    funcBlocksOut.insert(funcBlocksOut.end(),
        std::make_move_iterator(localFunc.begin()), std::make_move_iterator(localFunc.end()));
}

std::pair<std::vector<CodeBlock>, std::vector<CodeBlock>>
parsePythonAllFilesMultiThread(const std::vector<std::string>& pyFiles, const std::string& param)
{
    std::cout << "Counting total lines (Python)...\n";
    size_t totalLines = getTotalLineCount(pyFiles);
    std::cout << std::format("Total Python lines: {}\n", totalLines);

    unsigned int hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 2;
    size_t numThreads = std::min<size_t>(hwThreads, pyFiles.size());
    std::cout << std::format("Starting {} thread(s) for Python...\n", numThreads);

    std::atomic<size_t> processed{0};
    std::vector<CodeBlock> allIfBlocks;
    std::vector<CodeBlock> allFuncBlocks;

    nextPyFileIndex.store(0);

    auto startTime = high_resolution_clock::now();
    {
        std::vector<std::jthread> threads;
        threads.reserve(numThreads);
        for (size_t t = 0; t < numThreads; ++t) {
            threads.emplace_back(parsePythonWorkerDynamic,
                std::cref(pyFiles), std::cref(param),
                std::ref(processed), totalLines,
                std::ref(allIfBlocks), std::ref(allFuncBlocks));
        }
    }

    printProgress(totalLines, totalLines);
    std::cout << "\n";

    auto ms = duration_cast<milliseconds>(high_resolution_clock::now() - startTime).count();
    std::cout << std::format("Parsing (app.{}) finished in {} ms\n", param, ms);

    return { std::move(allIfBlocks), std::move(allFuncBlocks) };
}

/*******************************************************
 * findClientHeaderInUserInterface()
 *******************************************************/
void findClientHeaderInUserInterface(const fs::path& startPath,
    bool& hasClientHeader,
    std::string& clientHeaderName)
{
    hasClientHeader = false;
    clientHeaderName.clear();

    try {
        for (const auto& p : fs::recursive_directory_iterator(startPath,
            fs::directory_options::skip_permission_denied))
        {
            try {
                if (fs::is_symlink(p.path())) continue;
                if (!fs::is_regular_file(p.status())) continue;

                std::string rel = p.path().string();
                std::string lowerRel;
                lowerRel.reserve(rel.size());
                for (char c : rel)
                    lowerRel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerRel.find("userinterface") == std::string::npos) continue;

                std::string fn = p.path().filename().string();
                std::string lowerFn;
                for (char c : fn)
                    lowerFn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerFn == "locale_inc.h") {
                    hasClientHeader = true;
                    clientHeaderName = p.path().string();
                    return;
                }
            } catch (...) { continue; }
        }
    } catch (...) {}
}

/*******************************************************
 * findServerHeaderInCommon()
 *******************************************************/
void findServerHeaderInCommon(const fs::path& startPath,
    bool& hasServerHeader,
    std::string& serverHeaderName)
{
    hasServerHeader = false;
    serverHeaderName.clear();

    try {
        for (const auto& p : fs::recursive_directory_iterator(startPath,
            fs::directory_options::skip_permission_denied))
        {
            try {
                if (fs::is_symlink(p.path())) continue;
                if (!fs::is_regular_file(p.status())) continue;

                std::string rel = p.path().string();
                std::string lowerRel;
                lowerRel.reserve(rel.size());
                for (char c : rel)
                    lowerRel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerRel.find("common") == std::string::npos) continue;

                std::string fn = p.path().filename().string();
                std::string lowerFn;
                for (char c : fn)
                    lowerFn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerFn == "service.h" || lowerFn == "commondefines.h") {
                    hasServerHeader = true;
                    serverHeaderName = p.path().string();
                    return;
                }
            } catch (...) { continue; }
        }
    } catch (...) {}
}

/*******************************************************
 * findPythonRoots()
 *******************************************************/
void findPythonRoots(const fs::path& startPath, std::vector<std::string>& pythonRoots)
{
    pythonRoots.clear();
    try {
        for (const auto& p : fs::directory_iterator(startPath,
            fs::directory_options::skip_permission_denied))
        {
            try {
                if (!fs::is_directory(p.path())) continue;
                std::wstring wdir = p.path().filename().wstring();
                std::wstring lowerW;
                lowerW.reserve(wdir.size());
                for (wchar_t wc : wdir)
                    lowerW.push_back(static_cast<wchar_t>(std::tolower(wc)));
                if (lowerW == L"root")
                    pythonRoots.push_back(p.path().string());
            } catch (...) { continue; }
        }
    } catch (...) {}
}

/*******************************************************
 * findSourceFiles()
 *******************************************************/
std::vector<std::string> findSourceFiles(const fs::path& startRoot)
{
    std::vector<std::string> result;
    static const std::unordered_set<std::string> validExtensions = {".cpp", ".h"};

    try {
        for (const auto& p : fs::recursive_directory_iterator(startRoot,
            fs::directory_options::skip_permission_denied))
        {
            try {
                if (fs::is_symlink(p.path())) continue;
                if (fs::is_regular_file(p.path())) {
                    std::string ext = p.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (validExtensions.count(ext))
                        result.push_back(p.path().string());
                }
            } catch (...) { continue; }
        }
    } catch (...) {}

    return result;
}

/*******************************************************
 * readDefines()
 *******************************************************/
std::vector<std::string> readDefines(const std::string& filename) {
    std::vector<std::string> result;
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::cerr << std::format("Could not open {}!\n", filename);
        return result;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        size_t pos = line.find("#define ");
        if (pos != std::string::npos) {
            size_t start = pos + 8;
            size_t end = line.find_first_of(" \t\r\n", start);
            std::string name = line.substr(start, end - start);
            if (!name.empty()) result.push_back(std::move(name));
        }
    }
    return result;
}

/*******************************************************
 * getSubdirectoriesOfCurrentPath()
 *******************************************************/
std::vector<fs::path> getSubdirectoriesOfCurrentPath()
{
    std::vector<fs::path> dirs;
    for (const auto& p : fs::directory_iterator(fs::current_path(),
        fs::directory_options::skip_permission_denied))
    {
        if (fs::is_directory(p.path()))
            dirs.push_back(p.path());
    }
    std::ranges::sort(dirs);
    return dirs;
}

/*******************************************************
 * main()
 *******************************************************/
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
                // CLIENT / SERVER share the same flow
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

                auto defines = readDefines(headerName);
                if (defines.empty()) {
                    std::cerr << std::format("No #define entries in {}.\n", headerName);
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                auto sourceFiles = findSourceFiles(srcPath);
                if (sourceFiles.empty()) {
                    std::cerr << std::format("No .cpp/.h files found in {}.\n", srcPath.string());
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                while (true) {
                    clearConsole();
                    std::cout << std::format("{} defines in {}:\n", prefix, headerName);
                    for (size_t i = 0; i < defines.size(); ++i)
                        std::cout << std::format("{}) {}\n", i + 1, defines[i]);
                    std::cout << "0) Back\nChoice: ";
                    int dchoice;
                    std::cin >> dchoice;
                    std::cin.ignore(10000, '\n');
                    if (!std::cin || dchoice == 0) break;
                    if (dchoice < 1 || dchoice > static_cast<int>(defines.size())) {
                        std::cerr << "Invalid choice!\n"; continue;
                    }
                    const std::string& def = defines[dchoice - 1];
                    auto results = parseAllFilesMultiThread(sourceFiles, def);

                    writeOutputPerFile(prefix, def + "_DEFINE", results.first);
                    writeOutputPerFile(prefix, def + "_FUNC", results.second);

                    setColor(10);
                    std::cout << std::format("Done for define '{}' - see 'Output/{}_{}'\n",
                        def, prefix, def + "_DEFINE_files");
                    setColor(7);
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
                std::vector<std::string> pyFiles;
                try {
                    for (const auto& p : fs::recursive_directory_iterator(chosenPythonRoot,
                        fs::directory_options::skip_permission_denied))
                    {
                        if (fs::is_symlink(p.path())) continue;
                        if (fs::is_regular_file(p.path()) && p.path().extension() == ".py")
                            pyFiles.push_back(p.path().string());
                    }
                } catch (...) {}
                if (pyFiles.empty()) {
                    std::cerr << std::format("No .py files found in {}.\n", chosenPythonRoot);
                    std::cout << "Press ENTER...\n"; std::cin.ignore(10000, '\n'); continue;
                }
                auto paramSet = collectPythonParameters(pyFiles);
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
                    std::cout << "0) Back\nChoice: ";
                    int pchoice;
                    std::cin >> pchoice;
                    std::cin.ignore(10000, '\n');
                    if (!std::cin || pchoice == 0) break;
                    if (pchoice < 1 || pchoice > static_cast<int>(params.size())) {
                        std::cerr << "Invalid choice!\n"; continue;
                    }
                    const std::string& chosenParam = params[pchoice - 1];
                    auto pyResults = parsePythonAllFilesMultiThread(pyFiles, chosenParam);

                    fs::create_directory("Output");
                    {
                        std::ofstream out(std::format("Output/PYTHON_{}_DEFINE.txt", chosenParam));
                        std::unordered_set<std::string> defFiles;
                        for (const auto& b : pyResults.first) {
                            out << b.content << "\n";
                            defFiles.insert(b.filename);
                        }
                        out << std::format("\n--- SUMMARY ({} if-block(s)) in files: ---\n",
                            pyResults.first.size());
                        for (const auto& fn : defFiles) out << fn << "\n";
                    }
                    {
                        std::ofstream out(std::format("Output/PYTHON_{}_FUNC.txt", chosenParam));
                        std::unordered_set<std::string> funcFiles;
                        for (const auto& b : pyResults.second) {
                            out << b.content << "\n";
                            funcFiles.insert(b.filename);
                        }
                        out << std::format("\n--- SUMMARY ({} function block(s)) in files: ---\n",
                            pyResults.second.size());
                        for (const auto& fn : funcFiles) out << fn << "\n";
                    }
                    setColor(10);
                    std::cout << std::format("Done for app.{}. Press ENTER...\n", chosenParam);
                    setColor(7);
                    std::cin.ignore(10000, '\n');
                }
            }
            else { continue; }
        }
    }
}
