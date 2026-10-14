#include <unity.h>

#include <string>
#include <vector>

#include "LinkTimeline.h"

using drehklang::btlink::LinkTimeline;
using Step = LinkTimeline::Step;

void setUp() {}
void tearDown() {}

namespace {

std::vector<std::string> linesOf(LinkTimeline &timeline, uint32_t nowMs, uint8_t reason) {
  std::vector<std::string> lines;
  timeline.summarize(nowMs, reason, [&](const char *line) { lines.emplace_back(line); });
  return lines;
}

}  // namespace

// The drop ~30 s after the headphones connected (LMP response timeout):
// what had happened by then, and what never did.
void test_a_drop_says_how_long_who_and_why() {
  LinkTimeline timeline;
  timeline.aclUp(10000, /*ours=*/false);
  const auto lines = linesOf(timeline, 40250, 0x22);  // Tenths, cut, not rounded.
  TEST_ASSERT_TRUE(lines.size() >= 1);
  TEST_ASSERT_EQUAL_STRING("link lost after 30.2 s, reason 0x22, they connected",
                           lines[0].c_str());
}

void test_steps_seen_are_listed_with_their_time() {
  LinkTimeline timeline;
  timeline.aclUp(10000, true);
  timeline.done(Step::A2dp, 10500);
  timeline.done(Step::Audio, 10600);
  const auto lines = linesOf(timeline, 40000, 0x22);
  TEST_ASSERT_EQUAL(3, lines.size());
  TEST_ASSERT_EQUAL_STRING("link lost after 30.0 s, reason 0x22, we connected",
                           lines[0].c_str());
  TEST_ASSERT_EQUAL_STRING("seen: a2dp 0.5, audio 0.6", lines[1].c_str());
  TEST_ASSERT_EQUAL_STRING("not seen: auth, encryption, avrcp", lines[2].c_str());
}

void test_a_step_counts_once_at_its_first_time() {
  LinkTimeline timeline;
  timeline.aclUp(0, true);
  timeline.done(Step::Encryption, 300);
  timeline.done(Step::Encryption, 9000);
  const auto lines = linesOf(timeline, 10000, 0x13);
  TEST_ASSERT_EQUAL_STRING("seen: encryption 0.3", lines[1].c_str());
}

void test_a_power_mode_change_is_shown_last() {
  LinkTimeline timeline;
  timeline.aclUp(0, false);
  timeline.done(Step::A2dp, 400);
  timeline.modeChanged(2, 9300);
  const auto lines = linesOf(timeline, 9800, 0x08);
  TEST_ASSERT_EQUAL_STRING("seen: a2dp 0.4, mode 2 at 9.3", lines[1].c_str());
}

void test_everything_seen_leaves_out_the_not_seen_line() {
  LinkTimeline timeline;
  timeline.aclUp(0, true);
  for (Step step : {Step::Auth, Step::Encryption, Step::A2dp, Step::Audio, Step::Avrcp}) {
    timeline.done(step, 100);
  }
  const auto lines = linesOf(timeline, 5000, 0x22);
  TEST_ASSERT_EQUAL(2, lines.size());
}

void test_nothing_is_said_without_a_link() {
  LinkTimeline timeline;
  TEST_ASSERT_EQUAL(0, linesOf(timeline, 1000, 0x22).size());
  timeline.aclUp(0, true);
  linesOf(timeline, 1000, 0x22);
  // A summary closes the link's timeline: a second disconnect says nothing.
  TEST_ASSERT_EQUAL(0, linesOf(timeline, 2000, 0x22).size());
}

void test_steps_before_the_link_are_ignored() {
  LinkTimeline timeline;
  timeline.done(Step::A2dp, 100);
  timeline.aclUp(1000, true);
  const auto lines = linesOf(timeline, 2000, 0x22);
  TEST_ASSERT_EQUAL_STRING("seen: nothing", lines[1].c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_drop_says_how_long_who_and_why);
  RUN_TEST(test_steps_seen_are_listed_with_their_time);
  RUN_TEST(test_a_step_counts_once_at_its_first_time);
  RUN_TEST(test_a_power_mode_change_is_shown_last);
  RUN_TEST(test_everything_seen_leaves_out_the_not_seen_line);
  RUN_TEST(test_nothing_is_said_without_a_link);
  RUN_TEST(test_steps_before_the_link_are_ignored);
  return UNITY_END();
}
