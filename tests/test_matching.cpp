// =============================================================================
// test_matching.cpp — Hand-rolled golden tests G1–G9 (zero third-party deps).
//
// Purpose: exercise MatchingEngine with scripted sequences that lock the
// matching rules. Scenario .txt twins under tests/scenarios/ are for the CLI.
// Exit non-zero if any expect() fails.
// =============================================================================

#include "matching_engine.hpp"
#include "types.hpp"

#include <iostream>
#include <string>
#include <variant>
#include <vector>

namespace {

int g_failures = 0;  // global failure count for this process

// Record pass/fail for one assertion; prints ok/FAIL lines.
void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  } else {
    std::cout << "ok: " << msg << '\n';
  }
}

// G1: Non-marketable limits rest; one-sided / uncrossed book OK.
void test_g1_rest_only() {
  std::cout << "\n=== G1 Rest only ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Buy, OrderType::Limit, 100, 10});
  engine.submit({2, Side::Sell, OrderType::Limit, 101, 10});

  const auto bids = engine.bids(5);
  const auto asks = engine.asks(5);
  expect(!bids.empty() && bids[0].price == 100 && bids[0].total_qty == 10,
         "G1: best bid 10@100");
  expect(!asks.empty() && asks[0].price == 101 && asks[0].total_qty == 10,
         "G1: best ask 10@101");
}

// G6: Cancel resting order; second/unknown cancel → Reject UnknownOrderId.
void test_g6_cancel() {
  std::cout << "\n=== G6 Cancel ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Buy, OrderType::Limit, 100, 10});
  auto ev = engine.cancel(1);
  expect(std::holds_alternative<lob::CancelledEvent>(ev[0]), "G6: Cancelled");
  expect(engine.bids(1).empty(), "G6: book empty");
  expect(std::holds_alternative<lob::RejectedEvent>(engine.cancel(1)[0]),
         "G6: second cancel Rejected");
}

// G2: Exact full fill at maker price; book and ids cleared.
void test_g2_exact_fill() {
  std::cout << "\n=== G2 Exact full fill ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Buy, OrderType::Limit, 100, 10});

  auto ev = engine.submit({2, Side::Sell, OrderType::Limit, 100, 10});
  expect(ev.size() == 2, "G2: Accept + one Trade");
  expect(std::holds_alternative<lob::AcceptedEvent>(ev[0]), "G2: Accepted first");
  expect(std::holds_alternative<lob::TradeEvent>(ev[1]), "G2: Trade second");

  if (std::holds_alternative<lob::TradeEvent>(ev[1])) {
    const auto& t = std::get<lob::TradeEvent>(ev[1]);
    expect(t.maker_id == 1, "G2: maker is resting bid id 1");
    expect(t.taker_id == 2, "G2: taker is aggressor id 2");
    expect(t.price == 100, "G2: trade at maker price 100");
    expect(t.qty == 10, "G2: trade qty 10");
  }

  expect(engine.bids(5).empty() && engine.asks(5).empty(), "G2: book empty");
  expect(!engine.book().contains(1) && !engine.book().contains(2),
         "G2: both ids gone from index");
}

// G3: Partial fill; maker residual rests; taker does not rest.
void test_g3_partial_fill() {
  std::cout << "\n=== G3 Partial fill + rest ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Buy, OrderType::Limit, 100, 10});

  auto ev = engine.submit({2, Side::Sell, OrderType::Limit, 100, 4});
  expect(ev.size() == 2, "G3: Accept + one Trade");
  expect(std::holds_alternative<lob::TradeEvent>(ev[1]), "G3: Trade present");

  if (std::holds_alternative<lob::TradeEvent>(ev[1])) {
    const auto& t = std::get<lob::TradeEvent>(ev[1]);
    expect(t.maker_id == 1 && t.taker_id == 2, "G3: maker=1 taker=2");
    expect(t.price == 100 && t.qty == 4, "G3: trade 4@100");
  }

  const auto bids = engine.bids(5);
  expect(engine.asks(5).empty(), "G3: no asks (taker fully filled)");
  expect(!bids.empty(), "G3: bid residual remains");
  if (!bids.empty()) {
    expect(bids[0].price == 100 && bids[0].total_qty == 6, "G3: residual 6@100");
    expect(bids[0].order_count == 1, "G3: still one order at 100");
  }
  expect(engine.book().contains(1), "G3: maker id still on book");
  expect(!engine.book().contains(2), "G3: taker id not resting");
}

// G4: Walk multiple ask levels (price priority) with residual at last level.
void test_g4_walk_book() {
  std::cout << "\n=== G4 Walk the book ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Sell, OrderType::Limit, 101, 10});
  engine.submit({2, Side::Sell, OrderType::Limit, 102, 10});
  engine.submit({3, Side::Sell, OrderType::Limit, 103, 10});

  auto ev = engine.submit({4, Side::Buy, OrderType::Limit, 103, 25});

  // Accept + 3 trades
  expect(ev.size() == 4, "G4: Accept + 3 Trades");

  int trade_i = 0;
  const std::int64_t expect_px[] = {101, 102, 103};
  const std::int64_t expect_qty[] = {10, 10, 5};
  const std::uint64_t expect_maker[] = {1, 2, 3};

  for (std::size_t i = 1; i < ev.size(); ++i) {
    expect(std::holds_alternative<lob::TradeEvent>(ev[i]), "G4: event is Trade");
    if (!std::holds_alternative<lob::TradeEvent>(ev[i])) continue;
    const auto& t = std::get<lob::TradeEvent>(ev[i]);
    expect(t.price == expect_px[trade_i] && t.qty == expect_qty[trade_i],
           "G4: trade price/qty step " + std::to_string(trade_i));
    expect(t.maker_id == expect_maker[trade_i] && t.taker_id == 4,
           "G4: maker/taker step " + std::to_string(trade_i));
    ++trade_i;
  }
  expect(trade_i == 3, "G4: exactly 3 trades");

  expect(engine.bids(5).empty(), "G4: buy fully filled, no bid rest");
  const auto asks = engine.asks(5);
  expect(!asks.empty() && asks[0].price == 103 && asks[0].total_qty == 5,
         "G4: residual 5@103 on ask");
  expect(engine.book().contains(3) && !engine.book().contains(1) &&
             !engine.book().contains(2) && !engine.book().contains(4),
         "G4: only id 3 remains");
}

// G5: FIFO at one price — older resting id fills before newer.
void test_g5_fifo() {
  std::cout << "\n=== G5 FIFO same price ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Sell, OrderType::Limit, 100, 3});
  engine.submit({2, Side::Sell, OrderType::Limit, 100, 10});

  auto ev = engine.submit({3, Side::Buy, OrderType::Limit, 100, 5});
  // Accept + trade 3 from id1 + trade 2 from id2
  expect(ev.size() == 3, "G5: Accept + 2 Trades");

  expect(std::holds_alternative<lob::TradeEvent>(ev[1]), "G5: first trade");
  expect(std::holds_alternative<lob::TradeEvent>(ev[2]), "G5: second trade");
  if (std::holds_alternative<lob::TradeEvent>(ev[1]) &&
      std::holds_alternative<lob::TradeEvent>(ev[2])) {
    const auto& t1 = std::get<lob::TradeEvent>(ev[1]);
    const auto& t2 = std::get<lob::TradeEvent>(ev[2]);
    expect(t1.maker_id == 1 && t1.qty == 3 && t1.price == 100,
           "G5: first fills older id1 for 3");
    expect(t2.maker_id == 2 && t2.qty == 2 && t2.price == 100,
           "G5: then newer id2 for 2");
  }

  const auto asks = engine.asks(5);
  expect(!asks.empty() && asks[0].total_qty == 8 && asks[0].order_count == 1,
         "G5: id2 residual 8@100");
  expect(!engine.book().contains(1) && engine.book().contains(2),
         "G5: id1 gone, id2 remains");
}

// G8: After every op, book stays uncrossed (aggressive limit must match first).
void test_g8_no_cross() {
  std::cout << "\n=== G8 No crossed book ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  auto check = [&](const std::string& step) {
    expect(engine.book().is_uncrossed(), "G8: uncrossed after " + step);
  };

  engine.submit({1, Side::Sell, OrderType::Limit, 101, 10});
  check("rest ask");

  engine.submit({2, Side::Buy, OrderType::Limit, 100, 5});
  check("rest bid below ask");

  // Would cross if rested blindly at 105; matching must clear through 101.
  engine.submit({3, Side::Buy, OrderType::Limit, 105, 10});
  check("aggressive buy @105");
  expect(engine.asks(5).empty(), "G8: ask consumed");
  expect(!engine.bids(5).empty() && engine.bids(5)[0].price == 100,
         "G8: passive bid 100 still there (105 fully matched asks)");

  engine.submit({4, Side::Sell, OrderType::Limit, 99, 5});
  check("sell that hits bid @100");

  engine.cancel(2);
  check("cancel");
}

// G7: Market vs thin book — take all liquidity, cancel residual, never rest.
void test_g7_market_thin() {
  std::cout << "\n=== G7 Market vs thin book ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Sell, OrderType::Limit, 101, 10});
  engine.submit({2, Side::Sell, OrderType::Limit, 102, 20});

  // price=0 ignored for market
  auto ev = engine.submit({3, Side::Buy, OrderType::Market, 0, 100});

  expect(std::holds_alternative<lob::AcceptedEvent>(ev[0]), "G7: Accepted");
  if (std::holds_alternative<lob::AcceptedEvent>(ev[0])) {
    expect(std::get<lob::AcceptedEvent>(ev[0]).price == 0, "G7: market ACCEPT price 0");
  }

  std::int64_t traded = 0;
  bool saw_residual_cancel = false;
  for (std::size_t i = 1; i < ev.size(); ++i) {
    if (std::holds_alternative<lob::TradeEvent>(ev[i])) {
      traded += std::get<lob::TradeEvent>(ev[i]).qty;
    } else if (std::holds_alternative<lob::CancelledEvent>(ev[i])) {
      const auto& c = std::get<lob::CancelledEvent>(ev[i]);
      expect(c.qty == 70 && c.reason == lob::CancelReason::Residual,
             "G7: residual cancel 70");
      saw_residual_cancel = true;
    }
  }
  expect(traded == 30, "G7: traded all 30 ask liquidity");
  expect(saw_residual_cancel, "G7: residual Cancelled present");
  expect(engine.asks(5).empty() && engine.bids(5).empty(), "G7: book empty");
  expect(!engine.book().contains(3), "G7: market id does not rest");
  expect(engine.book().is_uncrossed(), "G7: still uncrossed");
}

// G9: Invalid qty / duplicate id rejected; existing book unchanged.
void test_g9_rejects() {
  std::cout << "\n=== G9 Reject bad input ===\n";
  lob::MatchingEngine engine;
  using lob::Side;
  using lob::OrderType;

  engine.submit({1, Side::Buy, OrderType::Limit, 100, 10});
  const auto bids_before = engine.bids(5);

  auto bad_qty = engine.submit({2, Side::Buy, OrderType::Limit, 100, 0});
  expect(std::holds_alternative<lob::RejectedEvent>(bad_qty[0]), "G9: qty 0 Rejected");
  if (std::holds_alternative<lob::RejectedEvent>(bad_qty[0])) {
    expect(std::get<lob::RejectedEvent>(bad_qty[0]).reason == lob::RejectReason::InvalidQty,
           "G9: InvalidQty");
  }

  auto neg_qty = engine.submit({2, Side::Sell, OrderType::Limit, 101, -5});
  expect(std::holds_alternative<lob::RejectedEvent>(neg_qty[0]), "G9: neg qty Rejected");

  auto dup = engine.submit({1, Side::Sell, OrderType::Limit, 101, 5});
  expect(std::holds_alternative<lob::RejectedEvent>(dup[0]), "G9: duplicate id Rejected");
  if (std::holds_alternative<lob::RejectedEvent>(dup[0])) {
    expect(std::get<lob::RejectedEvent>(dup[0]).reason == lob::RejectReason::DuplicateId,
           "G9: DuplicateId");
  }

  const auto bids_after = engine.bids(5);
  expect(!bids_after.empty() && bids_after[0].price == bids_before[0].price &&
             bids_after[0].total_qty == bids_before[0].total_qty,
         "G9: book unchanged after rejects");
  expect(engine.book().contains(1) && !engine.book().contains(2),
         "G9: only original id present");
}

}  // namespace

// Run all golden tests; return 1 if any assertion failed.
int main() {
  std::cout << "lob_tests — G1–G9\n";
  test_g1_rest_only();
  test_g6_cancel();
  test_g2_exact_fill();
  test_g3_partial_fill();
  test_g4_walk_book();
  test_g5_fifo();
  test_g8_no_cross();
  test_g7_market_thin();
  test_g9_rejects();

  if (g_failures != 0) {
    std::cerr << "\n" << g_failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "\nAll checks passed.\n";
  return 0;
}
