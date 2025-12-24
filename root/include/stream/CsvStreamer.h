// CsvStreamer: simple CSV token/pair streamer.
// - Returns pairs of uint8_t values (two tokens = one pair).
// - Trims whitespace, treats empty token as 0, clamps numeric values to [0,255].
// - nextPair(a,b) returns true when a pair was produced; false on EOF or error.
#pragma once
#include <string>
#include <fstream>

class CsvStreamer {
public:
    CsvStreamer() = default;
    ~CsvStreamer();

    bool open(const std::string& path);
    bool nextPair(uint8_t& a, uint8_t& b);
    void close();

    // Probe CSV: returns number of columns on the first non-empty line, or 0 on error.
    static int probeColumns(const std::string& path);

private:
    std::ifstream in_;
    std::string token_;
    bool opened_ = false;

    static void trim(std::string& s);
    static bool parseClampedUint8(const std::string& s, uint8_t& out);
};