#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "scanner/config.hpp"  // Format

namespace scanner {

// Escape a value for a CSV field (RFC 4180 quoting).
std::string csv_field(const std::string& s);

// Escape a value as a JSON string literal (including surrounding quotes).
std::string json_str(const std::string& s);

// Generic, column-based result writer. `cols` names the columns (used for the
// CSV header and JSON keys); each row supplies a pre-formatted `text_line` for
// text output plus one string per column.
struct OutputWriter {
    std::ofstream f;
    Format fmt = Format::Text;
    bool json_first = true;
    bool suppress_header = false;  // resumed CSV appends onto an existing header
    std::vector<std::string> cols;

    void begin();
    void row(const std::string& text_line, const std::vector<std::string>& vals);
    void flush() { f.flush(); }
    void end();
};

}  // namespace scanner
