#pragma once
#include <string>

// ---------------------------------------------------------------------------
// getIndent()
//   Returns the leading whitespace depth of a line.
//   Tabs count as 4 spaces.
//   Stops at the first non-whitespace character — does NOT scan the whole line.
// ---------------------------------------------------------------------------
inline int getIndent(const std::string& ln)
{
    int indent = 0;
    for (char c : ln) {
        if      (c == ' ')  ++indent;
        else if (c == '\t') indent += 4;
        else                break;   // stop at first non-whitespace
    }
    return indent;
}

// ---------------------------------------------------------------------------
// isAnyIfStart()
//   Returns true when a preprocessor line opens a new conditional block:
//   #if, #ifdef, #ifndef  (with optional leading whitespace).
//   Does NOT match #elif, #endif, #include, or code lines.
//   Replaces anyIfStartRegex to avoid per-line regex overhead inside blocks.
// ---------------------------------------------------------------------------
inline bool isAnyIfStart(const std::string& line)
{
    // Skip optional leading whitespace
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    // Must start with '#'
    if (i >= line.size() || line[i] != '#') return false;
    ++i;

    // Skip whitespace between '#' and keyword
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    // Match "if ", "ifdef ", "ifndef " — require trailing space/tab/end so
    // we do not accidentally match "#if" inside a longer token.
    auto startsWith = [&](const char* kw, size_t kwLen) -> bool {
        if (i + kwLen > line.size()) return false;
        if (line.compare(i, kwLen, kw, kwLen) != 0) return false;
        // next char must be whitespace or end-of-string (not alphanumeric/_)
        if (i + kwLen < line.size()) {
            char next = line[i + kwLen];
            if (next != ' ' && next != '\t') return false;
        }
        return true;
    };

    return startsWith("ifdef",  5)
        || startsWith("ifndef", 6)
        || startsWith("if",     2);   // checked last so "ifdef"/"ifndef" match first
}

// ---------------------------------------------------------------------------
// shouldRunDefineRegex()
//   Pre-filter: returns true only when a line MIGHT contain a conditional
//   directive for the given define name.
//   Requirement: line must contain BOTH '#' AND the define name substring.
//   This avoids calling the expensive std::regex on the ~99.9% of lines that
//   are ordinary code.
// ---------------------------------------------------------------------------
inline bool shouldRunDefineRegex(const std::string& line, const std::string& defineName)
{
    if (line.find('#') == std::string::npos)          return false;
    if (line.find(defineName) == std::string::npos)   return false;
    return true;
}
