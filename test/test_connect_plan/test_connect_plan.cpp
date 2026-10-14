#include <unity.h>

#include "ConnectPlan.h"
#include "PeerList.h"

using namespace drehklang::btlink;

void setUp() {}
void tearDown() {}

namespace {

Address addr(uint8_t last) { return {0xA0, 1, 2, 3, 4, last}; }

// Most recently used first: B, then A.
PeerList twoPeers() {
  PeerList list;
  list.add(addr(1), "A");
  list.add(addr(2), "B");
  return list;
}

}  // namespace

void test_nothing_to_try_without_peers() {
  ConnectPlan plan;
  PeerList empty;
  TEST_ASSERT_FALSE(plan.next(empty, 0).has_value());
}

// One pair of headphones: retried every 10 s, as before.
void test_one_peer_is_retried_after_the_round_pause() {
  ConnectPlan plan;
  PeerList list;
  list.add(addr(1), "A");
  const auto attempt = plan.next(list, 0);
  TEST_ASSERT_TRUE(attempt.has_value());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(1).data(), attempt->address.data(), 6);
  TEST_ASSERT_EQUAL_UINT32(ConnectPlan::kRoundPauseMs, attempt->pauseMs);
}

// Several: the most recently used first, the next soon after, then a pause.
void test_peers_are_tried_in_turn_most_recent_first() {
  ConnectPlan plan;
  const PeerList list = twoPeers();
  auto first = plan.next(list, 0);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), first->address.data(), 6);
  TEST_ASSERT_EQUAL_UINT32(ConnectPlan::kBetweenPeersMs, first->pauseMs);
  auto second = plan.next(list, 8000);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(1).data(), second->address.data(), 6);
  TEST_ASSERT_EQUAL_UINT32(ConnectPlan::kRoundPauseMs, second->pauseMs);
  auto third = plan.next(list, 20000);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), third->address.data(), 6);
}

void test_a_connection_starts_the_next_round_at_the_front() {
  ConnectPlan plan;
  const PeerList list = twoPeers();
  plan.next(list, 0);
  plan.connected();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), plan.next(list, 100)->address.data(), 6);
}

// The one the user picked is tried, and only it, until it connects or a
// minute has passed.
void test_a_wanted_peer_is_tried_alone_for_a_minute() {
  ConnectPlan plan;
  const PeerList list = twoPeers();
  plan.want(addr(1), 1000);
  for (uint32_t t : {1000u, 9000u, 60999u}) {
    auto attempt = plan.next(list, t);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(1).data(), attempt->address.data(), 6);
    TEST_ASSERT_EQUAL_UINT32(ConnectPlan::kBetweenPeersMs, attempt->pauseMs);
  }
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), plan.next(list, 61000)->address.data(), 6);
}

void test_connecting_ends_the_wish() {
  ConnectPlan plan;
  const PeerList list = twoPeers();
  plan.want(addr(1), 0);
  TEST_ASSERT_TRUE(plan.wanted(10).has_value());
  plan.connected();
  TEST_ASSERT_FALSE(plan.wanted(10).has_value());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), plan.next(list, 10)->address.data(), 6);
}

void test_a_forgotten_peer_is_no_longer_wanted() {
  ConnectPlan plan;
  PeerList list = twoPeers();
  plan.want(addr(1), 0);
  plan.forget(addr(1));
  list.remove(addr(1));
  TEST_ASSERT_FALSE(plan.wanted(10).has_value());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), plan.next(list, 10)->address.data(), 6);
}

// The turn survives the list shrinking under it.
void test_the_turn_wraps_when_the_list_shrinks() {
  ConnectPlan plan;
  PeerList list = twoPeers();
  plan.next(list, 0);  // B; A is next.
  list.remove(addr(1));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(2).data(), plan.next(list, 10)->address.data(), 6);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_to_try_without_peers);
  RUN_TEST(test_one_peer_is_retried_after_the_round_pause);
  RUN_TEST(test_peers_are_tried_in_turn_most_recent_first);
  RUN_TEST(test_a_connection_starts_the_next_round_at_the_front);
  RUN_TEST(test_a_wanted_peer_is_tried_alone_for_a_minute);
  RUN_TEST(test_connecting_ends_the_wish);
  RUN_TEST(test_a_forgotten_peer_is_no_longer_wanted);
  RUN_TEST(test_the_turn_wraps_when_the_list_shrinks);
  return UNITY_END();
}
