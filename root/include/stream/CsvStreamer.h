//cpp root\include\stream/CsvStreamer.h
#pragma once
#include <string>
#include <fstream>

// Simple CSV token / pair streamer.
// - Opens a CSV file and returns pairs of uint8_t values (two tokens = one pair).
// - Trims whitespace, treats empty token as 0, clamps numeric values to [0,255].
// - nextPair(a,b) returns true when a pair was produced; returns false on EOF or error.
// - Drops a final single leftover token (conservative policy).
class CsvStreamer {
public:
    CsvStreamer() = default;
    ~CsvStreamer();

    // Open the CSV file. Returns true on success.
    bool open(const std::string& path);

    // Read next pair. Returns true if a pair was produced and writes into a/b.
    // On parse error the method returns false and the stream is closed.
    bool nextPair(uint8_t& a, uint8_t& b);

    // Close file (safe to call multiple times)
    void close();

private:
    std::ifstream in_;
    std::string token_;
    bool opened_ = false;

    // Helpers
    static void trim(std::string& s);
    static bool parseClampedUint8(const std::string& s, uint8_t& out);
};