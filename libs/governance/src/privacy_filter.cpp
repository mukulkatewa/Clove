#include <clove/privacy_filter.hpp>

#include <algorithm>

namespace clove {

// ---------------------------------------------------------------------------
// Built-in PII patterns
// ---------------------------------------------------------------------------

struct BuiltinPattern {
    const char* name;
    PiiType     type;
    const char* regex;
};

static const BuiltinPattern kBuiltinPatterns[] = {
    {"ssn",         PiiType::SSN,
     R"(\b\d{3}-\d{2}-\d{4}\b)"},

    {"email",       PiiType::EMAIL,
     R"(\b[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}\b)"},

    {"phone",       PiiType::PHONE,
     R"(\b(\+?\d{1,3}[-.\s]?)?\(?\d{3}\)?[-.\s]?\d{3}[-.\s]?\d{4}\b)"},

    {"credit_card", PiiType::CREDIT_CARD,
     R"(\b\d{4}[-\s]?\d{4}[-\s]?\d{4}[-\s]?\d{4}\b)"},

    {"ip_address",  PiiType::IP_ADDRESS,
     R"(\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b)"},
};

// ---------------------------------------------------------------------------
// Mode conversion
// ---------------------------------------------------------------------------

PrivacyMode PrivacyFilter::mode_from_string(const std::string& s) {
    if (s == "redact") return PrivacyMode::REDACT;
    if (s == "block")  return PrivacyMode::BLOCK;
    return PrivacyMode::AUDIT;
}

const char* PrivacyFilter::mode_to_string(PrivacyMode m) {
    switch (m) {
    case PrivacyMode::AUDIT:  return "audit";
    case PrivacyMode::REDACT: return "redact";
    case PrivacyMode::BLOCK:  return "block";
    }
    return "audit";
}

// ---------------------------------------------------------------------------
// Construction & configuration
// ---------------------------------------------------------------------------

PrivacyFilter::PrivacyFilter() {
    // Patterns are compiled lazily — only when configure() is called with
    // enabled=true.  This avoids ~1 MB of std::regex DFA compilation at boot
    // when privacy filtering is not requested.
}

void PrivacyFilter::configure(const PrivacyFilterConfig& config) {
    config_ = config;
    if (config_.enabled) {
        build_patterns();
    } else {
        patterns_.clear();
    }
}

void PrivacyFilter::build_patterns() {
    patterns_.clear();

    // Built-in patterns
    for (const auto& builtin : kBuiltinPatterns) {
        bool enabled = false;
        for (const auto& name : config_.enabled_patterns) {
            if (name == builtin.name) {
                enabled = true;
                break;
            }
        }
        if (enabled) {
            patterns_.push_back({
                builtin.type,
                builtin.name,
                std::regex(builtin.regex, std::regex::ECMAScript | std::regex::optimize),
            });
        }
    }

    // Custom patterns
    for (const auto& [name, regex_str] : config_.custom_patterns) {
        try {
            patterns_.push_back({
                PiiType::CUSTOM,
                name,
                std::regex(regex_str, std::regex::ECMAScript | std::regex::optimize),
            });
        } catch (const std::regex_error&) {
            // Skip invalid patterns silently — in production, log a warning
        }
    }
}

// ---------------------------------------------------------------------------
// Type name helper
// ---------------------------------------------------------------------------

static std::string pii_type_name(PiiType type, const std::string& custom_name) {
    switch (type) {
    case PiiType::SSN:         return "SSN";
    case PiiType::EMAIL:       return "EMAIL";
    case PiiType::PHONE:       return "PHONE";
    case PiiType::CREDIT_CARD: return "CREDIT_CARD";
    case PiiType::IP_ADDRESS:  return "IP_ADDRESS";
    case PiiType::CUSTOM:      return custom_name;
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

std::vector<PiiMatch> PrivacyFilter::scan(const std::string& text) const {
    std::vector<PiiMatch> matches;

    for (const auto& entry : patterns_) {
        auto it  = std::sregex_iterator(text.begin(), text.end(), entry.regex);
        auto end = std::sregex_iterator();

        for (; it != end; ++it) {
            const auto& m = *it;
            matches.push_back({
                entry.type,
                m.str(),
                static_cast<size_t>(m.position()),
                static_cast<size_t>(m.position() + m.length()),
                pii_type_name(entry.type, entry.name),
            });
        }
    }

    // Sort by position for stable output
    std::sort(matches.begin(), matches.end(),
              [](const PiiMatch& a, const PiiMatch& b) {
                  return a.start_pos < b.start_pos;
              });

    return matches;
}

// ---------------------------------------------------------------------------
// Redaction
// ---------------------------------------------------------------------------

PrivacyFilter::RedactResult PrivacyFilter::redact(const std::string& text) const {
    auto matches = scan(text);

    if (matches.empty()) {
        return {text, {}};
    }

    // Build redacted text by replacing matches from end to start
    // (so positions remain valid)
    std::string result = text;

    // Process in reverse order to preserve positions
    for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
        std::string replacement = "[REDACTED:" + it->type_name + "]";
        result.replace(it->start_pos, it->end_pos - it->start_pos, replacement);
    }

    return {std::move(result), std::move(matches)};
}

// ---------------------------------------------------------------------------
// Fast PII check
// ---------------------------------------------------------------------------

bool PrivacyFilter::contains_pii(const std::string& text) const {
    for (const auto& entry : patterns_) {
        if (std::regex_search(text, entry.regex)) {
            return true;
        }
    }
    return false;
}

} // namespace clove
