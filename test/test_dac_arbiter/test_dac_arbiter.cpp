#include <unity.h>

#include "DacArbiter.h"

using drehklang::playback::DacArbiter;
using drehklang::playback::DacOwner;

namespace {
struct CountingOwner : DacOwner {
  int releases = 0;
  void releaseDac() override { ++releases; }
};
}  // namespace

void setUp() {}
void tearDown() {}

void test_first_claim_releases_nobody() {
  DacArbiter arbiter;
  CountingOwner player;
  arbiter.claim(player);
  TEST_ASSERT_TRUE(arbiter.owns(player));
  TEST_ASSERT_EQUAL(0, player.releases);
}

void test_claim_releases_the_previous_owner_once() {
  DacArbiter arbiter;
  CountingOwner player;
  CountingOwner tones;
  arbiter.claim(player);
  arbiter.claim(tones);
  TEST_ASSERT_EQUAL(1, player.releases);
  TEST_ASSERT_EQUAL(0, tones.releases);
  TEST_ASSERT_TRUE(arbiter.owns(tones));
  TEST_ASSERT_FALSE(arbiter.owns(player));
}

void test_reclaiming_by_the_owner_changes_nothing() {
  // Every blip claims; only the first may close the player's channel.
  DacArbiter arbiter;
  CountingOwner player;
  CountingOwner tones;
  arbiter.claim(player);
  arbiter.claim(tones);
  arbiter.claim(tones);
  TEST_ASSERT_EQUAL(1, player.releases);
  TEST_ASSERT_EQUAL(0, tones.releases);
}

void test_handover_back_releases_the_tones() {
  DacArbiter arbiter;
  CountingOwner player;
  CountingOwner tones;
  arbiter.claim(tones);
  arbiter.claim(player);
  TEST_ASSERT_EQUAL(1, tones.releases);
  TEST_ASSERT_TRUE(arbiter.owns(player));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_claim_releases_nobody);
  RUN_TEST(test_claim_releases_the_previous_owner_once);
  RUN_TEST(test_reclaiming_by_the_owner_changes_nothing);
  RUN_TEST(test_handover_back_releases_the_tones);
  return UNITY_END();
}
