#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "DefineExtractor/helpers.h"

// ============================================================
//  getIndent()
// ============================================================

TEST_CASE("getIndent returns 0 for empty string", "[getIndent]") {
    REQUIRE(getIndent("") == 0);
}

TEST_CASE("getIndent returns 0 for line with no leading whitespace", "[getIndent]") {
    REQUIRE(getIndent("hello world") == 0);
}

TEST_CASE("getIndent counts spaces correctly", "[getIndent]") {
    REQUIRE(getIndent("    hello") == 4);
    REQUIRE(getIndent("  x") == 2);
}

TEST_CASE("getIndent counts tabs as 4 spaces", "[getIndent]") {
    REQUIRE(getIndent("\thello") == 4);
    REQUIRE(getIndent("\t\thello") == 8);
}

TEST_CASE("getIndent mixes spaces and tabs", "[getIndent]") {
    REQUIRE(getIndent("  \thello") == 6);  // 2 spaces + 1 tab
}

TEST_CASE("getIndent stops scanning at first non-whitespace character", "[getIndent]") {
    // Trailing whitespace/spaces must NOT be counted.
    // The stub iterates full string so trailing spaces would add to result.
    REQUIRE(getIndent("    hello    ") == 4);  // NOT 8
    REQUIRE(getIndent("\thello\t\t") == 4);    // NOT 12
}

// ============================================================
//  isAnyIfStart()
// ============================================================

TEST_CASE("isAnyIfStart detects #if", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#if SOMETHING") == true);
    REQUIRE(isAnyIfStart("  #if SOMETHING") == true);   // leading whitespace
}

TEST_CASE("isAnyIfStart detects #ifdef", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#ifdef MY_DEFINE") == true);
    REQUIRE(isAnyIfStart("    #ifdef MY_DEFINE") == true);
}

TEST_CASE("isAnyIfStart detects #ifndef", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#ifndef MY_DEFINE") == true);
}

TEST_CASE("isAnyIfStart does NOT match #elif", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#elif SOMETHING") == false);
}

TEST_CASE("isAnyIfStart does NOT match #endif", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#endif") == false);
}

TEST_CASE("isAnyIfStart does NOT match plain code lines", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("int x = 5;") == false);
    REQUIRE(isAnyIfStart("// #if comment") == false);
    REQUIRE(isAnyIfStart("") == false);
}

TEST_CASE("isAnyIfStart does NOT match #include", "[isAnyIfStart]") {
    REQUIRE(isAnyIfStart("#include <windows.h>") == false);
}

// ============================================================
//  shouldRunDefineRegex()
// ============================================================

TEST_CASE("shouldRunDefineRegex returns true when line has # and define name", "[shouldRunDefineRegex]") {
    REQUIRE(shouldRunDefineRegex("#ifdef MY_DEFINE", "MY_DEFINE") == true);
    REQUIRE(shouldRunDefineRegex("#if defined(MY_DEFINE)", "MY_DEFINE") == true);
    REQUIRE(shouldRunDefineRegex("#elif MY_DEFINE", "MY_DEFINE") == true);
}

TEST_CASE("shouldRunDefineRegex returns false when line has no #", "[shouldRunDefineRegex]") {
    // Line mentions define name but is not a preprocessor directive
    REQUIRE(shouldRunDefineRegex("// MY_DEFINE comment", "MY_DEFINE") == false);
    REQUIRE(shouldRunDefineRegex("int x = MY_DEFINE;", "MY_DEFINE") == false);
}

TEST_CASE("shouldRunDefineRegex returns false when line has no define name", "[shouldRunDefineRegex]") {
    REQUIRE(shouldRunDefineRegex("#ifdef OTHER_THING", "MY_DEFINE") == false);
    REQUIRE(shouldRunDefineRegex("#if defined(UNRELATED)", "MY_DEFINE") == false);
}

TEST_CASE("shouldRunDefineRegex returns false for empty line", "[shouldRunDefineRegex]") {
    REQUIRE(shouldRunDefineRegex("", "MY_DEFINE") == false);
}

TEST_CASE("shouldRunDefineRegex returns false for plain code without define", "[shouldRunDefineRegex]") {
    REQUIRE(shouldRunDefineRegex("int foo() { return 42; }", "MY_DEFINE") == false);
}
