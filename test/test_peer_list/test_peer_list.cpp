#include <unity.h>

#include <string>
#include <vector>

#include "PeerList.h"

using namespace drehklang::btlink;

void setUp() {}
void tearDown() {}

namespace {

Address addr(uint8_t last) { return {0xA0, 1, 2, 3, 4, last}; }

}  // namespace

void test_a_new_peer_goes_to_the_front() {
  PeerList list;
  list.add(addr(1), "Major V");
  list.add(addr(2), "AirPods");
  TEST_ASSERT_EQUAL(2, list.peers().size());
  TEST_ASSERT_EQUAL_STRING("AirPods", list.peers()[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Major V", list.peers()[1].name.c_str());
}

void test_adding_a_known_peer_moves_it_and_takes_the_new_name() {
  PeerList list;
  list.add(addr(1), "Major V");
  list.add(addr(2), "AirPods");
  list.add(addr(1), "MAJOR V");
  TEST_ASSERT_EQUAL(2, list.peers().size());
  TEST_ASSERT_EQUAL_STRING("MAJOR V", list.peers()[0].name.c_str());
}

// The bond of the one that falls off must be removed as well.
void test_a_fifth_peer_evicts_the_least_recently_used() {
  PeerList list;
  for (uint8_t i = 1; i <= kMaxPeers; ++i) {
    TEST_ASSERT_FALSE(list.add(addr(i), "P" + std::to_string(i)).has_value());
  }
  list.touch(addr(1));  // Order now 1, 4, 3, 2: 2 is the oldest.
  const auto evicted = list.add(addr(9), "New");
  TEST_ASSERT_TRUE(evicted.has_value());
  TEST_ASSERT_EQUAL_STRING("P2", evicted->name.c_str());
  TEST_ASSERT_EQUAL(kMaxPeers, list.peers().size());
  TEST_ASSERT_EQUAL_STRING("New", list.peers()[0].name.c_str());
}

void test_touch_moves_a_known_peer_and_says_whether_the_order_changed() {
  PeerList list;
  list.add(addr(1), "A");
  list.add(addr(2), "B");
  TEST_ASSERT_FALSE(list.touch(addr(2)));  // Already first.
  TEST_ASSERT_TRUE(list.touch(addr(1)));
  TEST_ASSERT_EQUAL_STRING("A", list.peers()[0].name.c_str());
  TEST_ASSERT_FALSE(list.touch(addr(7)));  // Unknown.
  TEST_ASSERT_EQUAL(2, list.peers().size());
}

void test_remove_forgets_one_peer() {
  PeerList list;
  list.add(addr(1), "A");
  list.add(addr(2), "B");
  TEST_ASSERT_TRUE(list.remove(addr(1)));
  TEST_ASSERT_FALSE(list.remove(addr(1)));
  TEST_ASSERT_EQUAL(1, list.peers().size());
  TEST_ASSERT_EQUAL(-1, list.indexOf(addr(1)));
  TEST_ASSERT_EQUAL(0, list.indexOf(addr(2)));
}

void test_the_list_round_trips_through_bytes() {
  PeerList list;
  list.add(addr(1), "Major V");
  list.add(addr(2), "Kim\xE2\x80\x99s Earbuds");
  std::vector<uint8_t> bytes = list.serialize();
  PeerList back;
  TEST_ASSERT_TRUE(back.deserialize(bytes.data(), bytes.size()));
  TEST_ASSERT_EQUAL(2, back.peers().size());
  TEST_ASSERT_EQUAL_STRING("Kim\xE2\x80\x99s Earbuds", back.peers()[0].name.c_str());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(addr(1).data(), back.peers()[1].address.data(), 6);
}

void test_damaged_bytes_leave_the_list_empty() {
  PeerList list;
  list.add(addr(1), "Major V");
  std::vector<uint8_t> bytes = list.serialize();
  bytes.pop_back();
  PeerList back;
  TEST_ASSERT_FALSE(back.deserialize(bytes.data(), bytes.size()));
  TEST_ASSERT_EQUAL(0, back.peers().size());
  const uint8_t tooMany[] = {kMaxPeers + 1};
  TEST_ASSERT_FALSE(back.deserialize(tooMany, sizeof(tooMany)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_new_peer_goes_to_the_front);
  RUN_TEST(test_adding_a_known_peer_moves_it_and_takes_the_new_name);
  RUN_TEST(test_a_fifth_peer_evicts_the_least_recently_used);
  RUN_TEST(test_touch_moves_a_known_peer_and_says_whether_the_order_changed);
  RUN_TEST(test_remove_forgets_one_peer);
  RUN_TEST(test_the_list_round_trips_through_bytes);
  RUN_TEST(test_damaged_bytes_leave_the_list_empty);
  return UNITY_END();
}
