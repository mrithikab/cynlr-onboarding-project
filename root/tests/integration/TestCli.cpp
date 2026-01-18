#include <iostream>
#include <cstdlib>
#include <string>
#include <fstream>
#include <limits.h>

// Helper to run a command and check exit code
int run_cmd(const std::string& cmd, int expectedExit = 0) {
    int ret = std::system(cmd.c_str());
    if (ret != expectedExit) {
        std::cerr << "FAIL: Command returned " << ret << " but expected " << expectedExit << ": " << cmd << "\n";
    } else {
        std::cout << "PASS: " << cmd << " (exit " << ret << ")\n";
    }
    return ret;
}

static bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.is_open();
}

// Resolve absolute path to tested exe
static std::string resolve_exe_path() {
    // Helper: test candidate path; returns absolute if exists
    auto try_candidate = [](const char* rel) -> std::string {
        char absPath[_MAX_PATH];
        if (_fullpath(absPath, rel, _MAX_PATH) == nullptr) return std::string();
        std::ifstream f(absPath);
        if (f.is_open()) {
            f.close();
            return std::string(absPath);
        }
        return std::string();
    };

    // Release package layout and common build paths
    const char* candidates[] = {
        "..\\..\\cynlr-onboarding-project.exe",     // From tests/integration/
        "..\\..\\..\\cynlr-onboarding-project.exe", // From x64/Release/tests/integration/
        ".\\cynlr-onboarding-project.exe",          // Same directory
        ".\\x64\\Release\\cynlr-onboarding-project.exe",
        ".\\x64\\Debug\\cynlr-onboarding-project.exe",
        ".\\Release\\cynlr-onboarding-project.exe",
        ".\\Debug\\cynlr-onboarding-project.exe"
    };

    for (auto c : candidates) {
        std::string found = try_candidate(c);
        if (!found.empty()) return found;
    }

    // Fallback for local dev environment
    const std::string hardcoded = "C:\\Users\\Mrithika\\source\\repos\\cynlr-onboarding-project\\x64\\Release\\cynlr-onboarding-project.exe";
    if (file_exists(hardcoded)) {
        return hardcoded;
    }

    // Last resort
    return std::string("cynlr-onboarding-project.exe");
}

int main() {
    std::cout << "Running CLI integration tests...\n";
    std::string exe = resolve_exe_path();

    if (!file_exists(exe)) {
        std::cerr << "[TestCli] ERROR: Executable not found: " << exe << "\n";
        std::cerr << "[TestCli] Ensure executable is in the correct location.\n";
        return 1;
    }

    std::cout << "[TestCli] Using executable: " << exe << "\n\n";

    // All commands use --quiet to suppress verbose output
    std::string quiet = " --quiet";

    // 1. Valid CSV input (expect success)
    {
        std::ofstream f("cli_test.csv");
        f << "1,2,3,4";
        f.close();
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=cli_test.csv --threshold=100 --T_ns=1000" + quiet;
        run_cmd(cmd, 0);
    }
    
    // 2. Malformed CSV input (expect success but program may print parse error then exit 0)
    {
        std::ofstream f("cli_test_bad.csv");
        f << "1,abc,3,4";
        f.close();
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=cli_test_bad.csv --threshold=100 --T_ns=1000" + quiet;
        run_cmd(cmd, 0);
    }
    
    // 3. Missing CSV file (expected failure)
    {
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=notfound.csv --threshold=100 --T_ns=1000" + quiet;
        run_cmd(cmd, 1);
    }
    
    // 4. Valid filter file
    {
        std::ofstream f("cli_test_kernel.txt");
        f << "0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9";
        f.close();
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=cli_test.csv --filter=file --filterfile=cli_test_kernel.txt --threshold=100 --T_ns=1000" + quiet;
        run_cmd(cmd, 0);
    }
    
    // 5. Malformed filter file (app falls back to default kernel and returns 0)
    {
        std::ofstream f("cli_test_kernel_bad.txt");
        f << "0.1 0.2 abc 0.4 0.5 0.6 0.7 0.8 0.9";
        f.close();
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=cli_test.csv --filter=file --filterfile=cli_test_kernel_bad.txt --threshold=100 --T_ns=1000" + quiet;
        run_cmd(cmd, 0);
    }
    
    // 6. Unknown CLI argument (expected failure)
    {
        std::string cmd = "\"" + exe + "\" --unknownflag" + quiet;
        run_cmd(cmd, 1);
    }
    
    // 7. Help flag (don't add --quiet, should show usage)
    {
        std::string cmd = "\"" + exe + "\" --help";
        run_cmd(cmd, 1);
    }
    
    // 8. Stats flag enabled
    {
        std::string cmd = "\"" + exe + "\" --mode=csv --csv=cli_test.csv --threshold=100 --T_ns=1000 --stats" + quiet;
        run_cmd(cmd, 0);
    }
    
    std::cout << "CLI integration tests completed.\n";
    return 0;
}
