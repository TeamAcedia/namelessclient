// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#pragma once

#include "irr_v2d.h"
#include <SColor.h>
#include "pipeline.h"
#include "script/lua_api/l_cheats.h"

namespace irr
{
	class IrrlichtDevice;
}

class ShadowRenderer;
class Camera;
class Client;
class Hud;
class Minimap;
class RenderPipeline;
class RenderTarget;

class RenderingCore
{
protected:
	IrrlichtDevice *device;
	Client *client;
	Hud *hud;
	ShadowRenderer *shadow_renderer;

	RenderPipeline *pipeline;

	v2f virtual_size_scale;
	v2u32 virtual_size { 0, 0 };
public:
	RenderingCore(IrrlichtDevice *device, Client *client, Hud *hud,
			ShadowRenderer *shadow_renderer, RenderPipeline *pipeline,
			v2f virtual_size_scale);
	RenderingCore(const RenderingCore &) = delete;
	RenderingCore(RenderingCore &&) = delete;
	virtual ~RenderingCore();

	RenderingCore &operator=(const RenderingCore &) = delete;
	RenderingCore &operator=(RenderingCore &&) = delete;

	void draw(video::SColor _skycolor, bool _show_hud,
			bool _draw_wield_tool, bool _draw_crosshair, float dtime);
	void drawCombatTargetHUD();

	ShadowRenderer *get_shadow_renderer() { return shadow_renderer; };

	v2u32 getVirtualSize() const;

	static u16 combat_target;
	static video::SColor target_esp_color;

	static std::map<std::string, std::vector<int>> ESPplayersNames;
};
