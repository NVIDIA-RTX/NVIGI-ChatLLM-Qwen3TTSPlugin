-- SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
-- SPDX-License-Identifier: MIT
--

ROOT = path.getabsolute("./").."/"
include("premake/premake.utils.lua")
workspace "qwen3tts"
include("premake/premake.shared_config.lua")
include("premake/premake.shared_plugin.lua")
include("premake/premake.cuda_utils.lua")
project "gitVersion"
	kind "Utility"
	targetdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")
	objdir (ROOT .. "_artifacts/%{prj.name}/%{cfg.platform}/%{cfg.buildcfg}")
	if os.ishost("windows") then
		prebuildcommands { 'pushd '..path.translate(ROOT .. "_artifacts"), path.translate("../tools/").."gitVersion.bat", 'popd' }
	else
		prebuildcommands { '(cd '..path.translate(ROOT .. "_artifacts &&")..path.translate("../tools/").."gitVersion.sh)"}
	end
include("source/plugins/nvigi.tts/qwen3tts/premake.lua")
