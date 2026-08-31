-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: MIT

group "plugins/tts"

project "nvigi.plugin.tts.qwen3tts"
	kind "SharedLib"	
	targetdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")
	objdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")		
	pluginBasicSetup("tts/qwen3tts")

	files { 
		"./**.h",
		"./**.cpp",
	}

	vpaths { ["impl"] = {"./**.h", "./**.cpp" }}

	includedirs {coresdkdir .. "source/utils/nvigi.ai"}
	includedirs {ROOT .. "source/plugins/nvigi.tts/qwen3tts"}

	filter {"system:windows"}
		vpaths { ["impl"] = {"./**.h","./**.cpp", }}
		vpaths { ["utils"] = {ROOT .. "source/utils/**.h",ROOT .. "source/utils/**.cpp", }}
	filter {}

	-- ChatLLM.cpp (handles CUDA internally)
	includedirs { externaldir .."chatllmcpp" }
	libdirs { externaldir .."chatllmcpp" }
	links { "libchatllm.lib" }

group ""