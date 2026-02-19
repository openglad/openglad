#include "unit.h"

#include <openglad/io/physfs_api.h>

OG_UNIT_TEST(test_physfs_wrapper_init_deinit_roundtrip_restore_state)
{
    const bool init_ok = og::io::physfs_init("og_unit_tests");
    (void)init_ok; // false is acceptable when already initialized

    OG_ASSERT(og::io::physfs_deinit());
    OG_ASSERT(og::io::physfs_init("og_unit_tests"));
}
