#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <direct.h>
#include <limits.h>
#include <fstream>

static bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.is_open();
}

int run_test(const std::string& exe) {
    if (!file_exists(exe)) {
        std::cerr << "SKIP/ERROR: executable not found: " << exe << "\n";
        return 1;
    }

    // Quote the executable path to handle spaces
    std::string cmd = "\"" + exe + "\"";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "FAIL: " << exe << " (exit code " << ret << ")\n";
    } else {
        std::cout << "PASS: " << exe << "\n";
    }
    return ret;
}

int main() {
    // Print the directory the program starts in
    char cwd[_MAX_PATH];
    if (_getcwd(cwd, _MAX_PATH) != nullptr) {
        std::cout << "Starting directory: " << cwd << "\n";
    } else {
        std::cout << "Starting directory: <unknown>\n";
    }

    std::cout << "Running all unit/integration tests...\n";

    std::vector<std::string> tests = {
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\unit\\TestCsvStreamer.exe",
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\unit\\TestFilterBlock.exe",
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\unit\\TestFilterBlockCalc.exe",
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\unit\\TestDataGenerator.exe",
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\integration\\IntegrationTestDataGenerator.exe",
        "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\tests\\integration\\TestCli.exe"
    };

    int failures = 0;
    for (const auto& exe : tests) {
        int ret = run_test(exe);
        if (ret != 0) ++failures;
    }
    if (failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
}