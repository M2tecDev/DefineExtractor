#pragma once
// library.h — Header-only core library for DefineExtractor
// Provides all parsing, discovery, and extraction functions without console I/O.

#include <string>
#include <vector>
#include <iostream>
#include <format>
#include <algorithm>
#include <cstddef>

#include <filesystem>
namespace fs = std::filesystem;

#ifdef _WIN32
#include <windows.h>
#endif

#include "extraction.h"


// ─────────────────────────────────────────────────────────────────────────────
// Discovery functions
// ─────────────────────────────────────────────────────────────────────────────

inline void findClientHeaderInUserInterface(const fs::path& startPath,
    bool& hasClientHeader, std::string& clientHeaderName)
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

                std::string rel = extraction::pathText(p.path().parent_path());
                std::string lowerRel;
                lowerRel.reserve(rel.size());
                for (char c : rel)
                    lowerRel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerRel.find("userinterface") == std::string::npos) continue;

                std::string fn = extraction::pathText(p.path().filename());
                std::string lowerFn;
                for (char c : fn)
                    lowerFn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerFn == "locale_inc.h") {
                    hasClientHeader = true;
                    clientHeaderName = extraction::pathText(p.path());
                    return;
                }
            } catch (...) { continue; }
        }
    } catch (...) {}
}

inline void findServerHeaderInCommon(const fs::path& startPath,
    bool& hasServerHeader, std::string& serverHeaderName)
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

                std::string rel = extraction::pathText(p.path().parent_path());
                std::string lowerRel;
                lowerRel.reserve(rel.size());
                for (char c : rel)
                    lowerRel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerRel.find("common") == std::string::npos) continue;

                std::string fn = extraction::pathText(p.path().filename());
                std::string lowerFn;
                for (char c : fn)
                    lowerFn.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lowerFn == "service.h" || lowerFn == "commondefines.h") {
                    hasServerHeader = true;
                    serverHeaderName = extraction::pathText(p.path());
                    return;
                }
            } catch (...) { continue; }
        }
    } catch (...) {}
}

inline void findPythonRoots(const fs::path& startPath, std::vector<std::string>& pythonRoots)
{
    pythonRoots.clear();
    try {
        for (const auto& p : fs::directory_iterator(startPath,
            fs::directory_options::skip_permission_denied))
        {
            try {
                if (!fs::is_directory(p.path())) continue;
                auto directory = extraction::pathText(p.path().filename());
                std::ranges::transform(directory, directory.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (directory == "root")
                    pythonRoots.push_back(extraction::pathText(p.path()));
            } catch (...) { continue; }
        }
    } catch (...) {}
}

struct SourceFiles {
    std::vector<fs::path> paths;
    std::vector<extraction::FileIssue> issues;
};

inline SourceFiles findSourceFiles(const fs::path& root, extraction::Language language) {
    static const std::unordered_set<std::string> cppExtensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp",
        ".ixx", ".cppm",
    };
    SourceFiles files;
    std::vector<fs::path> pending{root};
    while (!pending.empty()) {
        auto directory = std::move(pending.back());
        pending.pop_back();
        std::error_code error;
        for (fs::directory_iterator entry(directory, error), end; entry != end;
             entry.increment(error)) {
            const auto& path = entry->path();
            const auto status = entry->symlink_status(error);
            if (error) {
                files.issues.push_back({path, error.message()});
                error.clear();
                continue;
            }
            if (fs::is_directory(status)) {
                pending.push_back(path);
            } else if (fs::is_regular_file(status)) {
                auto extension = extraction::pathText(path.extension());
                std::ranges::transform(extension, extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (language == extraction::Language::Python ? extension == ".py" :
                    cppExtensions.contains(extension)) {
                    files.paths.push_back(path);
                }
            }
        }
        if (error) files.issues.push_back({directory, error.message()});
    }
    std::ranges::sort(files.issues, {}, &extraction::FileIssue::path);
    return files;
}

inline std::vector<std::string> readDefines(const std::string& filename) {
    const auto input = extraction::readSource(extraction::pathFromText(filename));
    auto source = source_parser::logicalSource(input.text, extraction::Language::Cpp);
    source_parser::maskCommentsAndStrings(source.code, extraction::Language::Cpp);
    const std::string_view code(source.code);
    std::vector<std::string> result;
    std::unordered_set<std::string_view> seen;
    for (size_t offset = 0; offset < code.size();) {
        const auto end = source_parser::lineEnd(code, offset);
        const auto line = code.substr(offset, end - offset);
        const auto directive = source_parser::readDirective(line.substr(source_parser::skipWhitespace(line, 0)));
        if (directive.keyword == "define") {
            size_t cursor = 0;
            const auto name = source_parser::nextToken(directive.expression, cursor);
            if (!name.empty() && source_parser::isIdentifierStart(name.front()) && seen.insert(name).second) {
                result.emplace_back(name);
            }
        }
        offset = end + 1;
    }
    return result;
}

inline std::vector<fs::path> getSubdirectoriesOfCurrentPath()
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
