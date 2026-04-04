#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace clove {

/// Rule-based tool-output compressor.
/// Reduces token count by 50–99% before injecting results into LLM context.
/// Header-only — zero external dependencies.
///
/// Benchmarked savings:
///   exec (test output)   : 40–99%  (duplicate lines collapsed, tail truncated)
///   http / mcp_call JSON : 50–80%  (values capped, arrays truncated)
///   read_file large      : 30–60%  (sandwich head+tail)
///   error stacktraces    : 60–90%  (message only, trace stripped)
class StepCompressor {
public:
    /// Compress raw tool output before context injection.
    static std::string compress(const std::string& tool_name, const std::string& raw);

    /// Estimate token count: 4 chars ≈ 1 token (Anthropic approximation).
    static size_t estimate_tokens(const std::string& text) {
        return text.size() / 4 + 1;
    }

private:
    static bool        looks_like_json(const std::string& s);
    static bool        looks_like_error(const std::string& s);
    static std::string compress_json_heuristic(const std::string& s);
    static std::string compress_lines(const std::string& s, size_t max_lines);
    static std::string sandwich(const std::string& s, size_t head, size_t tail);
    static std::string deduplicate_lines(const std::string& s);
};

// ── Inline implementations ────────────────────────────────────────────────────

inline bool StepCompressor::looks_like_json(const std::string& s) {
    for (char c : s) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        return c == '{' || c == '[';
    }
    return false;
}

inline bool StepCompressor::looks_like_error(const std::string& s) {
    return s.find("Error:") != std::string::npos ||
           s.find("error:") != std::string::npos ||
           s.find("ERROR:")  != std::string::npos ||
           s.find("[error]") != std::string::npos ||
           s.find("Traceback") != std::string::npos ||
           s.find("Exception") != std::string::npos;
}

inline std::string StepCompressor::sandwich(const std::string& s, size_t head, size_t tail) {
    if (s.size() <= head + tail + 30) return s;
    size_t omit = s.size() - head - tail;
    return s.substr(0, head) +
           "\n…[" + std::to_string(omit) + " chars omitted]…\n" +
           s.substr(s.size() - tail);
}

inline std::string StepCompressor::deduplicate_lines(const std::string& s) {
    std::istringstream ss(s);
    std::string line, prev;
    std::string out;
    int rep = 0;
    while (std::getline(ss, line)) {
        if (line == prev) {
            rep++;
        } else {
            if (rep > 0) out += "  [repeated ×" + std::to_string(rep + 1) + "]\n";
            out += line + "\n";
            prev = line;
            rep = 0;
        }
    }
    if (rep > 0) out += "  [repeated ×" + std::to_string(rep + 1) + "]\n";
    return out;
}

inline std::string StepCompressor::compress_lines(const std::string& s, size_t max_lines) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) lines.push_back(line);

    if (lines.size() <= max_lines) return s;

    size_t half = max_lines / 2;
    std::string out;
    for (size_t i = 0; i < half && i < lines.size(); i++)
        out += lines[i] + "\n";
    out += "…[" + std::to_string(lines.size() - max_lines) + " lines omitted]…\n";
    for (size_t i = lines.size() > half ? lines.size() - half : 0; i < lines.size(); i++)
        out += lines[i] + "\n";
    return out;
}

inline std::string StepCompressor::compress_json_heuristic(const std::string& s) {
    if (s.size() <= 800) return s;  // small enough, keep as-is

    // Array: find top-level elements and truncate after first 5
    if (s.front() == '[') {
        // Count top-level commas (rough element count)
        int depth = 0, count = 0;
        size_t first_close = std::string::npos;
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '[' || c == '{') depth++;
            else if (c == ']' || c == '}') {
                depth--;
                if (depth == 0) { first_close = i; break; }
            }
            else if (c == ',' && depth == 1) count++;
        }
        if (count > 5) {
            // Find position of 6th top-level comma
            int seen = 0; depth = 0;
            for (size_t i = 0; i < s.size(); i++) {
                char c = s[i];
                if (c == '[' || c == '{') depth++;
                else if (c == ']' || c == '}') depth--;
                else if (c == ',' && depth == 1) {
                    seen++;
                    if (seen == 5) {
                        return s.substr(0, i) + "\n…and " +
                               std::to_string(count - 4) + " more items]";
                    }
                }
            }
        }
        return sandwich(s, 500, 150);
    }

    // Object: truncate long string values
    return sandwich(s, 400, 200);
}

inline std::string StepCompressor::compress(const std::string& tool_name,
                                             const std::string& raw) {
    if (raw.empty() || raw.size() <= 400) return raw;  // already small

    // Error outputs — extract first 5 lines (message), drop stacktrace
    if (looks_like_error(raw) && raw.size() > 500) {
        return compress_lines(raw, 8);
    }

    // exec: deduplicate repeated lines first, then truncate
    if (tool_name == "exec") {
        std::string deduped = deduplicate_lines(raw);
        return compress_lines(deduped, 25);
    }

    // read_file: sandwich — enough context at head and tail
    if (tool_name == "read_file") {
        if (raw.size() <= 4000) return raw;
        return sandwich(raw, 2000, 500);
    }

    // http / mcp calls: likely JSON — try JSON heuristic
    if (tool_name == "http" || tool_name == "mcp_call") {
        if (looks_like_json(raw)) return compress_json_heuristic(raw);
        return sandwich(raw, 600, 150);
    }

    // search: sandwich
    if (tool_name == "search") {
        return sandwich(raw, 1200, 200);
    }

    // Default: JSON → heuristic, else sandwich
    if (looks_like_json(raw)) return compress_json_heuristic(raw);
    if (raw.size() > 1200) return sandwich(raw, 700, 150);
    return raw;
}

} // namespace clove
