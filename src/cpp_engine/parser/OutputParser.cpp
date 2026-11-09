#include "OutputParser.hpp"

#include <cstring>
#include <cctype>
#include <regex.h>   /* POSIX ERE — no external dependencies */

namespace cerberus {

std::string_view OutputParser::arenaStr(ca_arena_t* arena, std::string_view sv) {
    if (sv.empty()) return {};
    if (!arena) return sv;
    char* p = static_cast<char*>(ca_arena_alloc(arena, sv.size()));
    if (!p) return sv;  /* arena full — return original view as fallback */
    std::memcpy(p, sv.data(), sv.size());
    return std::string_view(p, sv.size());
}

static std::string_view ltrim(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

static size_t skipString(std::string_view s, size_t pos) {
    if (pos >= s.size() || s[pos] != '"') return 0;
    ++pos;
    while (pos < s.size()) {
        if (s[pos] == '\\') { pos += 2; continue; }
        if (s[pos] == '"')  { return pos + 1; }
        ++pos;
    }
    return 0;
}

static std::string_view firstJSONValue(std::string_view s) {
    size_t i = s.find('{');
    if (i == std::string_view::npos) return {};
    ++i;

    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    size_t key_end = skipString(s, i);
    if (!key_end) return {};

    i = key_end;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i >= s.size() || s[i] != ':') return {};
    ++i;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i >= s.size()) return {};

    size_t val_start = i;
    if (s[i] == '"') {
        size_t val_end = skipString(s, i);
        if (!val_end) return {};
        return s.substr(val_start + 1, val_end - val_start - 2);
    }
    while (i < s.size() && s[i] != ',' && s[i] != '}' &&
           !std::isspace((unsigned char)s[i])) ++i;
    return s.substr(val_start, i - val_start);
}

static std::string_view firstJSONElement(std::string_view s) {
    size_t i = s.find('[');
    if (i == std::string_view::npos) return {};
    ++i;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i >= s.size()) return {};

    size_t start = i;
    if (s[i] == '"') {
        size_t end = skipString(s, i);
        if (!end) return {};
        return s.substr(start + 1, end - start - 2);
    }
    while (i < s.size() && s[i] != ',' && s[i] != ']') ++i;
    return s.substr(start, i - start);
}

bool OutputParser::tryJSON(ca_arena_t* arena, std::string_view input,
                           ParsedResult& out) {
    std::string_view trimmed = ltrim(input);
    if (trimmed.empty()) return false;

    if (trimmed.front() == '{') {
        std::string_view val = firstJSONValue(trimmed);
        out.kind      = ParsedKind::JSON_OBJECT;
        out.raw       = arenaStr(arena, input);
        out.extracted = val.empty() ? out.raw : arenaStr(arena, val);
        return true;
    }

    if (trimmed.front() == '[') {
        std::string_view val = firstJSONElement(trimmed);
        out.kind      = ParsedKind::JSON_ARRAY;
        out.raw       = arenaStr(arena, input);
        out.extracted = val.empty() ? out.raw : arenaStr(arena, val);
        return true;
    }

    if (trimmed.front() == '"' || std::isdigit((unsigned char)trimmed.front()) ||
        trimmed.front() == '-') {
        out.kind      = ParsedKind::SCALAR;
        out.raw       = arenaStr(arena, input);
        out.extracted = arenaStr(arena,
            trimmed.front() == '"' ? trimmed.substr(1, trimmed.size() - 2) : trimmed);
        return true;
    }

    return false;
}

bool OutputParser::tryRegex(ca_arena_t* arena, std::string_view input,
                            std::string_view pattern, ParsedResult& out) {
    if (pattern.empty()) return false;

    char pat_buf[256];
    size_t pat_len = pattern.size() < sizeof(pat_buf) - 1
                     ? pattern.size() : sizeof(pat_buf) - 1;
    std::memcpy(pat_buf, pattern.data(), pat_len);
    pat_buf[pat_len] = '\0';

    regex_t rx;
    if (regcomp(&rx, pat_buf, REG_EXTENDED) != 0) return false;

    char inp_buf[4096];
    size_t inp_len = input.size() < sizeof(inp_buf) - 1
                     ? input.size() : sizeof(inp_buf) - 1;
    std::memcpy(inp_buf, input.data(), inp_len);
    inp_buf[inp_len] = '\0';

    regmatch_t matches[2];
    bool matched = (regexec(&rx, inp_buf, 2, matches, 0) == 0);
    regfree(&rx);

    if (!matched) return false;

    regmatch_t& m = (matches[1].rm_so >= 0) ? matches[1] : matches[0];
    std::string_view extracted = input.substr(
        static_cast<size_t>(m.rm_so),
        static_cast<size_t>(m.rm_eo - m.rm_so));

    out.kind      = ParsedKind::PLAIN_TEXT;
    out.raw       = arenaStr(arena, input);
    out.extracted = arenaStr(arena, extracted);
    return true;
}

ParsedResult OutputParser::parse(ca_arena_t* arena,
                                 std::string_view output,
                                 std::string_view hint) {
    ParsedResult result;
    result.kind      = ParsedKind::PLAIN_TEXT;
    result.raw       = arenaStr(arena, output);
    result.extracted = result.raw;

    if (tryJSON(arena, output, result))  return result;
    if (!hint.empty() &&
        tryRegex(arena, output, hint, result)) return result;

    return result;
}

const char* OutputParser::kindName(ParsedKind kind) {
    switch (kind) {
        case ParsedKind::JSON_OBJECT: return "JSON_OBJECT";
        case ParsedKind::JSON_ARRAY:  return "JSON_ARRAY";
        case ParsedKind::SCALAR:      return "SCALAR";
        case ParsedKind::PLAIN_TEXT:  return "PLAIN_TEXT";
    }
    return "UNKNOWN";
}

} // namespace cerberus
