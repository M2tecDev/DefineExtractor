#include "DefineExtractor/library.h"

#include <catch2/catch_test_macros.hpp>

namespace {

struct Workspace {
    fs::path root;

    Workspace() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (size_t suffix = 0; root.empty(); ++suffix) {
            const auto candidate = fs::temp_directory_path() /
                std::format("defineextractor-test-{}-{}", stamp, suffix);
            if (fs::create_directory(candidate)) root = candidate;
        }
    }

    ~Workspace() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    fs::path write(const fs::path& name, std::string_view text) const {
        const auto file = root / name;
        fs::create_directories(file.parent_path());
        std::ofstream out;
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out.open(file, std::ios::binary);
        out << text;
        out.close();
        return file;
    }
};

using extraction::Hit;
using extraction::Language;

} // namespace

TEST_CASE("C++ conditions match complete identifiers anywhere in a directive", "[extraction]") {
    const Workspace workspace;
    const auto file = workspace.write("test.cpp",
        "#if FEATURE_EXTRA\n"
        "int extra;\n"
        "#endif\n"
        "#if OTHER && defined(FEATURE)\n"
        "int outer;\n"
        "# if\t!FEATURE || \\\n"
        "     defined(SECOND)\n"
        "int nested;\n"
        "# endif\n"
        "#elifdef FEATURE\n"
        "int alternate;\n"
        "#else\n"
        "int ignored;\n"
        "#endif\n"
        "#if FEAT\\\nURE\n"
        "int split;\n"
        "#endif\n");
    const auto result = extraction::scan({file}, {"FEATURE", "SECOND"}, Language::Cpp, 2);
    CHECK(result.symbols.at("FEATURE").conditions ==
        std::vector<Hit>{{0, {4, 9}}, {0, {6, 8}}, {0, {10, 11}}, {0, {15, 17}}});
    CHECK(result.symbols.at("SECOND").conditions == std::vector<Hit>{{0, {6, 8}}});
}

TEST_CASE("C++ comments and literals do not affect function ranges or directives", "[extraction]") {
    const Workspace workspace;
    const auto file = workspace.write("test.cpp", R"source(
/* #if FEATURE */
const auto raw = R"tag(
#if FEATURE
}
)tag";
int first() {
    const auto number = 1'000;
    const char* text = "FEATURE }";
    // #endif
    return FEATURE;
}
int second() { return OTHER; }
int third()
{
#if defined(FEATURE)
    return 1;
#endif
    return 0;
}
)source");
    const auto result = extraction::scan({file}, {"FEATURE", "OTHER"}, Language::Cpp, 1);
    CHECK(result.symbols.at("FEATURE").conditions == std::vector<Hit>{{0, {16, 17}}});
    CHECK(result.symbols.at("FEATURE").functions == std::vector<Hit>{{0, {7, 12}}, {0, {14, 20}}});
    CHECK(result.symbols.at("OTHER").functions == std::vector<Hit>{{0, {13, 13}}});
}

TEST_CASE("Python discovers and extracts the same conditions", "[extraction]") {
    const Workspace workspace;
    const auto file = workspace.write("root/test.py",
        "async def outer():\n"
        "    '''\n"
        "    if app.DOCSTRING:\n"
        "\n"
        "    '''\n"
        "    if not app.FEATURE:\n"
        "        value = 'if app.STRING:'\n"
        "    elif (app.SECOND\n"
        "          or app.FEATURE):\n"
        "        # if app.COMMENT:\n"
        "        pass\n"
        "    if other.app.INVALID:\n"
        "        pass\n"
        "    def inner():\n"
        "        if app.THIRD: return True\n"
        "    return False\n"
        "# if app.COMMENT:\n"
        "if app.FEATURE_EXTRA: pass\n");
    std::vector<extraction::FileIssue> issues;
    const auto found = extraction::collectPythonParameters({file}, issues);
    CHECK(issues.empty());
    CHECK(found == std::unordered_set<std::string>{"FEATURE", "SECOND", "THIRD", "FEATURE_EXTRA"});
    const auto result = extraction::scan({file}, {"FEATURE", "SECOND", "THIRD", "COMMENT"},
                                         Language::Python, 2);
    CHECK(result.symbols.at("FEATURE").conditions == std::vector<Hit>{{0, {6, 7}}, {0, {8, 11}}});
    CHECK(result.symbols.at("SECOND").conditions == std::vector<Hit>{{0, {8, 11}}});
    CHECK(result.symbols.at("FEATURE").functions == std::vector<Hit>{{0, {1, 16}}});
    CHECK(result.symbols.at("THIRD").functions == std::vector<Hit>{{0, {1, 16}}, {0, {14, 15}}});
    CHECK(result.symbols.at("COMMENT").conditions.empty());
}

TEST_CASE("Batch scans report failures and export reproducible distinct runs", "[extraction]") {
    const Workspace workspace;
    const auto left = workspace.write("src/a/same.cpp", "#if FLAG\nint a;\n#endif\n");
    const auto right = workspace.write("src/b/same.cpp", "#if FLAG\nint b;\n#endif\n");
    const auto empty = workspace.write("src/empty.cpp", "");
    const auto missing = workspace.root / "src/missing.cpp";
    const auto root = workspace.root / "src";
    const auto output = workspace.root / "results";
    const auto serial = extraction::scan({right, missing, left, empty, left}, {"FLAG", "FLAG"}, Language::Cpp, 1);
    const auto parallel = extraction::scan({empty, left, missing, right}, {"FLAG"}, Language::Cpp, 4);
    REQUIRE(serial.issues.size() == 1);
    CHECK(serial.issues.front().path == missing);
    CHECK(serial.processed == 3);
    CHECK(serial.symbols == parallel.symbols);
    const auto first = extraction::exportRun(serial, root, "CLIENT", output);
    const auto second = extraction::exportRun(parallel, root, "CLIENT", output);
    CHECK(first != second);
    const auto contents = extraction::readSource(first / "CLIENT_FLAG_DEFINE.txt").text;
    CHECK(contents == extraction::readSource(second / "CLIENT_FLAG_DEFINE.txt").text);
    CHECK(contents.starts_with("a/same.cpp:1-2"));
    CHECK(contents.find("b/same.cpp:1-2") != std::string::npos);
    CHECK(contents.ends_with("Matches: 2\n"));
    const auto discovered = findSourceFiles(root, Language::Cpp);
    CHECK(discovered.paths.size() == 3);
    CHECK(discovered.issues.empty());
    CHECK(findSourceFiles(root / "missing", Language::Cpp).issues.size() == 1);
}

TEST_CASE("Header discovery ignores fake declarations and deduplicates defines", "[extraction]") {
    const Workspace workspace;
    const auto header = workspace.write("feature.hpp",
        "\xef\xbb\xbf# define\tFEATURE 1\r\n"
        "// #define COMMENT 1\r\n"
        "/*\r\n#define MULTILINE 1\r\n*/\r\n"
        "#define FUNCTION(value) value\r\n"
        "#define FEATURE 1\r\n");
    CHECK(readDefines(extraction::pathText(header)) == std::vector<std::string>{"FEATURE", "FUNCTION"});
}

TEST_CASE("Continuation lines keep physical locations and Unicode paths", "[extraction]") {
    const Workspace workspace;
    const auto cpp = workspace.write(extraction::pathFromText("src/\xc3\xa4/feature.HPP"),
        "#if /* multi\r\nline */ FEATURE\r\n"
        "int first;\r\n"
        "#endif\r\n");
    const auto python = workspace.write("src/feature.PY",
        "if app.\\\n"
        "        FEATURE:\n"
        "    pass");
    const auto root = workspace.root / "src";
    const auto cppFiles = findSourceFiles(root, Language::Cpp);
    const auto pythonFiles = findSourceFiles(root, Language::Python);
    CHECK(cppFiles.paths == std::vector<fs::path>{cpp});
    CHECK(pythonFiles.paths == std::vector<fs::path>{python});
    const auto cppResult = extraction::scan(cppFiles.paths, {"FEATURE"}, Language::Cpp, 1);
    const auto pyResult = extraction::scan(pythonFiles.paths, {"FEATURE"}, Language::Python, 1);
    CHECK(cppResult.symbols.at("FEATURE").conditions == std::vector<Hit>{{0, {1, 3}}});
    CHECK(pyResult.symbols.at("FEATURE").conditions == std::vector<Hit>{{0, {1, 3}}});
    const auto output = extraction::exportRun(cppResult, root, "SERVER", workspace.root / "results");
    CHECK(extraction::readSource(output / "SERVER_FEATURE_DEFINE.txt").text.starts_with("\xc3\xa4/feature.HPP:1-3"));
}
