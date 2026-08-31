-- SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: MIT

if os.istarget("windows") then

group "samples"

project "nvigi.basic.tts.cxx"
	if premakeX64Targets() then
		filter { "platforms:x64" }
			kind "ConsoleApp"
		filter {}
	end
-- NONPUBLICBEGIN
	if _OPTIONS["arm64"] ~= nil then
		filter { "platforms:ARM64" }
			kind "ConsoleApp"
		filter {}
	end
-- NONPUBLICEND

    targetdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")
    objdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")
    
    dependson {"gitVersion"}

    files {
        "./**.h",
        "./**.hpp",
        "./**.cpp",
        "./**.rc",
        coresdkdir .. "source/utils/**.h",
        -- Shared wrapper headers
        ROOT .. "source/samples/shared/cxx_wrappers/**.hpp",
    }

    includedirs {
        coresdkdir .. "source/core/nvigi.api", 
        coresdkdir .. "source/utils/nvigi.ai",
        coresdkdir .. "source/utils",
        ROOT .. "source/plugins/nvigi.tts",
        ROOT .. "external/vulkanSDK/include",
        -- Shared wrappers directory
        ROOT .. "source/samples/shared"
    }
        

    filter { "platforms:x64" }
		libdirs {
			externaldir .. "vulkanSDK/Lib"
		}
	filter{}

-- NONPUBLICBEGIN
	filter { "platforms:ARM64" }
		libdirs {
			externaldir .. "vulkanSDK/Lib-ARM64"
		}
	filter{}
-- NONPUBLICEND

    filter {"system:windows"}
        vpaths { ["impl"] = {"./**.h","./**.hpp", "./**.cpp", }}
        vpaths { ["shared"] = {ROOT .. "source/samples/shared/cxx_wrappers/**.hpp"}}
        vpaths { ["utils"] = {ROOT .. "source/utils/**.h",ROOT .. "source/utils/**.cpp", }}
        links { "d3d12.lib", "dxgi.lib", "vulkan-1.lib", "dsound.lib" }
    filter {}    

group ""

end

