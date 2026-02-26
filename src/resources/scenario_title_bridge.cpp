#include <openglad/gameplay/scenario_title.h>

#include <openglad/resources/level_io.h>

#include <format>
#include <string>

namespace og::gameplay {

std::string scenario_title_for_level(short level)
{
    const std::string scenario_id = std::format("scen{}", level);
    std::string title = og::data::load_scenario_title(scenario_id.c_str());
    if (title == "none")
        title = std::format("Level {}", level);
    return title;
}

} // namespace og::gameplay
