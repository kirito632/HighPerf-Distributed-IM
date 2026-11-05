#include <gtest/gtest.h>

#include "ConfigMgr.h"

TEST(SectionInfoTest, MissingKeyReturnsEmptyString) {
    SectionInfo info;
    EXPECT_EQ(info["Host"], "");
    EXPECT_EQ(info["Port"], "");
}

TEST(SectionInfoTest, ReadsConfiguredValues) {
    SectionInfo info;
    info._section_datas["Host"] = "127.0.0.1";
    info._section_datas["Port"] = "8090";

    EXPECT_EQ(info["Host"], "127.0.0.1");
    EXPECT_EQ(info["Port"], "8090");
    EXPECT_EQ(info["Name"], "");
}

TEST(SectionInfoTest, CopyKeepsValuesIndependent) {
    SectionInfo source;
    source._section_datas["Name"] = "chatserver1";

    SectionInfo copy(source);
    EXPECT_EQ(copy["Name"], "chatserver1");

    copy._section_datas["Name"] = "chatserver2";
    EXPECT_EQ(source["Name"], "chatserver1");
    EXPECT_EQ(copy["Name"], "chatserver2");
}
