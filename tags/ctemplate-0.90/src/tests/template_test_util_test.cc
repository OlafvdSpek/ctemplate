// Copyright 2008 Google Inc. All Rights Reserved.

#include "config_for_unittests.h"
#include <stdio.h>
#include <assert.h>
#include <vector>
#include "tests/template_test_util.h"
#include <google/template_dictionary.h>

using std::vector;
using GOOGLE_NAMESPACE::TemplateDictionary;
using GOOGLE_NAMESPACE::TemplateDictionaryPeer;

namespace {

// This works in both debug mode and NDEBUG mode.
#define EXPECT_FALSE(cond)  do {                                \
  if (cond) {                                                   \
    printf("%s: %d: ASSERT FAILED: %s\n", __FILE__, __LINE__,   \
           #cond);                                              \
    assert(!(cond));                                            \
    exit(1);                                                    \
  }                                                             \
} while (0)

#define EXPECT_TRUE(cond)  EXPECT_FALSE(!(cond))

#define EXPECT_EQ(a, b)  EXPECT_TRUE(a == b)

#define EXPECT_STREQ(a, b)  do {                                          \
  if (strcmp((a), (b))) {                                                 \
    printf("%s: %d: ASSERT FAILED: '%s' != '%s'\n", __FILE__, __LINE__,   \
           (a), (b));                                                     \
    assert(!strcmp((a), (b)));                                            \
    exit(1);                                                              \
  }                                                                       \
} while (0)


void Test_GetSectionValue() {
  TemplateDictionary dict("test_GetSectionValue");
  dict.SetValue("VALUE", "value");

  TemplateDictionaryPeer peer(&dict);
  EXPECT_STREQ("value", peer.GetSectionValue("VALUE"));
}

void Test_IsHiddenSection() {
  TemplateDictionary dict("test_IsHiddenSection");

  {
    TemplateDictionaryPeer peer(&dict);
    EXPECT_TRUE(peer.IsHiddenSection("SECTION"));
  }

  dict.AddSectionDictionary("SECTION");

  {
    TemplateDictionaryPeer peer(&dict);
    EXPECT_FALSE(peer.IsHiddenSection("SECTION"));
  }
}

void Test_GetSectionDictionaries() {
  TemplateDictionary dict("test_GetSectionDictionaries");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    // Add some dummy value into the vector to confirm that the call to
    // GetSectionDictionaries will correctly clear the vector.
    dicts.push_back(NULL);
    EXPECT_EQ(0, peer.GetSectionDictionaries("SECTION", &dicts));
    EXPECT_TRUE(dicts.empty());
  }

  dict.AddSectionDictionary("SECTION")->SetValue("SECTION_VALUE", "0");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(1, peer.GetSectionDictionaries("SECTION", &dicts));

    TemplateDictionaryPeer peer_section(dicts[0]);
    EXPECT_STREQ("0", peer_section.GetSectionValue("SECTION_VALUE"));
  }

  dict.AddSectionDictionary("SECTION")->SetValue("SECTION_VALUE", "1");
  dict.AddSectionDictionary("ANOTHER_SECTION")->SetValue("ANOTHER_VALUE", "2");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(2, peer.GetSectionDictionaries("SECTION", &dicts));

    TemplateDictionaryPeer peer_section0(dicts[0]);
    EXPECT_STREQ("0", peer_section0.GetSectionValue("SECTION_VALUE"));

    TemplateDictionaryPeer peer_section1(dicts[1]);
    EXPECT_STREQ("1", peer_section1.GetSectionValue("SECTION_VALUE"));
  }
}

void Test_GetIncludeDictionaries() {
  TemplateDictionary dict("test_GetIncludeDictionaries");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    // Add some dummy value into the vector to confirm that the call to
    // GetSectionDictionaries will correctly clear the vector.
    dicts.push_back(NULL);
    EXPECT_EQ(0, peer.GetIncludeDictionaries("SECTION", &dicts));
    EXPECT_TRUE(dicts.empty());
  }

  dict.AddIncludeDictionary("SECTION")->SetValue("SECTION_VALUE", "0");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(1, peer.GetIncludeDictionaries("SECTION", &dicts));

    TemplateDictionaryPeer peer_section(dicts[0]);
    EXPECT_STREQ("0", peer_section.GetSectionValue("SECTION_VALUE"));
  }

  dict.AddIncludeDictionary("SECTION")->SetValue("SECTION_VALUE", "1");
  dict.AddIncludeDictionary("ANOTHER_SECTION")->SetValue("ANOTHER_VALUE", "2");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(2, peer.GetIncludeDictionaries("SECTION", &dicts));

    TemplateDictionaryPeer peer_section0(dicts[0]);
    EXPECT_STREQ("0", peer_section0.GetSectionValue("SECTION_VALUE"));

    TemplateDictionaryPeer peer_section1(dicts[1]);
    EXPECT_STREQ("1", peer_section1.GetSectionValue("SECTION_VALUE"));
  }
}

void Test_GetIncludeAndSectionDictionaries() {
  TemplateDictionary dict("test_GetIncludeAndSectionDictionaries");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(0, peer.GetIncludeDictionaries("SECTION", &dicts));
    EXPECT_EQ(0, peer.GetSectionDictionaries("SECTION", &dicts));
  }

  dict.AddIncludeDictionary("SECTION")->SetValue("SECTION_VALUE", "0");
  dict.AddSectionDictionary("SECTION")->SetValue("SECTION_VALUE", "1");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> include_dicts;
    EXPECT_EQ(1, peer.GetIncludeDictionaries("SECTION", &include_dicts));

    TemplateDictionaryPeer include_peer(include_dicts[0]);
    EXPECT_STREQ("0", include_peer.GetSectionValue("SECTION_VALUE"));

    vector<const TemplateDictionary*> section_dicts;
    EXPECT_EQ(1, peer.GetSectionDictionaries("SECTION", &section_dicts));

    TemplateDictionaryPeer section_peer(section_dicts[0]);
    EXPECT_STREQ("1", section_peer.GetSectionValue("SECTION_VALUE"));
  }

  dict.AddIncludeDictionary("SECTION")->SetValue("SECTION_VALUE", "2");
  dict.AddIncludeDictionary("ANOTHER_SECTION")->SetValue("ANOTHER_VALUE", "3");

  dict.AddSectionDictionary("SECTION")->SetValue("SECTION_VALUE", "4");
  dict.AddSectionDictionary("ONE_MORE_SECTION")->SetValue("ANOTHER_VALUE", "5");

  {
    TemplateDictionaryPeer peer(&dict);
    vector<const TemplateDictionary*> dicts;
    EXPECT_EQ(2, peer.GetIncludeDictionaries("SECTION", &dicts));

    TemplateDictionaryPeer include_peer0(dicts[0]);
    EXPECT_STREQ("0", include_peer0.GetSectionValue("SECTION_VALUE"));

    TemplateDictionaryPeer include_peer1(dicts[1]);
    EXPECT_STREQ("2", include_peer1.GetSectionValue("SECTION_VALUE"));

    EXPECT_EQ(1, peer.GetIncludeDictionaries("ANOTHER_SECTION", &dicts));
    EXPECT_EQ(0, peer.GetIncludeDictionaries("ONE_MORE_SECTION", &dicts));

    vector<const TemplateDictionary*> section_dicts;
    EXPECT_EQ(2, peer.GetSectionDictionaries("SECTION", &section_dicts));

    TemplateDictionaryPeer section_peer0(section_dicts[0]);
    EXPECT_STREQ("1", section_peer0.GetSectionValue("SECTION_VALUE"));

    TemplateDictionaryPeer section_peer1(section_dicts[1]);
    EXPECT_STREQ("4", section_peer1.GetSectionValue("SECTION_VALUE"));

    EXPECT_EQ(0, peer.GetSectionDictionaries("ANOTHER_SECTION", &dicts));
    EXPECT_EQ(1, peer.GetSectionDictionaries("ONE_MORE_SECTION", &dicts));
  }
}

}  // namespace anonymous

int main(int argc, char **argv) {
  Test_GetSectionValue();
  Test_IsHiddenSection();
  Test_GetSectionDictionaries();
  Test_GetIncludeDictionaries();
  Test_GetIncludeAndSectionDictionaries();

  printf("PASS\n");
  return 0;
}
