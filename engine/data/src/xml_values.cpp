#include "alamo/data/xml_values.hpp"

#include <charconv>
#include <cmath>
#include <limits>

#include "alamo/data/values.hpp"

namespace alamo::data::xml_values {

namespace {
bool c_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool hexdigit(char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
bool starts_ci(std::string_view s, std::string_view p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (lower(s[i]) != p[i]) return false;
    return true;
}
}  // namespace

int32_t c_atoi(std::string_view s) { return parse_int_prefix(s).value_or(0); }

double c_atof(std::string_view s) {
    size_t i = 0;
    while (i < s.size() && c_space(s[i])) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    const std::string_view r = s.substr(i);
    const double sign = neg ? -1.0 : 1.0;
    if (starts_ci(r, "inf")) return sign * std::numeric_limits<double>::infinity();
    if (starts_ci(r, "nan")) return std::numeric_limits<double>::quiet_NaN();
    // Hex: "0x" followed by a hex digit (or '.' + hex digit).
    if (r.size() > 2 && r[0] == '0' && lower(r[1]) == 'x' &&
        (hexdigit(r[2]) || (r[2] == '.' && r.size() > 3 && hexdigit(r[3])))) {
        double v = 0;
        const auto res = std::from_chars(r.data() + 2, r.data() + r.size(), v, std::chars_format::hex);
        if (res.ec == std::errc::result_out_of_range) return 0.0 * sign;  // rare; treated as underflow
        return res.ec == std::errc() ? sign * v : 0.0;
    }
    // Decimal prefix: digits [. digits] [e [+-] digits]; at least one mantissa digit.
    size_t j = 0, mant = 0, int_digits = 0;
    bool seen_nonzero = false;
    while (j < r.size() && digit(r[j])) {
        if (r[j] != '0') seen_nonzero = true;
        if (seen_nonzero) ++int_digits;
        ++j, ++mant;
    }
    int lead_frac_zeros = 0;
    if (j < r.size() && r[j] == '.') {
        ++j;
        while (j < r.size() && digit(r[j])) {
            if (!seen_nonzero) {
                if (r[j] == '0') ++lead_frac_zeros;
                else seen_nonzero = true;
            }
            ++j, ++mant;
        }
    }
    if (mant == 0) return 0.0;
    int64_t exp10 = 0;
    if (j < r.size() && lower(r[j]) == 'e') {
        size_t k = j + 1;
        bool eneg = false;
        if (k < r.size() && (r[k] == '+' || r[k] == '-')) eneg = r[k++] == '-';
        if (k < r.size() && digit(r[k])) {
            for (; k < r.size() && digit(r[k]); ++k)
                if (exp10 < 100000) exp10 = exp10 * 10 + (r[k] - '0');
            if (eneg) exp10 = -exp10;
            j = k;
        }
    }
    double v = 0;
    const auto res = std::from_chars(r.data(), r.data() + j, v, std::chars_format::general);
    if (res.ec == std::errc::result_out_of_range) {
        // Order of magnitude decides overflow (+-HUGE_VAL = inf) versus underflow (+-0).
        const int64_t mag = int_digits > 0 ? static_cast<int64_t>(int_digits) + exp10 : exp10 - lead_frac_zeros;
        return mag > 0 ? sign * std::numeric_limits<double>::infinity() : sign * 0.0;
    }
    return res.ec == std::errc() ? sign * v : 0.0;
}

std::vector<std::string_view> split_list(std::string_view s, bool keep_bar, bool split_bar) {
    std::vector<std::string_view> out;
    size_t i = 0;
    auto delim = [&](char c) {
        return c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n' || ((keep_bar || split_bar) && c == '|');
    };
    while (i < s.size()) {
        while (i < s.size() && delim(s[i])) {
            if (keep_bar && s[i] == '|') out.push_back(s.substr(i, 1));
            ++i;
        }
        const size_t b = i;
        while (i < s.size() && !delim(s[i])) ++i;
        if (i > b) out.push_back(s.substr(b, i - b));
    }
    return out;
}

bool skipped(std::string_view v) {
    v = trim(v);
    return v.empty() || v == "TBD";
}

bool to_bool(std::string_view v) { return parse_type_bool(trim(v)); }

int32_t to_int(std::string_view v) { return c_atoi(v); }

float to_float(std::string_view v) { return static_cast<float>(c_atof(v)); }

std::vector<std::string_view> to_list(std::string_view v) { return split_list(v, false, false); }

std::vector<std::string_view> to_mask(std::string_view v) { return split_list(v, false, true); }

XmlNode last_child(XmlNode e, std::string_view tag) {
    XmlNode last;
    for (XmlNode c : e.children(tag)) last = c;
    return last;
}

std::string_view text(XmlNode e, std::string_view tag) {
    std::string_view out;
    for (XmlNode c : e.children(tag)) {
        const std::string_view v = c.text_trimmed();
        if (!skipped(v)) out = v;
    }
    return out;
}

bool bool_or(XmlNode e, std::string_view tag, bool fallback) {
    const std::string_view v = text(e, tag);
    return v.empty() ? fallback : to_bool(v);
}

int32_t int_or(XmlNode e, std::string_view tag, int32_t fallback) {
    const std::string_view v = text(e, tag);
    return v.empty() ? fallback : to_int(v);
}

float float_or(XmlNode e, std::string_view tag, float fallback) {
    const std::string_view v = text(e, tag);
    return v.empty() ? fallback : to_float(v);
}

}  // namespace alamo::data::xml_values
