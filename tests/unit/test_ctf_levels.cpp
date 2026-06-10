#include <gtest/gtest.h>

#include <openglad/core/ctf_constants.h>

// Shipped CTF level validation lands with the content phase. This anchors
// the binary and pins the authored-entity family ids the maps will use.
TEST(CtfLevels, family_ids)
{
    ASSERT_EQ(13, og::FAMILY_FLAG);
    ASSERT_EQ(14, og::FAMILY_CTF_POINT);
    ASSERT_EQ(8, og::SCEN_TYPE_CTF);
}
