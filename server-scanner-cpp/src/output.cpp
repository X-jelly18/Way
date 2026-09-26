#include "scanner/output.hpp"

#include <cstdio>

namespace scanner {

std::string csv_field(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

void OutputWriter::begin() {
    if (fmt == Format::Csv && !suppress_header) {
        for (size_t i = 0; i < cols.size(); i++) { if (i) f << ","; f << cols[i]; }
        f << "\n";
    } else if (fmt == Format::Json) {
        f << "[\n";
    }
    // Ndjson has no preamble (each line is a standalone object), so it streams
    // and appends cleanly.
    f.flush();
}

void OutputWriter::row(const std::string& text_line, const std::vector<std::string>& vals) {
    if (fmt == Format::Text) {
        f << text_line << "\n";
    } else if (fmt == Format::Csv) {
        for (size_t i = 0; i < vals.size(); i++) { if (i) f << ","; f << csv_field(vals[i]); }
        f << "\n";
    } else if (fmt == Format::Ndjson) {
        f << "{";
        for (size_t i = 0; i < cols.size() && i < vals.size(); i++) {
            if (i) f << ",";
            f << json_str(cols[i]) << ":" << json_str(vals[i]);
        }
        f << "}\n";
    } else {  // Json
        if (!json_first) f << ",\n";
        json_first = false;
        f << "  {";
        for (size_t i = 0; i < cols.size() && i < vals.size(); i++) {
            if (i) f << ",";
            f << json_str(cols[i]) << ":" << json_str(vals[i]);
        }
        f << "}";
    }
    f.flush();
}

void OutputWriter::end() {
    if (fmt == Format::Json) f << (json_first ? "]\n" : "\n]\n");
    f.flush();
}

}  // namespace scanner
