#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Thousands-separated number formatting for CLI output.
inline std::string WithCommas(std::int64_t value) {
  std::string digits = std::to_string(value < 0 ? -value : value);
  std::string out = value < 0 ? "-" : "";
  const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i >= lead && (i - lead) % 3 == 0) {
      out += ',';
    }
    out += digits[i];
  }
  return out;
}

inline std::string Money(double value) {
  char fraction[8];
  double magnitude = value < 0 ? -value : value;
  std::snprintf(fraction, sizeof(fraction), "%.2f",
                magnitude - static_cast<std::int64_t>(magnitude));
  std::string out = value < 0 ? "-£" : "£";
  out += WithCommas(static_cast<std::int64_t>(magnitude));
  out += fraction + 1; // ".xx"
  return out;
}
