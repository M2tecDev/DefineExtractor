#pragma once

#include "source_parser.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

namespace extraction {

namespace fs = std::filesystem;
using source_parser::Language;
using source_parser::SourceRange;
using Progress = std::function<void(size_t completed, size_t total)>;

inline std::string pathText(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

inline fs::path pathFromText(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

struct InputFile {
    fs::path path;
    std::string text;
    std::vector<size_t> lines;
};

inline InputFile readSource(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size < 0) throw std::ios_base::failure("Could not open or size file");
    std::string text(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        throw std::ios_base::failure("Could not read file");
    }
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    auto lines = source_parser::lineStarts(text);
    return {path, std::move(text), std::move(lines)};
}

struct Hit {
    size_t file;
    SourceRange range;
    bool operator==(const Hit&) const = default;
};

struct SymbolHits {
    std::vector<Hit> conditions;
    std::vector<Hit> functions;
    bool operator==(const SymbolHits&) const = default;
};

struct FileIssue {
    fs::path path;
    std::string message;
};

struct Result {
    std::vector<InputFile> files;
    std::vector<FileIssue> issues;
    std::map<std::string, SymbolHits> symbols;
    size_t processed = 0;
};

inline Result scan(std::vector<fs::path> paths, const std::vector<std::string>& symbols,
                   Language language, size_t maxThreads, const Progress& progress = {}) {
    using SymbolEntry = std::pair<const std::string, SymbolHits>;
    Result result;
    std::unordered_map<std::string_view, size_t> requested;
    std::vector<SymbolEntry*> entries;
    for (const auto& symbol : symbols) {
        auto [entry, inserted] = result.symbols.try_emplace(symbol);
        if (inserted) {
            requested.emplace(entry->first, entries.size());
            entries.push_back(&*entry);
        }
    }
    if (paths.empty() || entries.empty()) return result;
    std::ranges::sort(paths);
    const auto unique = std::ranges::unique(paths);
    paths.erase(unique.begin(), unique.end());

    struct Match {
        size_t symbol;
        SourceRange range;
        bool function;
    };
    struct FileResult {
        std::optional<InputFile> input;
        std::vector<Match> matches;
        std::string error;
    };
    std::vector<FileResult> scanned(paths.size());
    std::atomic_size_t next{0};
    std::mutex progressMutex;
    size_t completed = 0;
    auto lastUpdate = std::chrono::steady_clock::time_point{};
    std::mutex errorMutex;
    std::exception_ptr workerError;

    const auto& lookup = requested;
    const auto reference = [&](std::string_view name) -> std::optional<size_t> {
        const auto symbol = lookup.find(name);
        return symbol == lookup.end() ? std::nullopt : std::optional{symbol->second};
    };
    const auto worker = [&] {
        try {
            while (true) {
                const auto index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= paths.size()) break;
                auto& file = scanned[index];
                try {
                    auto input = readSource(paths[index]);
                    const auto emit = [&](size_t symbol, SourceRange range, bool function) {
                        file.matches.push_back({symbol, range, function});
                    };
                    if (language == Language::Cpp) {
                        source_parser::parseCpp(input.text, input.lines, reference, emit);
                    } else {
                        source_parser::parsePython(input.text, input.lines, reference, emit);
                    }
                    if (!file.matches.empty()) file.input = std::move(input);
                } catch (const std::ios_base::failure& error) {
                    file.error = error.what();
                }
                if (progress) {
                    std::lock_guard lock(progressMutex);
                    const auto now = std::chrono::steady_clock::now();
                    if (++completed == paths.size() ||
                        now - lastUpdate >= std::chrono::milliseconds(100)) {
                        progress(completed, paths.size());
                        lastUpdate = now;
                    }
                }
            }
        } catch (...) {
            std::lock_guard lock(errorMutex);
            if (!workerError) workerError = std::current_exception();
            next.store(paths.size(), std::memory_order_relaxed);
        }
    };
    const auto workers = std::clamp(maxThreads, size_t{1}, paths.size());
    if (workers == 1) {
        worker();
    } else {
        std::vector<std::jthread> threads;
        threads.reserve(workers);
        for (size_t index = 0; index < workers; ++index) threads.emplace_back(worker);
    }
    if (workerError) std::rethrow_exception(workerError);

    for (size_t index = 0; index < scanned.size(); ++index) {
        auto& file = scanned[index];
        if (!file.error.empty()) {
            result.issues.push_back({paths[index], std::move(file.error)});
            continue;
        }
        ++result.processed;
        if (!file.input) continue;
        const auto fileIndex = result.files.size();
        result.files.push_back(std::move(*file.input));
        std::ranges::sort(file.matches, [](const Match& lhs, const Match& rhs) {
            return std::tie(lhs.range.firstLine, lhs.range.lastLine) <
                std::tie(rhs.range.firstLine, rhs.range.lastLine);
        });
        for (const auto& match : file.matches) {
            auto& hits = entries[match.symbol]->second;
            auto& group = match.function ? hits.functions : hits.conditions;
            if (group.empty() || group.back() != Hit{fileIndex, match.range}) {
                group.push_back({fileIndex, match.range});
            }
        }
    }
    return result;
}

inline void writeHits(std::ostream& output, const Result& result,
                      const std::vector<Hit>& hits, const fs::path& root) {
    for (const auto& hit : hits) {
        const auto& source = result.files[hit.file];
        const auto relative = source.path.lexically_relative(root);
        output << pathText(relative.empty() ? source.path : relative) << ':'
               << hit.range.firstLine << '-' << hit.range.lastLine << '\n';
        const auto start = source.lines[hit.range.firstLine - 1];
        const auto end = hit.range.lastLine < source.lines.size()
            ? source.lines[hit.range.lastLine] : source.text.size();
        output.write(source.text.data() + start, static_cast<std::streamsize>(end - start));
        if (end > start && source.text[end - 1] != '\n') output << '\n';
        output << '\n';
    }
    output << "Matches: " << hits.size() << '\n';
}

inline fs::path exportRun(const Result& result, const fs::path& root,
                          std::string_view prefix, const fs::path& outputRoot = "Output") {
    const auto validComponent = [](std::string_view text) {
        return !text.empty() && source_parser::isIdentifierStart(text.front()) &&
            std::ranges::all_of(text, [](unsigned char ch) { return source_parser::isIdentifierPart(ch); });
    };
    if (!validComponent(prefix)) throw std::invalid_argument("Invalid export prefix");
    for (const auto& entry : result.symbols) {
        if (!validComponent(entry.first)) throw std::invalid_argument("Invalid symbol in export");
    }
    fs::create_directories(outputRoot);
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    fs::path stage;
    fs::path published;
    for (size_t suffix = 0;; ++suffix) {
        const auto base = std::format("{}_{}_{}", prefix, stamp, suffix);
        published = outputRoot / base;
        stage = outputRoot / (base + ".partial");
        if (!fs::exists(published) && fs::create_directory(stage)) {
            if (!fs::exists(published)) break;
            fs::remove(stage);
        }
    }
    try {
        for (const auto& [symbol, hits] : result.symbols) {
            const auto stem = std::format("{}_{}", prefix, symbol);
            auto uniqueStem = stem;
            for (size_t suffix = 2; fs::exists(stage / pathFromText(uniqueStem + "_DEFINE.txt")); ++suffix) {
                uniqueStem = std::format("{}-{}", stem, suffix);
            }
            for (const auto& [kind, group] : {std::pair{"DEFINE", &hits.conditions},
                                                std::pair{"FUNC", &hits.functions}}) {
                const auto filename = std::format("{}_{}.txt", uniqueStem, kind);
                std::ofstream output;
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output.open(stage / pathFromText(filename), std::ios::binary);
                writeHits(output, result, *group, root);
                output.close();
            }
        }
        fs::rename(stage, published);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(stage, ignored);
        throw;
    }
    return published;
}

inline std::unordered_set<std::string> collectPythonParameters(
    const std::vector<fs::path>& paths, std::vector<FileIssue>& issues) {
    std::unordered_set<std::string> parameters;
    for (const auto& path : paths) {
        try {
            const auto input = readSource(path);
            source_parser::parsePython(input.text, input.lines,
                [&](std::string_view name) -> std::optional<size_t> {
                    parameters.emplace(name);
                    return std::nullopt;
                },
                [](size_t, SourceRange, bool) {});
        } catch (const std::ios_base::failure& error) {
            issues.push_back({path, error.what()});
        }
    }
    return parameters;
}

} // namespace extraction
