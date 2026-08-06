#pragma once

// =============================================================================
// scenario.hpp — Line-oriented scenario DSL runner (file or interactive stdin).
//
// Purpose: parse NEW / CANCEL / SNAPSHOT / CHECK_* / QUIT commands, drive one
// MatchingEngine, print events via event_format, and count CHECK failures.
//
// DSL:
//   NEW <id> <BUY|SELL> <LIMIT|MARKET> <qty> <price>
//   CANCEL <id>
//   SNAPSHOT <depth>
//   CHECK_BEST_BID <price> <qty>
//   CHECK_BEST_ASK <price> <qty>
//   CHECK_TRADE_COUNT <n>     (cumulative trades since session start)
//   CHECK_NO_CROSS
//   HELP                      (interactive)
//   QUIT | EXIT               (end session; also Ctrl-D / EOF)
// =============================================================================

#include "event_format.hpp"
#include "matching_engine.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lob {

// Outcome of running one scenario stream / file.
struct ScenarioResult {
  int checks_failed{0};           // failed CHECK_* (or unknown-command / parse errors)
  int lines_run{0};               // non-empty, non-comment command lines executed
  std::uint64_t trades_seen{0};   // cumulative Trade events in this run
};

namespace detail {

inline std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

inline Side parse_side(const std::string& s) {
  if (s == "BUY") return Side::Buy;
  if (s == "SELL") return Side::Sell;
  throw std::runtime_error("bad side: " + s);
}

inline OrderType parse_type(const std::string& s) {
  if (s == "LIMIT") return OrderType::Limit;
  if (s == "MARKET") return OrderType::Market;
  throw std::runtime_error("bad type: " + s);
}

inline void print_snapshot(const MatchingEngine& engine, std::size_t depth,
                           std::ostream& out) {
  out << "SNAPSHOT depth=" << depth << '\n';
  for (const auto& lvl : engine.bids(depth)) {
    out << "  BID px=" << lvl.price << " qty=" << lvl.total_qty
        << " n=" << lvl.order_count << '\n';
  }
  for (const auto& lvl : engine.asks(depth)) {
    out << "  ASK px=" << lvl.price << " qty=" << lvl.total_qty
        << " n=" << lvl.order_count << '\n';
  }
}

inline bool check_best(const std::vector<BookLevel>& side, std::int64_t price,
                       std::int64_t qty, std::string* why) {
  if (qty == 0) {
    if (!side.empty() && side[0].total_qty != 0) {
      *why = "expected empty/zero best, got qty=" + std::to_string(side[0].total_qty);
      return false;
    }
    return true;
  }
  if (side.empty()) {
    *why = "expected best level, side empty";
    return false;
  }
  if (side[0].price != price || side[0].total_qty != qty) {
    *why = "expected " + std::to_string(qty) + "@" + std::to_string(price) +
           " got " + std::to_string(side[0].total_qty) + "@" +
           std::to_string(side[0].price);
    return false;
  }
  return true;
}

inline void print_help(std::ostream& out) {
  out << "Commands:\n"
      << "  NEW <id> <BUY|SELL> <LIMIT|MARKET> <qty> <price>\n"
      << "  CANCEL <id>\n"
      << "  SNAPSHOT <depth>\n"
      << "  CHECK_BEST_BID <price> <qty>\n"
      << "  CHECK_BEST_ASK <price> <qty>\n"
      << "  CHECK_TRADE_COUNT <n>\n"
      << "  CHECK_NO_CROSS\n"
      << "  HELP\n"
      << "  QUIT | EXIT   (or Ctrl-D)\n";
}

}  // namespace detail

// Run commands from any input stream (file or std::cin).
// If interactive=true, prints "> " prompts to stderr.
inline ScenarioResult run_scenario(std::istream& in, std::ostream& out = std::cout,
                                   bool interactive = false) {
  MatchingEngine engine;
  ScenarioResult result;
  std::string line;
  int line_no = 0;

  if (interactive) {
    std::cerr << "Interactive matcher. Type HELP for commands, QUIT or Ctrl-D to exit.\n";
  }

  while (true) {
    if (interactive) {
      std::cerr << "> " << std::flush;
    }
    if (!std::getline(in, line)) {
      break;  // EOF (Ctrl-D) or stream end
    }
    ++line_no;
    line = detail::trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    try {
      if (cmd == "QUIT" || cmd == "EXIT") {
        break;
      }
      if (cmd == "HELP") {
        detail::print_help(out);
        ++result.lines_run;
        continue;
      }
      if (cmd == "NEW") {
        std::uint64_t id{};
        std::string side_s, type_s;
        std::int64_t qty{}, price{};
        iss >> id >> side_s >> type_s >> qty >> price;
        OrderRequest req{id, detail::parse_side(side_s), detail::parse_type(type_s),
                         price, qty};
        const auto events = engine.submit(req);
        for (const auto& ev : events) {
          out << format_event(ev) << '\n';
          if (std::holds_alternative<TradeEvent>(ev)) {
            ++result.trades_seen;
          }
        }
      } else if (cmd == "CANCEL") {
        std::uint64_t id{};
        iss >> id;
        for (const auto& ev : engine.cancel(id)) {
          out << format_event(ev) << '\n';
        }
      } else if (cmd == "SNAPSHOT") {
        std::size_t depth{};
        iss >> depth;
        detail::print_snapshot(engine, depth, out);
      } else if (cmd == "CHECK_BEST_BID") {
        std::int64_t price{}, qty{};
        iss >> price >> qty;
        std::string why;
        if (!detail::check_best(engine.bids(1), price, qty, &why)) {
          out << "CHECK_FAIL line " << line_no << " CHECK_BEST_BID: " << why << '\n';
          ++result.checks_failed;
        }
      } else if (cmd == "CHECK_BEST_ASK") {
        std::int64_t price{}, qty{};
        iss >> price >> qty;
        std::string why;
        if (!detail::check_best(engine.asks(1), price, qty, &why)) {
          out << "CHECK_FAIL line " << line_no << " CHECK_BEST_ASK: " << why << '\n';
          ++result.checks_failed;
        }
      } else if (cmd == "CHECK_TRADE_COUNT") {
        std::uint64_t n{};
        iss >> n;
        if (result.trades_seen != n) {
          out << "CHECK_FAIL line " << line_no << " CHECK_TRADE_COUNT: expected "
              << n << " got " << result.trades_seen << '\n';
          ++result.checks_failed;
        }
      } else if (cmd == "CHECK_NO_CROSS") {
        if (!engine.book().is_uncrossed()) {
          out << "CHECK_FAIL line " << line_no << " CHECK_NO_CROSS\n";
          ++result.checks_failed;
        }
      } else {
        out << "CHECK_FAIL line " << line_no << " unknown command: " << cmd << '\n';
        ++result.checks_failed;
      }
      ++result.lines_run;
    } catch (const std::exception& ex) {
      out << "CHECK_FAIL line " << line_no << " parse/run: " << ex.what() << '\n';
      ++result.checks_failed;
    }
  }

  return result;
}

// Open `path` and run as a non-interactive scenario. Throws if open fails.
inline ScenarioResult run_scenario_file(const std::string& path,
                                        std::ostream& out = std::cout) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open scenario: " + path);
  }
  return run_scenario(in, out, /*interactive=*/false);
}

}  // namespace lob
