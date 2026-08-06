// =============================================================================
// main.cpp — CLI entry for the matcher.
//
// Purpose: run a scenario file, or an interactive stdin session.
// Usage:
//   ./matcher                         # interactive (type commands)
//   ./matcher -                       # interactive (explicit stdin)
//   ./matcher <scenario.txt>          # file mode
// Exit: 0 if no CHECK failures; 1 on open/check failure.
// =============================================================================

#include "scenario.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace {

bool is_stdin_arg(const char* arg) {
  return std::strcmp(arg, "-") == 0;
}

int run_and_report(const lob::ScenarioResult& result) {
  if (result.checks_failed != 0) {
    std::cerr << result.checks_failed << " check(s) failed\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    if (argc < 2 || is_stdin_arg(argv[1])) {
      const auto result = lob::run_scenario(std::cin, std::cout, /*interactive=*/true);
      return run_and_report(result);
    }

    const auto result = lob::run_scenario_file(argv[1], std::cout);
    return run_and_report(result);
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
