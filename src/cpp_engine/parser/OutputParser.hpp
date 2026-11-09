#pragma once

#include <string_view>
#include <cstddef>

extern "C" {
    #include "allocator.h"
}

namespace geryon {

/* Detected format of a tool's output string. */
enum class ParsedKind {
    JSON_OBJECT,  /* Starts with '{', valid JSON object. */
    JSON_ARRAY,   /* Starts with '[', valid JSON array.  */
    SCALAR,       /* Plain JSON number or quoted string.  */
    PLAIN_TEXT    /* Anything else — unstructured text.   */
};

/*
 * Result of parsing one tool output.
 * Both string_view fields point into the provided arena — zero copy.
 * 'extracted' is the most useful sub-value (first JSON field value, or raw
 * text); 'raw' is always the original unmodified output.
 */
struct ParsedResult {
    ParsedKind       kind;
    std::string_view raw;        /* original tool output */
    std::string_view extracted;  /* simplified value for the next plan step */
};

/*
 * OutputParser — fast, arena-allocated tool-output parser.
 *
 * Priority order:
 *   1. JSON object  — extracts the value of the first key found.
 *   2. JSON array   — extracts the first element as a string.
 *   3. Scalar JSON  — number or quoted string, used as-is.
 *   4. Plain text   — returned verbatim; extracted == raw.
 *
 * No external JSON library is used. The parser is intentionally minimal:
 * it only needs to classify output and pull one representative value for
 * the planning context, not perform full structural traversal.
 */
class OutputParser {
public:
    /*
     * Parse 'output' into a ParsedResult.
     * String data is copied into 'arena' so the result lifetime is tied
     * to the arena, not the original string.
     * 'hint' is an optional regex pattern (POSIX ERE); if provided and
     * the JSON paths don't match, tryRegex() is attempted before
     * falling back to PLAIN_TEXT.
     */
    static ParsedResult parse(ca_arena_t* arena,
                              std::string_view output,
                              std::string_view hint = {});

    /* Returns a human-readable name for a ParsedKind value. */
    static const char* kindName(ParsedKind kind);

private:
    static bool tryJSON (ca_arena_t* arena, std::string_view input,
                         ParsedResult& out);
    static bool tryRegex(ca_arena_t* arena, std::string_view input,
                         std::string_view pattern, ParsedResult& out);

    /* Copy a string_view into the arena, returning a view into arena memory. */
    static std::string_view arenaStr(ca_arena_t* arena, std::string_view sv);
};

} // namespace geryon
