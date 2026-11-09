#include <openglad/resources/company.h>
#include "../../src/resources/company_clock_state.h"

namespace og::data {

void set_company_clock_for_tests(std::optional<std::int64_t> fixed_now_s)
{
    detail::company_clock_override() = fixed_now_s;
}

} // namespace og::data
