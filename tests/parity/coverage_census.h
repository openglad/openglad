#pragma once

#include "parity_runner.h"
#include "scenario_table.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace og::parity::census {

inline constexpr std::size_t kPartitions = 4;

struct ObservedUnion
{
    CoverageObservation obs;
    std::uint64_t exercises = 0;
    std::vector<std::string> unloaded;
};

inline void require(bool condition, const std::string& reason)
{
    if (!condition) throw std::runtime_error("parity census: " + reason);
}

inline void append(ObservedUnion& result, const ScenarioSpec& spec,
                   const RunOutcome& outcome)
{
    if (!outcome.loaded && !builds_its_own_arena(spec))
        result.unloaded.emplace_back(spec.id);
    const auto& source = outcome.coverage;
    auto& target = result.obs;
    target.walker_families.insert(source.walker_families.begin(), source.walker_families.end());
    target.weapon_families.insert(source.weapon_families.begin(), source.weapon_families.end());
    target.treasure_families.insert(source.treasure_families.begin(), source.treasure_families.end());
    target.generator_families.insert(source.generator_families.begin(), source.generator_families.end());
    target.effect_families.insert(source.effect_families.begin(), source.effect_families.end());
    target.event_kinds.insert(source.event_kinds.begin(), source.event_kinds.end());
    result.exercises |= static_cast<std::uint64_t>(spec.exercises);
}

inline std::string nonce(const std::filesystem::path& directory)
{
    std::ifstream input(directory / "nonce");
    std::string value, extra;
    require(static_cast<bool>(input >> value) && value.size() == 32,
            "missing or invalid fresh fixture nonce");
    require(!(input >> extra), "unexpected nonce data");
    return value;
}

template<class Value>
void write_set(std::ostream& output, const std::set<Value>& values)
{
    output << values.size();
    for (const auto& value : values)
    {
        output << ' ';
        if constexpr (std::is_same_v<Value, std::string>) output << std::quoted(value);
        else output << value;
    }
    output << '\n';
}

template<class Value>
void read_set(std::istream& input, std::set<Value>& values)
{
    std::size_t count = 0;
    require(static_cast<bool>(input >> count) && count <= 10000,
            "invalid observation count");
    for (std::size_t i = 0; i < count; ++i)
    {
        Value value{};
        if constexpr (std::is_same_v<Value, std::string>) input >> std::quoted(value);
        else input >> value;
        require(static_cast<bool>(input) && values.insert(value).second,
                "missing or duplicate observation");
    }
}

inline void write_observation(std::ostream& output, const CoverageObservation& observation)
{
    write_set(output, observation.walker_families);
    write_set(output, observation.weapon_families);
    write_set(output, observation.treasure_families);
    write_set(output, observation.generator_families);
    write_set(output, observation.effect_families);
    write_set(output, observation.event_kinds);
}

inline void read_observation(std::istream& input, CoverageObservation& observation)
{
    read_set(input, observation.walker_families);
    read_set(input, observation.weapon_families);
    read_set(input, observation.treasure_families);
    read_set(input, observation.generator_families);
    read_set(input, observation.effect_families);
    read_set(input, observation.event_kinds);
}

inline std::filesystem::path partition_path(const std::filesystem::path& directory,
                                           std::size_t partition)
{
    return directory / ("partition-" + std::to_string(partition) + ".txt");
}

inline ObservedUnion collect_partition(const std::filesystem::path& directory,
                                      std::size_t partition)
{
    require(partition < kPartitions, "partition index out of range");
    const auto path = partition_path(directory, partition);
    require(!std::filesystem::exists(path), "partition already collected in this fixture");
    const auto temporary = path.string() + ".partial";
    std::ofstream output(temporary);
    require(static_cast<bool>(output), "cannot create observation file");
    output << "OG_CENSUS_1 " << nonce(directory) << ' ' << partition << ' '
           << kPartitions << '\n';
    ObservedUnion result;
    std::size_t rows = 0;
    for (std::size_t i = partition; i < std::size(kScenarios); i += kPartitions)
    {
        const auto& spec = kScenarios[i];
        const auto outcome = run_scenario(spec);
        output << std::quoted(std::string(spec.id)) << ' ' << outcome.loaded << ' '
               << static_cast<std::uint64_t>(spec.exercises) << '\n';
        write_observation(output, outcome.coverage);
        append(result, spec, outcome);
        ++rows;
    }
    output << "END " << rows << '\n';
    output.close();
    require(static_cast<bool>(output), "incomplete observation write");
    std::filesystem::rename(temporary, path);
    return result;
}

inline ObservedUnion read_partitions(const std::filesystem::path& directory)
{
    const auto expected_nonce = nonce(directory);
    ObservedUnion result;
    for (std::size_t partition = 0; partition < kPartitions; ++partition)
    {
        std::ifstream input(partition_path(directory, partition));
        std::string magic, recorded_nonce;
        std::size_t recorded_partition = 0, recorded_count = 0;
        require(static_cast<bool>(input >> magic >> recorded_nonce >> recorded_partition >> recorded_count)
                && magic == "OG_CENSUS_1" && recorded_nonce == expected_nonce
                && recorded_partition == partition && recorded_count == kPartitions,
                "missing, stale, or mismatched partition " + std::to_string(partition));
        std::size_t rows = 0;
        for (std::size_t i = partition; i < std::size(kScenarios); i += kPartitions)
        {
            const auto& spec = kScenarios[i];
            std::string id;
            int loaded = -1;
            std::uint64_t exercises = 0;
            require(static_cast<bool>(input >> std::quoted(id) >> loaded >> exercises)
                    && id == spec.id && (loaded == 0 || loaded == 1)
                    && exercises == static_cast<std::uint64_t>(spec.exercises),
                    "missing, duplicate, or unexpected scenario at index " + std::to_string(i));
            RunOutcome outcome;
            outcome.loaded = loaded == 1;
            read_observation(input, outcome.coverage);
            append(result, spec, outcome);
            ++rows;
        }
        std::string end, extra;
        std::size_t recorded_rows = 0;
        require(static_cast<bool>(input >> end >> recorded_rows) && end == "END"
                && recorded_rows == rows && !(input >> extra),
                "partition row census mismatch or trailing data");
    }
    return result;
}

inline ObservedUnion collect()
{
    if (const char* directory = std::getenv("OPENGLAD_PARITY_CENSUS_DIR"))
    {
        if (const char* partition = std::getenv("OPENGLAD_PARITY_CENSUS_PART");
            partition != nullptr && *partition != '\0')
        {
            const std::string value(partition);
            require(value.size() == 1 && value[0] >= '0' && value[0] <= '9',
                    "invalid requested partition");
            return collect_partition(directory, static_cast<std::size_t>(value[0] - '0'));
        }
        return read_partitions(directory);
    }
    ObservedUnion result;
    for (const auto& spec : kScenarios) append(result, spec, run_scenario(spec));
    return result;
}

} // namespace og::parity::census
