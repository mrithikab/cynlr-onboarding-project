#include <iostream>
#include <direct.h>   // _getcwd
#include <limits.h>   // _MAX_PATH
#include <fstream>
#include <vector>
#include <cstdint>
#include "stream/CsvStreamer.h"

static void fail(const std::string &msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}
static void pass(const std::string &msg) {
    std::cout << "PASS: " << msg << "\n";
}


static void printRunInfo(const std::string &path) { //for debugging
    char cwd[_MAX_PATH];
    if (_getcwd(cwd, _MAX_PATH)) {
        std::cout << "CWD: " << cwd << "\n";
    }
    std::ifstream f(path);
    std::cout << "File '" << path << "' exists: " << std::boolalpha << (bool)f << "\n";
    if (f) {
        std::string contents;
        std::getline(f, contents);
        std::cout << "First line contents: '" << contents << "'\n";
    }
}

void testCsvParsing() {
    // Test 1: normal, clamping, malformed
    {

        const std::string path = "test_csv_streamer.csv";
       
        std::ofstream f(path);
        f << "10,20, ,300,-5,abc";
        f.close(); // ensure data flushed and handle released before CsvStreamer opens the file
        CsvStreamer s;
        if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);
        std::vector<std::pair<int,int>> got;
        uint8_t a=0,b=0;
        while (s.nextPair(a,b)) got.emplace_back(a,b);
        s.close();
        if (got.size() != 2) fail("CsvStreamer produced wrong number of pairs: expected 2 got " + std::to_string(got.size()));
        if (got[0].first != 10 || got[0].second != 20) fail("CsvStreamer pair0 mismatch");
        if (got[1].first != 0 || got[1].second != 255) fail("CsvStreamer pair1 mismatch");
        pass("CsvStreamer basic parsing and clamping");
    }
    // Test 2: odd number of tokens
    {
        const std::string path = "test_csv_streamer_odd.csv";
       
        std::ofstream f(path);
        f << "1,2,3";
        f.close();
        CsvStreamer s;
        if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);
        std::vector<std::pair<int,int>> got;
        uint8_t a=0,b=0;
        while (s.nextPair(a,b)) got.emplace_back(a,b);
        s.close();
        if (got.size() != 1) fail("CsvStreamer odd tokens: expected 1 pair");
        if (got[0].first != 1 || got[0].second != 2) fail("CsvStreamer odd tokens: pair mismatch");
        pass("CsvStreamer odd number of tokens");
    }
    // Test 3: empty file
    {
        const std::string path = "test_csv_streamer_empty.csv";
       
        std::ofstream f(path);
        f.close();
        CsvStreamer s;
        if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);
        uint8_t a=0,b=0;
        if (s.nextPair(a,b)) fail("CsvStreamer should not produce pairs for empty file");
        s.close();
        pass("CsvStreamer empty file");
    }
    // Test 4: malformed (non-numeric in first token)
    {
        const std::string path = "test_csv_streamer_malformed.csv";
        
        std::ofstream f(path);
        f << "abc,2,3,4";
        f.close();
        CsvStreamer s;
        if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);
        uint8_t a=0,b=0;
        if (s.nextPair(a,b)) fail("CsvStreamer should fail on malformed first token");
        s.close();
        pass("CsvStreamer malformed first token");
    }
    // Test 5: malformed (non-numeric in second token)
    {
        const std::string path = "test_csv_streamer_malformed2.csv";
        
        std::ofstream f(path);
        f << "1,abc,3,4";
        f.close();
        CsvStreamer s;
        if (!s.open(path)) fail("CsvStreamer.open failed to open " + path);
        uint8_t a=0,b=0;
        if (s.nextPair(a,b)) fail("CsvStreamer should fail on malformed second token");
        s.close();
        pass("CsvStreamer malformed second token");
    }
}

int main() {
    std::cout << "\nRunning CsvStreamer unit tests...\n";
    testCsvParsing();
    std::cout << "All CsvStreamer tests passed.\n";
    return 0;
}
