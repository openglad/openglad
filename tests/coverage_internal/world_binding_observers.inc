std::vector<std::string> og_function_names()
{
    WorldScripts& ws = active_world_scripts();
    ScriptHost::Impl& impl = ws.host().impl();
    lua_State* L = impl.L;
    impl.push_sandbox_root();
    lua_getfield(L, -1, "og");
    std::vector<std::string> names;
    lua_pushnil(L);
    // plumbing: og_function_names: lua_next over the sandbox og table collecting function-valued
    //   string keys, sorted; used by the fence-walk test (test_campaign_hooks.cpp
    //   fence_walk_everything_outside_the_allowlist_errors) to enumerate bindings.
    while (lua_next(L, -2) != 0) {
        // plumbing: og_function_names: lua_next over the sandbox og table collecting
        //   function-valued string keys, sorted; used by the fence-walk test
        //   (test_campaign_hooks.cpp fence_walk_everything_outside_the_allowlist_errors) to
        //   enumerate bindings.
        if (lua_isfunction(L, -1) && lua_type(L, -2) == LUA_TSTRING)
            names.emplace_back(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    std::sort(names.begin(), names.end());
    return names;
}
