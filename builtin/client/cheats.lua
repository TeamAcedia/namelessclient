core.cheats = {
	["Render"] = {
		["CheatHUD"] = "cheat_hud",
		["FullBright"] = "fullbright",
		["BrightNight"] = "no_night",
		["Xray"] = "xray",
		["EntityESP"] = "enable_entity_esp",
		["EntityTracers"] = "enable_entity_tracers",
		["PlayerESP"] = "enable_player_esp",
		["PlayerTracers"] = "enable_player_tracers",
		["NodeESP"] = "enable_node_esp",
		["NodeTracers"] = "enable_node_tracers",
	--	["HUDBypass"] = "hud_flags_bypass", dont wanna work, will fix later
		["NoHurtCam"] = "no_hurt_cam",
		["NoParticles"] = "norender.particles"
    },
	["Player"] = {
		["PrivBypass"] = "priv_bypass",
		["NoFallDamage"] = "prevent_natural_damage",
		["Reach"] = "reach",
		["AutoRespawn"] = "autorespawn",
    },
	["Movement"] = {
		["Freecam"] = "freecam",
		["AutoForward"] = "continuous_forward",
		["PitchMove"] = "pitch_move",
		["AutoJump"] = "autojump",
		["Flight"] = "free_move",
		["Noclip"] = "noclip",
		["FastMove"] = "fast_move",
		["Jesus"] = "jesus",
		["NoSlow"] = "no_slow",
		["JetPack"] = "jetpack",
		["AntiSlip"] = "antislip",
		["AirJump"] = "airjump",
		["Spider"] = "spider",
		["AutoSneak"] = "autosneak",
    },
	["Combat"] = {
		["AntiKnockback"] = "antiknockback",
		["AttachmentFloat"] = "float_above_parent",
    },
	["Interact"] = {
		["FastHit"] = "spamclick",
		["AutoHit"] = "autohit",
		["FastPlace"] = "fastplace",
		["AutoPlace"] = "autoplace",
		["AutoDig"] = "autodig",
		["FastDig"] = "fastdig",
		["InstantBreak"] = "instant_break",
		["AutoTool"] = "autotool",
    }
}

function core.register_cheat(cheatname, category, func)
	core.cheats[category] = core.cheats[category] or {}
	core.cheats[category][cheatname] = func
end