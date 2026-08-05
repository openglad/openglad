# --------------------------------------------------------------------------
# Combined C++ + Lua coverage gate, and the modding API stub drift check.
#
# Included by the top-level CMakeLists.txt; runs in that scope.
# --------------------------------------------------------------------------

# --------------------------------------------------------------------------
# Combined C++ + Lua coverage gate
# --------------------------------------------------------------------------
# Family behaviour is Lua (packs/**/scripts/*.lua), so a gate that
# measures src/ alone measures the shrinking half of the codebase and would
# report green while thousands of lines of game logic went untested. These
# targets produce ONE report across both languages:
#
#   cmake --build <build> --target coverage_run     # collect (re-runs ctest)
#   cmake --build <build> --target coverage_report  # merge + gate
#
# coverage_run arms the in-process pack-Lua recorder through
# OPENGLAD_LUA_COVERAGE and re-runs the suite. coverage_report takes the
# DENOMINATOR from scripts/lua_inventory.py — a static enumeration of the
# repository's Lua, so it does not depend on which tests ran and a source no
# test loads counts as 0% rather than vanishing — measures it with
# og_lua_lines, attributes the dumps' hits back to it by content hash, and in
# an ENABLE_COVERAGE build folds in gcovr's src/ tracefile. It writes lcov
# tracefiles (lua.info / cpp.info / combined.info) and fails below threshold.
if(BUILD_TESTING)
    # Executable-line oracle: asks Lua which lines of a source can hold a
    # breakpoint. Deliberately linked against Lua ALONE rather than
    # og_gameplay — linking the instrumented game would make this tool's own
    # execution write .gcda files and inflate the very C++ numbers it is
    # being used to report.
    add_executable(og_lua_lines
        ${CMAKE_SOURCE_DIR}/scripts/coverage/lua_lines_main.cpp
        ${SRC_DIR}/gameplay/script/script_coverage.cpp
    )
    target_include_directories(og_lua_lines PRIVATE
        $<BUILD_INTERFACE:${OG_PUBLIC_INCLUDE_DIR}>
    )
    target_compile_features(og_lua_lines PRIVATE cxx_std_20)
    target_link_libraries(og_lua_lines PRIVATE og_lua)

    set(OG_COVERAGE_DIR ${CMAKE_BINARY_DIR}/coverage)
    set(OG_LUA_RAW_DIR ${OG_COVERAGE_DIR}/lua-raw)

    add_custom_target(coverage_run
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${OG_LUA_RAW_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OG_LUA_RAW_DIR}
        COMMAND ${CMAKE_COMMAND} -E env OPENGLAD_LUA_COVERAGE=${OG_LUA_RAW_DIR}
                ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
                --output-on-failure --no-tests=error
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running the suite with the pack-Lua coverage recorder armed"
        USES_TERMINAL
        VERBATIM
    )

    set(OG_COVERAGE_CPP_ARGS "")
    if(ENABLE_COVERAGE)
        set(OG_COVERAGE_CPP_ARGS --cpp-build-dir ${CMAKE_BINARY_DIR})
    endif()

    # The coverage gate HARD-requires the interpreter: unlike the per-build
    # lint dependency (skippable so a pythonless machine still builds the
    # game), a coverage_report invocation with no Python 3 must fail loudly
    # rather than let "did not run" read as "passed".
    if(Python3_Interpreter_FOUND)
        add_custom_target(coverage_report
            COMMAND ${OG_PY_GIT_ENV} ${Python3_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/scripts/coverage/coverage_report.py
                    --repo-root ${CMAKE_SOURCE_DIR}
                    --lua-raw-dir ${OG_LUA_RAW_DIR}
                    --lines-tool $<TARGET_FILE:og_lua_lines>
                    --output-dir ${OG_COVERAGE_DIR}
                    ${OG_COVERAGE_CPP_ARGS}
            DEPENDS og_lua_lines
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Merging C++ and pack-Lua coverage and enforcing the gate"
            USES_TERMINAL
            VERBATIM
        )
    else()
        add_custom_target(coverage_report
            COMMAND ${CMAKE_COMMAND} -E echo
                    "coverage_report: the coverage gate needs a Python 3 interpreter and none was found at configure time. Install Python 3 and reconfigure."
            COMMAND ${CMAKE_COMMAND} -E false
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Merging C++ and pack-Lua coverage and enforcing the gate"
            USES_TERMINAL
            VERBATIM
        )
    endif()
    # The gate path lints the FULL inventory — untracked files, .glad archive
    # members, embedded literals — which the per-build lint deliberately does
    # not (see the check_lua_statement_lines comment near the target). A junk
    # or undeclared shipped-Lua candidate must fail THIS target loudly,
    # naming the repository-side path, instead of breaking every ninja build.
    add_dependencies(coverage_report check_lua_statement_lines_full)
    # lua-language-server --check over the workspace gates the same target
    # (the check_luals comment near its
    # definition explains why it is NOT an og_gameplay build dep — per-build
    # cost stays flat). api_stub_check joins below, after its definition.
    add_dependencies(coverage_report check_luals)
endif()

# --------------------------------------------------------------------------
# Modding API stubs drift check (enforced at the coverage gate)
# --------------------------------------------------------------------------
# docs/modding/og-api.d.lua is GENERATED from the binding registration
# tables by scripts/modding/gen_api_stubs.py; this target regenerates and
# byte-compares, and its red output names the regeneration command.
# This is a coverage_report dependency (below, beside check_luals and the
# full statement lint), but deliberately NOT a build dependency of
# og_gameplay, so per-build cost stays flat. Stale stubs would also mislead
# check_luals and rule 8 of the statement lint, which both read the committed
# file. Run alone:
#
#   cmake --build <build> --target api_stub_check
#
# Like coverage_report, an explicit invocation without Python 3 must fail
# loudly rather than let "did not run" read as "passed".
if(Python3_Interpreter_FOUND)
    add_custom_target(api_stub_check
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/scripts/modding/check_api_stubs.py
                --repo-root ${CMAKE_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking docs/modding/og-api.d.lua against the binding registration tables"
        USES_TERMINAL
        VERBATIM
    )
else()
    add_custom_target(api_stub_check
        COMMAND ${CMAKE_COMMAND} -E echo
                "api_stub_check: needs a Python 3 interpreter and none was found at configure time. Install Python 3 and reconfigure."
        COMMAND ${CMAKE_COMMAND} -E false
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking docs/modding/og-api.d.lua against the binding registration tables"
        USES_TERMINAL
        VERBATIM
    )
endif()

# Stub drift gates coverage_report (the target exists only under
# BUILD_TESTING; check_luals is wired beside the full statement lint at the
# coverage_report definition above). It stays out of og_gameplay's
# dependencies so per-build cost stays flat.
if(BUILD_TESTING)
    add_dependencies(coverage_report api_stub_check)
endif()
