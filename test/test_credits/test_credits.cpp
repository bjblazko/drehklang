#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

#include "Credits.h"

using drehklang::about::Credit;
using drehklang::about::kCredits;

namespace {

// The repository root, from this file's own path: the test binary's
// working directory is not guaranteed.
std::string repoPath(const std::string &relative) {
  std::string here = __FILE__;
  const std::string marker = "test/test_credits/";
  return here.substr(0, here.rfind(marker)) + relative;
}

std::string readFile(const std::string &relative) {
  std::ifstream in(repoPath(relative));
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// lib_deps entries of the firmware environment, one per line.
std::string libDeps() {
  const std::string ini = readFile("platformio.ini");
  const auto start = ini.find("lib_deps =");
  if (start == std::string::npos) return {};
  std::string block;
  std::istringstream lines(ini.substr(start));
  std::string line;
  std::getline(lines, line);  // "lib_deps =" itself.
  while (std::getline(lines, line)) {
    if (line.empty() || (line[0] != ' ' && line[0] != '\t')) break;
    block += line + "\n";
  }
  return block;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_every_credit_is_complete() {
  for (const Credit &credit : kCredits) {
    TEST_ASSERT_NOT_NULL(credit.name);
    TEST_ASSERT_TRUE_MESSAGE(credit.use[0] != '\0', credit.name);
    TEST_ASSERT_TRUE_MESSAGE(credit.licence[0] != '\0', credit.name);
    TEST_ASSERT_TRUE_MESSAGE(credit.home[0] != '\0', credit.name);
  }
}

void test_every_licence_text_is_in_the_repository() {
  for (const Credit &credit : kCredits) {
    const std::string text = readFile(std::string("licenses/") + credit.licence + ".txt");
    TEST_ASSERT_TRUE_MESSAGE(text.size() > 200, credit.licence);
  }
  // Drehklang's own GPL-3.0-or-later: the GPL-3.0 text, which says "or later" itself.
  TEST_ASSERT_TRUE(readFile("licenses/GPL-3.0.txt").size() > 200);
}

void test_every_lib_dep_has_a_credit_at_its_pinned_version() {
  std::istringstream lines(libDeps());
  std::string line;
  int deps = 0;
  while (std::getline(lines, line)) {
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    ++deps;
    const Credit *match = nullptr;
    for (const Credit &credit : kCredits) {
      if (credit.dependency && line.find(credit.dependency) != std::string::npos) {
        match = &credit;
      }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(match, line.c_str());
    TEST_ASSERT_TRUE_MESSAGE(line.find(match->version) != std::string::npos, line.c_str());
  }
  TEST_ASSERT_TRUE(deps >= 4);
}

void test_every_credit_is_in_third_party_md() {
  const std::string doc = readFile("THIRD-PARTY.md");
  TEST_ASSERT_FALSE(doc.empty());
  for (const Credit &credit : kCredits) {
    TEST_ASSERT_TRUE_MESSAGE(doc.find(credit.name) != std::string::npos, credit.name);
    if (credit.notice) {
      TEST_ASSERT_TRUE_MESSAGE(doc.find(credit.notice) != std::string::npos, credit.name);
    }
  }
}

void test_the_faad2_line_is_verbatim() {
  // FAAD2's licence prescribes this exact sentence (neaacdec.h).
  for (const Credit &credit : kCredits) {
    if (std::string(credit.name) == "FAAD2") {
      TEST_ASSERT_EQUAL_STRING("Code from FAAD2 is copyright (c) Nero AG, www.nero.com",
                               credit.notice);
      return;
    }
  }
  TEST_FAIL_MESSAGE("FAAD2 missing");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_credit_is_complete);
  RUN_TEST(test_every_licence_text_is_in_the_repository);
  RUN_TEST(test_every_lib_dep_has_a_credit_at_its_pinned_version);
  RUN_TEST(test_every_credit_is_in_third_party_md);
  RUN_TEST(test_the_faad2_line_is_verbatim);
  return UNITY_END();
}
