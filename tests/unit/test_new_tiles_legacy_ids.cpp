// Reads the LEGACY duplicate pixdefs copy (include/openglad/legacy/pixdefs.h)
// in an ISOLATED translation unit — the header is a full copy of
// core/pixdefs.h, not a forwarding include, so a lockstep edit miss produces
// silent ID bugs rather than compile errors. test_new_tiles.cpp compares
// these values against the core header's.
#include <openglad/legacy/pixdefs.h>

#include <array>

namespace og_test {

std::array<int, 9> legacy_westlands_pix_ids()
{
    return {PIX_SNOW1, PIX_SNOW2, PIX_LAVA1,  PIX_LAVA2, PIX_MARSH1,
            PIX_MARSH2, PIX_ASH1, PIX_ASH2, PIX_MAX};
}

} // namespace og_test
