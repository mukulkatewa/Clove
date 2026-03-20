#pragma once

#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace clove {

enum class PrivacyMode { AUDIT, REDACT, BLOCK };
enum class PiiType { SSN, EMAIL, PHONE, CREDIT_CARD, IP_ADDRESS, CUSTOM };

struct PiiMatch {
    PiiType     type;
    std::string matched_text;
    size_t      start_pos;
    size_t      end_pos;
    std::string type_name;
};

struct PrivacyFilterConfig {
    bool        enabled = false;
    PrivacyMode mode    = PrivacyMode::AUDIT;

    std::vector<std::string> enabled_patterns = {
        "ssn", "email", "phone", "credit_card", "ip_address"};

    // Custom patterns: name -> regex string
    std::vector<std::pair<std::string, std::string>> custom_patterns;
};

class PrivacyFilter {
public:
    PrivacyFilter();

    void configure(const PrivacyFilterConfig& config);

    [[nodiscard]] bool        is_enabled() const { return config_.enabled; }
    [[nodiscard]] PrivacyMode mode() const { return config_.mode; }

    /// Scan text for PII, return all matches.
    [[nodiscard]] std::vector<PiiMatch> scan(const std::string& text) const;

    /// Redact PII from text, return cleaned text + matches found.
    struct RedactResult {
        std::string         cleaned_text;
        std::vector<PiiMatch> matches;
    };
    [[nodiscard]] RedactResult redact(const std::string& text) const;

    /// Check if text contains PII (fast check, no details).
    [[nodiscard]] bool contains_pii(const std::string& text) const;

    static PrivacyMode  mode_from_string(const std::string& s);
    static const char*  mode_to_string(PrivacyMode m);

private:
    PrivacyFilterConfig config_;

    struct PatternEntry {
        PiiType     type;
        std::string name;
        std::regex  regex;
    };
    std::vector<PatternEntry> patterns_;

    void build_patterns();
};

} // namespace clove
