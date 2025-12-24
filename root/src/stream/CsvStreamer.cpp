#include "stream/CsvStreamer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>

CsvStreamer::~CsvStreamer() {
    close();
}

bool CsvStreamer::open(const std::string& path) {
    close();
    in_.open(path);
    opened_ = in_.is_open();
    return opened_;
}

void CsvStreamer::close() {
    if (in_.is_open()) in_.close();
    opened_ = false;
}

void CsvStreamer::trim(std::string& s) {
    auto not_ws = [](int ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
}

bool CsvStreamer::parseClampedUint8(const std::string& s, uint8_t& out) {
    if (s.empty()) { out = 0; return true; }
    try {
        long v = std::stol(s);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out = static_cast<uint8_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool CsvStreamer::nextPair(uint8_t& a, uint8_t& b) {
    if (!opened_) return false;
    std::string t1, t2;
    // read first token
    if (!std::getline(in_, t1, ',')) {
        // EOF or error
        return false;
    }
    trim(t1);
    if (!parseClampedUint8(t1, a)) {
        std::cerr << "CsvStreamer: parse error for token '" << t1 << "'\n";
        close();
        return false;
    }

    // read second token
    if (!std::getline(in_, t2, ',')) {
        // odd number of tokens: drop the final single sample per policy
        return false;
    }
    trim(t2);
    if (!parseClampedUint8(t2, b)) {
        std::cerr << "CsvStreamer: parse error for token '" << t2 << "'\n";
        close();
        return false;
    }

    return true;
}

int CsvStreamer::probeColumns(const std::string& path) {
    std::ifstream in(path);
    if (!in) return 0;

    std::string line;
    while (std::getline(in, line)) {
        // trim whitespace
        auto not_ws = [](int ch) { return !std::isspace(ch); };
        auto first = std::find_if(line.begin(), line.end(), not_ws);
        if (first == line.end()) continue; // empty line

        // count commas -> columns = commas+1
        int commas = 0;
        for (char c : line) if (c == ',') ++commas;
        return commas + 1;
    }
    return 0;
}