solution "Openglad"
   configurations { "Debug", "Release" }

   newoption {
	   trigger     = "cflags",
	   value       = "FLAGS",
	   description = "Flags passed directly to the compiler."
	}
   newoption {
	   trigger     = "ldflags",
	   value       = "FLAGS",
	   description = "Flags passed directly to the linker."
	}
   newoption {
	   trigger     = "includedirs",
	   value       = "DIR",
	   description = "Directories to add to the include path."
	}
   newoption {
	   trigger     = "libdirs",
	   value       = "DIR",
	   description = "Directories to add to the linker lib path."
	}
	
	if _OPTIONS["cflags"] then
		buildoptions {string.explode(_OPTIONS["cflags"], " ")}
	end
	if _OPTIONS["ldflags"] then
		linkoptions {string.explode(_OPTIONS["ldflags"], " ")}
	end
	if _OPTIONS["includedirs"] then
		includedirs {string.explode(_OPTIONS["includedirs"], " ")}
	end
	if _OPTIONS["libdirs"] then
		libdirs {string.explode(_OPTIONS["libdirs"], " ")}
	end
   
   project "openglad"
	  kind "ConsoleApp"
      language "C++"
      files { "src/**.h", "src/**.cpp", "src/**.c", "util/savepng.*" }
	  excludes { "src/purchasing.*", "src/OuyaController.*" }
	  
	  -- Messy, but premake doesn't let you re-add excluded files.  TODO: Manage a lua list instead.
	  excludes { "src/external/physfs/archivers/grp.c", "src/external/physfs/archivers/hog.c", "src/external/physfs/archivers/lzma.c", "src/external/physfs/archivers/mvl.c", "src/external/physfs/archivers/qpak.c", "src/external/physfs/archivers/wad.c", "src/external/physfs/extras/PhysDS.NET/**", "src/external/physfs/extras/physfs_rb/**", "src/external/physfs/extras/abs-file.h", "src/external/physfs/extras/globbing.c", "src/external/physfs/extras/globbing.h", "src/external/physfs/extras/ignorecase.c", "src/external/physfs/extras/ignorecase.h", "src/external/physfs/extras/physfshttpd.c", "src/external/physfs/extras/physfsunpack.c", "src/external/physfs/extras/selfextract.c" }
      --files { "src/external/physfs/archivers/dir.c", "src/external/physfs/archivers/zip.c", "src/external/physfs/extras/physfsrwops.*" }
	  defines { "PHYSFS_SUPPORTS_ZIP" }
	  buildoptions { "-std=gnu++0x" }
	
	  links { "SDL2main", "SDL2", "SDL2_mixer", "png" }
	  includedirs { "src/external/**" }
 
      configuration "Debug"
         defines { "DEBUG" }
         flags { "Symbols" }
 
      configuration "Release"
		 kind "WindowedApp"
         defines { "NDEBUG" }
         flags { "Optimize" }