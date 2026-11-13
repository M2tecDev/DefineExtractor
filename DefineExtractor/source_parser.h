#pragma once

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace source_parser {

enum class Language { Cpp, Python };

struct SourceRange {
    size_t firstLine;
    size_t lastLine;
    bool operator==(const SourceRange&) const = default;
};

inline bool isIdentifierStart(unsigned char ch) {
    return std::isalpha(ch) || ch == '_' || ch >= 128;
}

inline bool isIdentifierPart(unsigned char ch) {
    return isIdentifierStart(ch) || std::isdigit(ch);
}

inline bool isSpace(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

inline size_t lineEnd(std::string_view text, size_t offset) {
    const auto end = text.find('\n', offset);
    return end == std::string_view::npos ? text.size() : end;
}

inline size_t skipWhitespace(std::string_view text, size_t offset) {
    while (offset < text.size() && isSpace(text[offset])) ++offset;
    return offset;
}

inline std::string_view nextToken(std::string_view text, size_t& offset) {
    offset = skipWhitespace(text, offset);
    if (offset == text.size()) return {};
    const auto start = offset++;
    if (isIdentifierPart(text[start])) {
        while (offset < text.size() && isIdentifierPart(text[offset])) ++offset;
    }
    return text.substr(start, offset - start);
}

struct LogicalSource {
    std::string code;
    std::vector<std::pair<size_t, size_t>> splices;

    size_t physicalOffset(size_t logicalOffset) const {
        const auto after = std::ranges::upper_bound(splices, logicalOffset, {},
            &std::pair<size_t, size_t>::first);
        return logicalOffset + (after == splices.begin() ? 0 : std::prev(after)->second);
    }
};

inline LogicalSource logicalSource(std::string_view text, Language language) {
    LogicalSource source{std::string(text), {}};
    if (language == Language::Cpp && text.find('\\') != std::string_view::npos) {
        source.code.clear();
        size_t copied = 0;
        for (auto slash = text.find('\\'); slash != std::string_view::npos;
             slash = text.find('\\', slash + 1)) {
            auto newline = slash + 1;
            if (newline < text.size() && text[newline] == '\r') ++newline;
            if (newline == text.size() || text[newline] != '\n') continue;
            source.code.append(text.substr(copied, slash - copied));
            copied = newline + 1;
            source.splices.emplace_back(source.code.size(), copied - source.code.size());
        }
        source.code.append(text.substr(copied));
    }
    return source;
}

inline size_t quotedEnd(std::string_view text, size_t start, Language language) {
    const char quote = text[start];
    const bool triple = language == Language::Python && start + 2 < text.size() &&
        text[start + 1] == quote && text[start + 2] == quote;
    const auto delimiter = text.substr(start, triple ? 3 : 1);
    auto offset = start + delimiter.size();
    while (offset < text.size()) {
        if (text[offset] == '\\') {
            ++offset;
            if (offset < text.size() && text[offset] == '\r') ++offset;
            if (offset < text.size()) ++offset;
        } else if (text.substr(offset).starts_with(delimiter)) {
            return offset + delimiter.size();
        } else if (text[offset] == '\n' && !triple) {
            return offset;
        } else {
            ++offset;
        }
    }
    return offset;
}

inline size_t rawStringEnd(std::string_view text, size_t start) {
    const auto delimiterStart = start + 2;
    const auto limit = std::min(text.size() - delimiterStart, size_t{17});
    const auto open = text.substr(delimiterStart, limit).find_first_of("()\\ \t\r\n");
    if (open == std::string_view::npos || text[delimiterStart + open] != '(') return start;
    std::string closing = ")";
    closing.append(text.substr(delimiterStart, open));
    closing += '"';
    const auto end = text.find(closing, delimiterStart + open + 1);
    return end == std::string_view::npos ? text.size() : end + closing.size();
}

inline void maskCommentsAndStrings(std::string& code, Language language,
                                  std::vector<size_t>* literalContinuations = nullptr) {
    const std::string_view text(code);
    for (size_t offset = 0; offset < text.size();) {
        const char ch = text[offset];
        size_t end = offset;
        bool literal = false;
        if (language == Language::Python && ch == '#') {
            end = lineEnd(text, offset);
        } else if (language == Language::Cpp && ch == '/') {
            if (text.substr(offset).starts_with("//")) end = lineEnd(text, offset);
            if (text.substr(offset).starts_with("/*")) {
                const auto close = text.find("*/", offset + 2);
                end = close == std::string_view::npos ? text.size() : close + 2;
            }
        } else if (language == Language::Cpp && ch == 'R' && text.substr(offset).starts_with("R\"")) {
            end = rawStringEnd(text, offset);
            literal = end != offset;
        } else if (ch == '"' || ch == '\'') {
            if (language == Language::Cpp && ch == '\'' && offset > 0 &&
                offset + 1 < text.size() && isIdentifierPart(text[offset - 1]) &&
                isIdentifierPart(text[offset + 1])) {
                ++offset;
                continue;
            }
            end = quotedEnd(text, offset, language);
            literal = true;
        }
        if (end == offset) {
            ++offset;
            continue;
        }
        const auto start = offset;
        const bool cppBlockComment = language == Language::Cpp && text.substr(start).starts_with("/*");
        for (; offset < end; ++offset) {
            if (literal && literalContinuations && code[offset] == '\n') {
                literalContinuations->push_back(offset);
            }
            if (cppBlockComment || (code[offset] != '\n' && code[offset] != '\r')) code[offset] = ' ';
        }
        if (literal) code[start] = '@';
        if (literal && end > start && code[end - 1] != '\n') {
            code[end - 1] = '@';
        }
    }
}

inline std::vector<size_t> lineStarts(std::string_view text) {
    std::vector<size_t> starts{0};
    for (auto newline = text.find('\n'); newline != std::string_view::npos;
         newline = text.find('\n', newline + 1)) {
        if (newline + 1 < text.size()) starts.push_back(newline + 1);
    }
    return starts;
}

inline size_t sourceLine(const std::vector<size_t>& starts, size_t offset) {
    return static_cast<size_t>(std::ranges::upper_bound(starts, offset) - starts.begin());
}

struct Directive {
    std::string_view keyword;
    std::string_view expression;
};

inline Directive readDirective(std::string_view line) {
    if (!line.starts_with('#')) return {};
    size_t cursor = 1;
    const auto keyword = nextToken(line, cursor);
    return {keyword, line.substr(cursor)};
}

inline bool startsCppConditional(std::string_view keyword) {
    return keyword == "if" || keyword == "ifdef" || keyword == "ifndef";
}

inline bool continuesCppConditional(std::string_view keyword) {
    return keyword == "elif" || keyword == "elifdef" || keyword == "elifndef" || keyword == "else";
}

template<typename Reference, typename Emit>
void parseCpp(std::string_view original, const std::vector<size_t>& starts,
              Reference reference, Emit emit) {
    auto source = logicalSource(original, Language::Cpp);
    maskCommentsAndStrings(source.code, Language::Cpp);
    const std::string_view code(source.code);
    const auto lineOf = [&](size_t offset) { return sourceLine(starts, source.physicalOffset(offset)); };

    struct Scope {
        size_t firstLine;
        std::unordered_set<size_t> symbols;
    };
    std::vector<Scope> branches;
    std::optional<Scope> function;
    std::vector<bool> braces;
    size_t statementStart = 0;
    size_t parens = 0;
    bool functionHead = false;
    bool candidateParens = false;
    bool directiveAllowed = true;
    std::string_view previous;
    std::string_view beforePrevious;

    const auto finish = [&](const Scope& scope, size_t lastLine, bool function) {
        for (auto symbol : scope.symbols) {
            emit(symbol, SourceRange{scope.firstLine, lastLine}, function);
        }
    };
    const auto resetHead = [&](size_t offset) {
        statementStart = offset;
        functionHead = false;
        candidateParens = false;
        previous = {};
        beforePrevious = {};
    };

    for (size_t offset = 0; offset < code.size();) {
        const char ch = code[offset];
        if (isSpace(ch)) {
            if (ch == '\n') directiveAllowed = true;
            ++offset;
            continue;
        }
        if (directiveAllowed && ch == '#') {
            const auto end = lineEnd(code, offset);
            const auto directive = readDirective(code.substr(offset, end - offset));
            const auto first = lineOf(offset);
            if (continuesCppConditional(directive.keyword) || directive.keyword == "endif") {
                if (!branches.empty()) {
                    finish(branches.back(), first - 1, false);
                    branches.pop_back();
                }
            }
            if (startsCppConditional(directive.keyword) || directive.keyword == "elif" ||
                directive.keyword == "elifdef" || directive.keyword == "elifndef") {
                Scope branch{first, {}};
                size_t cursor = 0;
                for (auto token = nextToken(directive.expression, cursor); !token.empty();
                     token = nextToken(directive.expression, cursor)) {
                    if (token == "defined" || !isIdentifierStart(token.front())) continue;
                    if (const auto symbol = reference(token)) {
                        branch.symbols.insert(*symbol);
                        if (function) function->symbols.insert(*symbol);
                    }
                    if (directive.keyword.ends_with("def")) break;
                }
                branches.push_back(std::move(branch));
            } else if (directive.keyword == "else") {
                branches.push_back({first, {}});
            }
            if (statementStart <= offset && previous.empty()) statementStart = end;
            offset = end;
            continue;
        }
        directiveAllowed = false;
        const auto tokenStart = offset;
        const auto token = nextToken(code, offset);
        if (isIdentifierStart(token.front()) && function) {
            if (const auto symbol = reference(token)) {
                function->symbols.insert(*symbol);
            }
        }
        if (token == "(") {
            if (parens++ == 0) {
                candidateParens = !previous.empty() && isIdentifierStart(previous.front()) &&
                    previous != "if" && previous != "else" && previous != "for" &&
                    previous != "while" && previous != "switch" && previous != "catch" &&
                    previous != "sizeof" && previous != "decltype" && previous != "static_assert" &&
                    previous != "requires" && beforePrevious != "." && beforePrevious != ">";
            }
        } else if (token == ")" && parens != 0) {
            if (--parens == 0 && candidateParens) functionHead = true;
        } else if (token == "{" && parens == 0) {
            const bool isFunction = functionHead && !function;
            if (isFunction) {
                function = Scope{lineOf(skipWhitespace(code, statementStart)), {}};
            }
            braces.push_back(isFunction);
            resetHead(offset);
            continue;
        } else if (token == "}" && parens == 0) {
            if (!braces.empty()) {
                if (braces.back()) {
                    finish(*function, lineOf(tokenStart), true);
                    function.reset();
                }
                braces.pop_back();
            }
            resetHead(offset);
            continue;
        } else if ((token == ";" || token == "=") && parens == 0) {
            resetHead(offset);
            continue;
        }
        beforePrevious = previous;
        previous = token;
    }
    for (auto& branch : branches) finish(branch, starts.size(), false);
}

inline size_t pythonIndent(std::string_view text) {
    size_t indent = 0;
    for (char ch : text) {
        if (ch == ' ') ++indent;
        else if (ch == '\t') indent += 8 - indent % 8;
        else if (ch == '\f') indent = 0;
        else break;
    }
    return indent;
}

inline std::string_view nextPythonToken(std::string_view text, size_t& offset) {
    auto token = nextToken(text, offset);
    while (token == "\\" && offset < text.size() &&
           (text[offset] == '\r' || text[offset] == '\n')) {
        token = nextToken(text, offset);
    }
    return token;
}

template<typename Reference, typename Emit>
void parsePython(std::string_view original, const std::vector<size_t>& starts,
                 Reference reference, Emit emit) {
    auto source = logicalSource(original, Language::Python);
    std::vector<size_t> literalContinuations;
    maskCommentsAndStrings(source.code, Language::Python, &literalContinuations);
    const std::string_view code(source.code);
    struct Scope {
        size_t indent;
        size_t firstLine;
        std::unordered_set<size_t> symbols;
        bool function;
    };
    std::vector<Scope> scopes;
    size_t lastStatementLine = 0;
    const auto finish = [&](const Scope& scope, size_t lastLine) {
        for (auto symbol : scope.symbols) {
            emit(symbol, SourceRange{scope.firstLine, lastLine}, scope.function);
        }
    };

    for (size_t offset = 0; offset < code.size();) {
        const auto start = offset;
        auto end = lineEnd(code, start);
        const auto initial = code.substr(start, end - start);
        if (skipWhitespace(initial, 0) == initial.size()) {
            offset = std::min(end + 1, code.size());
            continue;
        }
        const auto indent = pythonIndent(initial);
        while (!scopes.empty() && scopes.back().indent >= indent) {
            finish(scopes.back(), lastStatementLine);
            scopes.pop_back();
        }

        size_t nesting = 0;
        for (size_t cursor = start; cursor < end || end < code.size(); ++cursor) {
            if (cursor == end) {
                auto last = end;
                while (last > start && (code[last - 1] == ' ' || code[last - 1] == '\r' ||
                                       code[last - 1] == '\t')) --last;
                if (nesting == 0 && (last == start || code[last - 1] != '\\') &&
                    !std::ranges::binary_search(literalContinuations, end)) break;
                end = lineEnd(code, end + 1);
                continue;
            }
            const auto ch = code[cursor];
            if (ch == '(' || ch == '[' || ch == '{') ++nesting;
            if ((ch == ')' || ch == ']' || ch == '}') && nesting != 0) --nesting;
        }
        offset = std::min(end + 1, code.size());
        lastStatementLine = sourceLine(starts, end);
        const auto first = sourceLine(starts, start);
        const auto statement = code.substr(start, end - start);
        size_t cursor = 0;
        auto keyword = nextToken(statement, cursor);
        if (keyword == "async") keyword = nextToken(statement, cursor);
        const bool function = keyword == "def";
        if (!function && keyword != "if" && keyword != "elif") continue;

        Scope scope{indent, first, {}, function};
        nesting = 0;
        std::string_view previous;
        for (auto token = nextPythonToken(statement, cursor); !token.empty(); token = nextPythonToken(statement, cursor)) {
            if (token == ":" && nesting == 0) break;
            if (token == "(" || token == "[" || token == "{") ++nesting;
            if ((token == ")" || token == "]" || token == "}") && nesting != 0) --nesting;
            if (!function && token == "app" && previous != ".") {
                auto attributeCursor = cursor;
                if (nextPythonToken(statement, attributeCursor) == ".") {
                    const auto name = nextPythonToken(statement, attributeCursor);
                    if (!name.empty() && isIdentifierStart(name.front())) {
                        if (const auto symbol = reference(name)) {
                            scope.symbols.insert(*symbol);
                            for (auto& enclosing : scopes) {
                                if (enclosing.function) enclosing.symbols.insert(*symbol);
                            }
                        }
                    }
                }
            }
            previous = token;
        }
        if (skipWhitespace(statement, cursor) < statement.size()) {
            finish(scope, lastStatementLine);
        } else {
            scopes.push_back(std::move(scope));
        }
    }
    for (auto& scope : scopes) finish(scope, lastStatementLine);
}

} // namespace source_parser
