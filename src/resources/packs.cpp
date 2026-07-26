/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/resources/packs.h>

#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/physfs_api.h>
#include <openglad/core/util.h>

#include <string>

namespace og::resources {

int register_mounted_pack_scripts()
{
    int registered = 0;
    // Sorted enumeration keeps the replay order identical on every peer.
    for (const std::string& pack_id :
         og::io::physfs_enumerate_files_sorted("packs")) {
        const std::string scripts_dir = "packs/" + pack_id + "/scripts";
        for (const std::string& file :
             og::io::physfs_enumerate_files_sorted(scripts_dir)) {
            if (file.size() < 4 ||
                file.compare(file.size() - 4, 4, ".lua") != 0)
                continue;
            const std::string vpath = scripts_dir + "/" + file;
            std::vector<std::uint8_t> bytes = read_file(vpath.c_str());
            if (bytes.empty()) {
                LogWarn("class pack script unreadable: {}\n", vpath);
                continue;
            }
            og::script::register_pack_script(
                {pack_id, vpath,
                 std::string(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size())});
            registered++;
        }
    }
    if (registered > 0)
        Log("Registered {} class pack script(s)\n", registered);
    return registered;
}

}  // namespace og::resources
