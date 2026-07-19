/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/resources/company.h>

#include <chrono>

namespace og::data {

namespace {

std::optional<std::int64_t>& company_clock_override()
{
    static std::optional<std::int64_t> fixed_now_s;
    return fixed_now_s;
}

} // namespace

std::int64_t company_clock_now_s()
{
    const auto& fixed = company_clock_override();
    if (fixed.has_value())
        return *fixed;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void set_company_clock_for_tests(std::optional<std::int64_t> fixed_now_s)
{
    company_clock_override() = fixed_now_s;
}

} // namespace og::data
