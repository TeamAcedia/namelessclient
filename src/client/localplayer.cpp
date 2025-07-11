// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include <iostream>

#include "localplayer.h"
#include <cmath>
#include "mtevent.h"
#include "collision.h"
#include "nodedef.h"
#include "settings.h"
#include "environment.h"
#include "map.h"
#include "client.h"
#include "content_cao.h"
#include "client/game.h"
#include "util/pointedthing.h"

/*
	PlayerSettings
*/

const static std::string PlayerSettings_names[] = {
	"freecam", "free_move", "pitch_move", "fast_move", "continuous_forward", "always_fly_fast",
	"aux1_descends", "noclip", "autojump"
};

void PlayerSettings::readGlobalSettings()
{
	freecam = g_settings->getBool("freecam");
	free_move = g_settings->getBool("free_move");
	pitch_move = g_settings->getBool("pitch_move");
	fast_move = g_settings->getBool("fast_move");
	continuous_forward = g_settings->getBool("continuous_forward");
	always_fly_fast = g_settings->getBool("always_fly_fast");
	aux1_descends = g_settings->getBool("aux1_descends");
	noclip = g_settings->getBool("noclip");
	autojump = g_settings->getBool("autojump");
}


void PlayerSettings::registerSettingsCallback()
{
	for (auto &name : PlayerSettings_names) {
		g_settings->registerChangedCallback(name,
			&PlayerSettings::settingsChangedCallback, this);
	}
}

void PlayerSettings::deregisterSettingsCallback()
{
	g_settings->deregisterAllChangedCallbacks(this);
}

void PlayerSettings::settingsChangedCallback(const std::string &name, void *data)
{
	((PlayerSettings *)data)->readGlobalSettings();
}

/*
	LocalPlayer
*/

LocalPlayer::LocalPlayer(Client *client, const std::string &name):
	Player(name, client->idef()),
	m_client(client)
{
	m_player_settings.readGlobalSettings();
	m_player_settings.registerSettingsCallback();
}

LocalPlayer::~LocalPlayer()
{
	m_player_settings.deregisterSettingsCallback();
}

static aabb3f getNodeBoundingBox(const std::vector<aabb3f> &nodeboxes)
{
	if (nodeboxes.empty())
		return aabb3f(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

	auto it = nodeboxes.begin();
	aabb3f b_max(it->MinEdge, it->MaxEdge);

	++it;
	for (; it != nodeboxes.end(); ++it)
		b_max.addInternalBox(*it);

	return b_max;
}


bool LocalPlayer::updateSneakNode(Map *map, const v3f &position,
	const v3f &sneak_max)
{
	// Acceptable distance to node center
	// This must be > 0.5 units to get the sneak ladder to work
	// 0.05 prevents sideways teleporting through 1/16 thick walls
	constexpr f32 allowed_range = (0.5f + 0.05f) * BS;
	static const v3s16 dir9_center[9] = {
		v3s16( 0, 0,  0),
		v3s16( 1, 0,  0),
		v3s16(-1, 0,  0),
		v3s16( 0, 0,  1),
		v3s16( 0, 0, -1),
		v3s16( 1, 0,  1),
		v3s16(-1, 0,  1),
		v3s16( 1, 0, -1),
		v3s16(-1, 0, -1)
	};

	const NodeDefManager *nodemgr = m_client->ndef();
	MapNode node;
	bool is_valid_position;
	bool new_sneak_node_exists = m_sneak_node_exists;

	// We want the top of the sneak node to be below the players feet
	f32 position_y_mod = 0.02f * BS;
	if (m_sneak_node_exists)
		position_y_mod = m_sneak_node_bb_top.MaxEdge.Y - position_y_mod;

	// Get position of current standing node
	const v3s16 current_node = floatToInt(position - v3f(0.0f, position_y_mod, 0.0f), BS);

	if (current_node != m_sneak_node) {
		new_sneak_node_exists = false;
	} else {
		node = map->getNode(current_node, &is_valid_position);
		if (!is_valid_position || !nodemgr->get(node).walkable)
			new_sneak_node_exists = false;
	}

	// Keep old sneak node
	if (new_sneak_node_exists)
		return true;

	// Get new sneak node
	m_sneak_ladder_detected = false;
	f32 min_distance_sq = HUGE_VALF;

	for (const auto &d : dir9_center) {
		const v3s16 p = current_node + d;

		node = map->getNode(p, &is_valid_position);
		// The node to be sneaked on has to be walkable
		if (!is_valid_position || !nodemgr->get(node).walkable)
			continue;

		v3f pf = intToFloat(p, BS);
		{
			std::vector<aabb3f> nodeboxes;
			node.getCollisionBoxes(nodemgr, &nodeboxes);
			pf += getNodeBoundingBox(nodeboxes).getCenter();
		}

		const v2f diff(position.X - pf.X, position.Z - pf.Z);
		const f32 distance_sq = diff.getLengthSQ();

		if (distance_sq > min_distance_sq ||
				std::fabs(diff.X) > allowed_range + sneak_max.X ||
				std::fabs(diff.Y) > allowed_range + sneak_max.Z)
			continue;

		// And the node(s) above have to be nonwalkable
		bool ok = true;
		if (!physics_override.sneak_glitch) {
			u16 height =
				ceilf((m_collisionbox.MaxEdge.Y - m_collisionbox.MinEdge.Y) / BS);
			for (u16 y = 1; y <= height; y++) {
				node = map->getNode(p + v3s16(0, y, 0), &is_valid_position);
				if (!is_valid_position || nodemgr->get(node).walkable) {
					ok = false;
					break;
				}
			}
		} else {
			// legacy behavior: check just one node
			node = map->getNode(p + v3s16(0, 1, 0), &is_valid_position);
			ok = is_valid_position && !nodemgr->get(node).walkable;
		}
		if (!ok)
			continue;

		min_distance_sq = distance_sq;
		m_sneak_node = p;
		new_sneak_node_exists = true;
	}

	if (!new_sneak_node_exists)
		return false;

	// Update saved top bounding box of sneak node
	node = map->getNode(m_sneak_node);
	std::vector<aabb3f> nodeboxes;
	node.getCollisionBoxes(nodemgr, &nodeboxes);
	m_sneak_node_bb_top = getNodeBoundingBox(nodeboxes);

	if (physics_override.sneak_glitch) {
		// Detect sneak ladder:
		// Node two meters above sneak node must be solid
		node = map->getNode(m_sneak_node + v3s16(0, 2, 0),
			&is_valid_position);
		if (is_valid_position && nodemgr->get(node).walkable) {
			// Node three meters above: must be non-solid
			node = map->getNode(m_sneak_node + v3s16(0, 3, 0),
				&is_valid_position);
			m_sneak_ladder_detected = is_valid_position &&
				!nodemgr->get(node).walkable;
		}
	}
	return true;
}

void LocalPlayer::moveFreecam(f32 dtime, Environment *env, std::vector<CollisionInfo> *collision_info)
{
	v3f position = getPosition();

	position += getSpeed() * dtime;
	setPosition(position);

	return;
}

void LocalPlayer::move(f32 dtime, Environment *env, std::vector<CollisionInfo> *collision_info)
{
	v3f position = getLegitPosition();
	v3f speed = getLegitSpeed();
	// Node at feet position, update each ClientEnvironment::step()
	if (!collision_info || collision_info->empty())
		m_standing_node = floatToInt(position, BS);

	PlayerControl &correct_control =
	(m_freecam && !g_settings->getBool("lua_control")) ? empty_control :
	(g_settings->getBool("lua_control"))               ? lua_control :
	                                                     control;


	Map *map = &env->getMap();
	const NodeDefManager *nodemgr = m_client->ndef();


	// Copy parent position if local player is attached
	if (getParent()) {
		setLegitPosition(m_cao->getPosition());
		m_added_velocity = v3f(0.0f); // ignored
		return;
	}

	PlayerSettings &player_settings = getPlayerSettings();

	// Skip collision detection if noclip mode is used
	bool fly_allowed = m_client->checkLocalPrivilege("fly");
	bool noclip = (m_client->checkLocalPrivilege("noclip") && player_settings.noclip);
	bool free_move = (player_settings.free_move && fly_allowed);
	
	if (noclip && free_move) {
		position += speed * dtime;
		setLegitPosition(position);

		touching_ground = false;
		m_added_velocity = v3f(0.0f); // ignored
		return;
	}

	speed += m_added_velocity;
	m_added_velocity = v3f(0.0f);

	/*
		Collision detection
	*/

	bool is_valid_position;
	MapNode node;
	v3s16 pp;

	/*
		Check if player is in liquid (the oscillating value)
	*/

	// If in liquid, the threshold of coming out is at higher y
	if (in_liquid)
	{
		pp = floatToInt(position + v3f(0.0f, BS * 0.1f, 0.0f), BS);
		node = map->getNode(pp, &is_valid_position);
		if (is_valid_position) {
			const ContentFeatures &cf = nodemgr->get(node.getContent());
			in_liquid = cf.liquid_move_physics;
			move_resistance = cf.move_resistance;
		} else {
			in_liquid = false;
		}
	} else {
		// If not in liquid, the threshold of going in is at lower y

		pp = floatToInt(position + v3f(0.0f, BS * 0.5f, 0.0f), BS);
		node = map->getNode(pp, &is_valid_position);
		if (is_valid_position) {
			const ContentFeatures &cf = nodemgr->get(node.getContent());
			in_liquid = cf.liquid_move_physics;
			move_resistance = cf.move_resistance;
		} else {
			in_liquid = false;
		}
	}


	/*
		Check if player is in liquid (the stable value)
	*/
	pp = floatToInt(position + v3f(0.0f), BS);
	node = map->getNode(pp, &is_valid_position);
	if (is_valid_position) {
		in_liquid_stable = nodemgr->get(node.getContent()).liquid_move_physics;
	} else {
		in_liquid_stable = false;
	}

	/*
		Check if player is climbing
	*/

	pp = floatToInt(position + v3f(0.0f, 0.5f * BS, 0.0f), BS);
	v3s16 pp2 = floatToInt(position + v3f(0.0f, -0.2f * BS, 0.0f), BS);
	node = map->getNode(pp, &is_valid_position);
	bool is_valid_position2;
	MapNode node2 = map->getNode(pp2, &is_valid_position2);

	if (!(is_valid_position && is_valid_position2)) {
		is_climbing = false;
	} else {
		is_climbing = (nodemgr->get(node.getContent()).climbable ||
			nodemgr->get(node2.getContent()).climbable) && !free_move;
	}

	if (!is_climbing && !free_move && g_settings->getBool("spider")) {
		v3s16 spider_positions[4] = {
			floatToInt(position + v3f(+1.0f, +0.0f,  0.0f) * BS, BS),
			floatToInt(position + v3f(-1.0f, +0.0f,  0.0f) * BS, BS),
			floatToInt(position + v3f( 0.0f, +0.0f, +1.0f) * BS, BS),
			floatToInt(position + v3f( 0.0f, +0.0f, -1.0f) * BS, BS),
		};

		for (v3s16 sp : spider_positions) {
			bool is_valid;
			MapNode node = map->getNode(sp, &is_valid);

			if (is_valid && nodemgr->get(node.getContent()).walkable) {
				is_climbing = true;
				break;
			}
		}
	}


	// Player object property step height is multiplied by BS in
	// /src/script/common/c_content.cpp and /src/content_sao.cpp
	float player_stepheight = (m_cao == nullptr) ? 0.0f :
	((touching_ground ? m_cao->getStepHeight() : (0.2f * BS)) * 
	(g_settings->getBool("step") ? g_settings->getFloat("step.mult") : 
		(g_settings->getBool("scaffold") ? 2.0f : 1.0f)));

	v3f accel_f(0, -gravity, 0);
	const v3f initial_position = position;
	const v3f initial_speed = speed;

	collisionMoveResult result = collisionMoveSimple(env, m_client,
		m_collisionbox, player_stepheight, dtime,
		&position, &speed, accel_f, m_cao);

	bool could_sneak = correct_control.sneak && !free_move && !in_liquid &&
		!is_climbing && physics_override.sneak;

	// Add new collisions to the vector
	if (collision_info && !free_move) {
		v3f diff = intToFloat(m_standing_node, BS) - position;
		f32 distance_sq = diff.getLengthSQ();
		// Force update each ClientEnvironment::step()
		bool is_first = collision_info->empty();

		for (const auto &colinfo : result.collisions) {
			collision_info->push_back(colinfo);

			if (colinfo.type != COLLISION_NODE ||
					colinfo.axis != COLLISION_AXIS_Y ||
					(could_sneak && m_sneak_node_exists))
				continue;

			diff = intToFloat(colinfo.node_p, BS) - position;

			// Find nearest colliding node
			f32 len_sq = diff.getLengthSQ();
			if (is_first || len_sq < distance_sq) {
				m_standing_node = colinfo.node_p;
				distance_sq = len_sq;
				is_first = false;
			}
		}
	}

	/*
		If the player's feet touch the topside of any node
		at the END of clientstep, then this is set to true.

		Player is allowed to jump when this is true.
	*/
	bool touching_ground_was = touching_ground;
	touching_ground = result.touching_ground || (g_settings->getBool("airjump") && !correct_control.sneak);
	bool sneak_can_jump = false;

	// Max. distance (X, Z) over border for sneaking determined by collision box
	// * 0.49 to keep the center just barely on the node
	v3f sneak_max = m_collisionbox.getExtent() * 0.49f;

	if (m_sneak_ladder_detected) {
		// restore legacy behavior (this makes the speed.Y hack necessary)
		sneak_max = v3f(0.4f * BS, 0.0f, 0.4f * BS);
	}

	/*
		If sneaking, keep on top of last walked node and don't fall off
	*/
	if (could_sneak && m_sneak_node_exists && !g_settings->getBool("autosneak")) {
		const v3f sn_f = intToFloat(m_sneak_node, BS);
		const v3f bmin = sn_f + m_sneak_node_bb_top.MinEdge;
		const v3f bmax = sn_f + m_sneak_node_bb_top.MaxEdge;
		const v3f old_pos = position;
		const v3f old_speed = speed;
		f32 y_diff = bmax.Y - position.Y;
		m_standing_node = m_sneak_node;

		// (BS * 0.6f) is the basic stepheight while standing on ground
		if (y_diff < BS * 0.6f) {
			// Only center player when they're on the node
			position.X = rangelim(position.X,
				bmin.X - sneak_max.X, bmax.X + sneak_max.X);
			position.Z = rangelim(position.Z,
				bmin.Z - sneak_max.Z, bmax.Z + sneak_max.Z);

			if (position.X != old_pos.X)
				speed.X = 0.0f;
			if (position.Z != old_pos.Z)
				speed.Z = 0.0f;
		}

		if (y_diff > 0 && speed.Y <= 0.0f) {
			// Move player to the maximal height when falling or when
			// the ledge is climbed on the next step.

			v3f check_pos = position;
			check_pos.Y += y_diff * dtime * 22.0f + BS * 0.01f;
			if (y_diff < BS * 0.6f || (physics_override.sneak_glitch
					&& !collision_check_intersection(env, m_client, m_collisionbox, check_pos, m_cao))) {
				// Smoothen the movement (based on 'position.Y = bmax.Y')
				position.Y = std::min(check_pos.Y, bmax.Y);
				speed.Y = 0.0f;
			}
		}

		// Allow jumping on node edges while sneaking
		if (speed.Y == 0.0f || m_sneak_ladder_detected)
			sneak_can_jump = true;

		if (collision_info &&
				speed.Y - old_speed.Y > BS) {
			// Collide with sneak node, report fall damage
			CollisionInfo sn_info;
			sn_info.node_p = m_sneak_node;
			sn_info.old_speed = old_speed;
			sn_info.new_speed = speed;
			collision_info->push_back(sn_info);
		}
	}

	/*
		Find the next sneak node if necessary
	*/
	bool new_sneak_node_exists = false;

	if (could_sneak)
		new_sneak_node_exists = updateSneakNode(map, position, sneak_max);

	/*
		Set new position but keep sneak node set
	*/
	m_sneak_node_exists = new_sneak_node_exists;

	/*
		Report collisions
	*/

	if (!result.standing_on_object && !touching_ground_was && touching_ground) {
		m_client->getEventManager()->put(new SimpleTriggerEvent(MtEvent::PLAYER_REGAIN_GROUND));

		// Set camera impact value to be used for view bobbing
		camera_impact = getLegitSpeed().Y * -1;
	}

	/*
		Check properties of the node on which the player is standing
	*/
	const ContentFeatures &f = nodemgr->get(map->getNode(m_standing_node));
	const ContentFeatures &f1 = nodemgr->get(map->getNode(m_standing_node + v3s16(0, 1, 0)));

	// We can jump from a bouncy node we collided with this clientstep,
	// even if we are not "touching" it at the end of clientstep.
	int standing_node_bouncy = 0;
	if (result.collides && speed.Y > 0.0f) {
		// must use result.collisions here because sometimes collision_info
		// is passed in prepopulated with a problematic floor.
		for (const auto &colinfo : result.collisions) {
			if (colinfo.axis == COLLISION_AXIS_Y) {
				// we cannot rely on m_standing_node because "sneak stuff"
				standing_node_bouncy = itemgroup_get(nodemgr->get(map->getNode(colinfo.node_p)).groups, "bouncy");
				if (standing_node_bouncy != 0)
					break;
			}
		}
	}

	// Determine if jumping is possible
	m_disable_jump = itemgroup_get(f.groups, "disable_jump") ||
		itemgroup_get(f1.groups, "disable_jump");
	m_can_jump = ((touching_ground && !is_climbing) || sneak_can_jump || standing_node_bouncy != 0)
			&& !m_disable_jump;
	m_disable_descend = itemgroup_get(f.groups, "disable_descend") ||
		itemgroup_get(f1.groups, "disable_descend");

	// Jump/Sneak key pressed while bouncing from a bouncy block
	float jumpspeed = movement_speed_jump * physics_override.jump;
	if (m_can_jump && (correct_control.jump || correct_control.sneak) && standing_node_bouncy > 0) {
		// controllable (>0) bouncy block
		if (!correct_control.jump) {
			jumpspeed = -m_speed.Y / 3.0f;
		} else {
			// jump pressed
			// Reduce boost when speed already is high
			if (g_settings->getBool("BHOP")) {
				jumpspeed = jumpspeed;
			} else {
				jumpspeed = jumpspeed / (1.0f + (m_speed.Y * 2.8f / jumpspeed));
			}
		}
		speed.Y += jumpspeed;
		m_can_jump = false;
	} else if(speed.Y > jumpspeed && standing_node_bouncy < 0) {
		// uncontrollable bouncy is limited to normal jump height.
		m_can_jump = false;
	}

	// Prevent sliding on the ground when jump speed is 0
	m_can_jump = m_can_jump && jumpspeed != 0.0f;
	
	setLegitPosition(position);
	setLegitSpeed(speed);
	// Autojump
	handleAutojump(dtime, env, result, initial_position, initial_speed);
}

void LocalPlayer::move(f32 dtime, Environment *env)
{
	move(dtime, env, nullptr);
}

void LocalPlayer::applyFreecamControl(float dtime, Environment *env)
{
	setPitch(control.pitch);
	setYaw(control.yaw);
	
	PlayerSettings &player_settings = getPlayerSettings();

	v3f speedH, speedVFreecam; // Horizontal (X, Z) and Vertical (Y)

	bool pitch_move = player_settings.pitch_move;
	bool fast_climb = control.aux1 && !player_settings.aux1_descends;
	bool always_fly_fast = player_settings.always_fly_fast;

	// Whether superspeed mode is used or not
	bool superspeed = false;

	const f32 speed_walk = movement_speed_walk * physics_override.speed_walk;
	// const f32 speed_fast = movement_speed_fast * physics_override.speed_fast;

	f32 new_speed_fast = g_settings->getFloat("movement_speed_fast") * BS;

	if (always_fly_fast)
		superspeed = true;

	// Old descend control
	if (player_settings.aux1_descends) {
		// If free movement and fast movement, always move fast
		superspeed = true;

		// Auxiliary button 1 (E)
		if (control.aux1) {
				//.Y = -new_speed_fast;
		}
	} else {
		// New minecraft-like descend control

		// Auxiliary button 1 (E)
		if (control.aux1) {
			if (!is_climbing) {
				// aux1 is "Turbo button"
				superspeed = true;
			}
		}

		if (control.sneak && !control.jump) {
			// In free movement mode, sneak descends
			if (control.aux1 || always_fly_fast)
				speedVFreecam.Y = -new_speed_fast;
			else
				speedVFreecam.Y = -speed_walk;
		}
	}

	speedH = v3f(std::sin(control.movement_direction), 0.0f,
			std::cos(control.movement_direction));

	if (control.jump) {
		if (!control.sneak) {
			// Don't fly up if sneak key is pressed
			if (player_settings.aux1_descends || always_fly_fast) {
				speedVFreecam.Y = new_speed_fast;
			} else {
				if (control.aux1)
					speedVFreecam.Y = new_speed_fast;
				else
					speedVFreecam.Y = speed_walk;
			}
		}
	}

	speedH = speedH.normalize() * new_speed_fast;

	speedH *= control.movement_speed; /* Apply analog input */

	// Acceleration increase
	f32 incH = 0.0f; // Horizontal (X, Z)
	f32 incV = 0.0f; // Vertical (Y)
	
	if (superspeed || (is_climbing && fast_climb) || ((in_liquid || in_liquid_stable) && fast_climb)) {
		incH = incV = movement_acceleration_fast * physics_override.acceleration_fast * BS * dtime;
	} else {
		incH = incV = movement_acceleration_default * physics_override.acceleration_default * BS * dtime;
	}

	// Accelerate to target speed with maximum increment
	v3f target_speed = (speedH + speedVFreecam) * physics_override.speed;
	accelerateFreecam(target_speed,
		incH * physics_override.speed * 1.0f, incV * physics_override.speed,
		pitch_move);
}

void LocalPlayer::applyControl(float dtime, Environment *env)
{
	// Clear stuff
	swimming_vertical = false;
	swimming_pitch = false;

	setPitch(control.pitch);
	setYaw(control.yaw);

	// Nullify speed and don't run positioning code if the player is attached
	if (getParent()) {
		setLegitSpeed(v3f(0.0f));
		return;
	}

	PlayerControl &correct_control =
	(m_freecam && !g_settings->getBool("lua_control")) ? empty_control :
	(g_settings->getBool("lua_control"))               ? lua_control :
	                                                     control;

	PlayerSettings &player_settings = getPlayerSettings();

	// All vectors are relative to the player's yaw,
	// (and pitch if pitch move mode enabled),
	// and will be rotated at the end
	v3f speedH, speedV; // Horizontal (X, Z) and Vertical (Y)

	bool fly_allowed = m_client->checkLocalPrivilege("fly");
	bool fast_allowed = m_client->checkLocalPrivilege("fast");

	bool free_move = fly_allowed && player_settings.free_move;
	bool fast_move = fast_allowed && player_settings.fast_move;
	bool pitch_move = (free_move || in_liquid) && player_settings.pitch_move;
	// When aux1_descends is enabled the fast key is used to go down, so fast isn't possible
	bool fast_climb = fast_move && correct_control.aux1 && !player_settings.aux1_descends;
	bool always_fly_fast = player_settings.always_fly_fast;

	// Whether superspeed mode is used or not
	bool superspeed = false;

	const f32 speed_walk = movement_speed_walk * physics_override.speed_walk;
	// const f32 speed_fast = movement_speed_fast * physics_override.speed_fast;

	f32 new_speed_fast = g_settings->getFloat("movement_speed_fast") * BS;

	if (always_fly_fast && free_move && fast_move)
		superspeed = true;

	// Old descend control
	if (player_settings.aux1_descends) {
		// If free movement and fast movement, always move fast
		if (free_move && fast_move)
			superspeed = true;

		// Auxiliary button 1 (E)
		if (correct_control.aux1) {
			if (free_move) {
				// In free movement mode, aux1 descends
				if (fast_move)
					speedV.Y = -new_speed_fast;
				else
					speedV.Y = -speed_walk;
			} else if ((in_liquid || in_liquid_stable) && !m_disable_descend) {
				speedV.Y = -speed_walk;
				swimming_vertical = true;
			} else if (is_climbing && !m_disable_descend) {
				speedV.Y = -movement_speed_climb * physics_override.speed_climb;
			} else {
				// If not free movement but fast is allowed, aux1 is
				// "Turbo button"
				if (fast_move)
					superspeed = true;
			}
		}
	} else {
		// New minecraft-like descend control

		// Auxiliary button 1 (E)
		if (correct_control.aux1) {
			if (!is_climbing) {
				// aux1 is "Turbo button"
				if (fast_move)
					superspeed = true;
			}
		}

		if (correct_control.sneak && !correct_control.jump) {
			// Descend player in freemove mode, liquids and climbable nodes by sneak key, only if jump key is released
			if (free_move) {
				// In free movement mode, sneak descends
				if (fast_move && (correct_control.aux1 || always_fly_fast))
					speedV.Y = -new_speed_fast;
				else
					speedV.Y = -speed_walk;
			} else if ((in_liquid || in_liquid_stable) && !m_disable_descend) {
				if (fast_climb)
					speedV.Y = -new_speed_fast;
				else
					speedV.Y = -speed_walk;
				swimming_vertical = true;
			} else if (is_climbing && !m_disable_descend) {
				if (fast_climb)
					speedV.Y = -new_speed_fast;
				else
					speedV.Y = -movement_speed_climb * physics_override.speed_climb;
			}
		}
	}

	speedH = v3f(std::sin(correct_control.movement_direction), 0.0f,
			std::cos(correct_control.movement_direction));

	if (m_autojump) {
		// release autojump after a given time
		m_autojump_time -= dtime;
		if (m_autojump_time <= 0.0f)
			m_autojump = false;
	}

	if (g_settings->getBool("BHOP") && control.isMoving() && !g_settings->getBool("freecam") && !g_settings->getBool("free_move") && g_settings->getBool("BHOP.jump")) {
		control.jump = true;
	}

	if (g_settings->getBool("BHOP") && control.isMoving() && !g_settings->getBool("freecam") && !g_settings->getBool("free_move") && g_settings->getBool("BHOP.sprint")) {
		control.aux1 = true;
	}

	if (correct_control.jump) {
		if (free_move) {
			if (!correct_control.sneak) {
				// Don't fly up if sneak key is pressed
				if (player_settings.aux1_descends || always_fly_fast) {
					if (fast_move)
						speedV.Y = new_speed_fast;
					else
						speedV.Y = speed_walk;
				} else {
					if (fast_move && correct_control.aux1)
						speedV.Y = new_speed_fast;
					else
						speedV.Y = speed_walk;
				}
			}
		} else if (m_can_jump || g_settings->getBool("jetpack")) {
			/*
				NOTE: The d value in move() affects jump height by
				raising the height at which the jump speed is kept
				at its starting value
			*/
			v3f speedJ = getLegitSpeed();
			if (speedJ.Y >= -0.5f * BS || g_settings->getBool("jetpack")) {
				speedJ.Y = movement_speed_jump * physics_override.jump;
				setLegitSpeed(speedJ);
				m_client->getEventManager()->put(new SimpleTriggerEvent(MtEvent::PLAYER_JUMP));
			}
		} else if (in_liquid && !m_disable_jump && !correct_control.sneak) {
			if (fast_climb)
				speedV.Y = new_speed_fast;
			else
				speedV.Y = speed_walk;
			swimming_vertical = true;
		} else if (is_climbing && !m_disable_jump && !correct_control.sneak) {
			if (fast_climb)
				speedV.Y = new_speed_fast;
			else
				speedV.Y = movement_speed_climb * physics_override.speed_climb;
		}
	}

	// The speed of the player (Y is ignored)
	if (superspeed || (is_climbing && fast_climb) ||
			((in_liquid || in_liquid_stable) && fast_climb))
		speedH = speedH.normalize() * new_speed_fast;
	else if (correct_control.sneak && !free_move && !in_liquid && !in_liquid_stable && !g_settings->getBool("no_slow"))
		speedH = speedH.normalize() * movement_speed_crouch * physics_override.speed_crouch;
	else
		speedH = speedH.normalize() * speed_walk;

	speedH *= correct_control.movement_speed; /* Apply analog input */

	// Acceleration increase
	f32 incH = 0.0f; // Horizontal (X, Z)
	f32 incV = 0.0f; // Vertical (Y)
	if ((!touching_ground && !free_move && !is_climbing && !in_liquid) ||
			(!free_move && m_can_jump && correct_control.jump)) {
		// Jumping and falling
		if (superspeed || (fast_move && correct_control.aux1))
			incH = movement_acceleration_fast * physics_override.acceleration_fast * BS * dtime;
		else
			if (g_settings->getBool("BHOP")) {
				incH = (movement_acceleration_air*100) * (physics_override.acceleration_air*100) * BS * dtime;
			} else {
				incH = movement_acceleration_air * physics_override.acceleration_air * BS * dtime;
			}
		incV = 0.0f; // No vertical acceleration in air
	} else if (superspeed || (is_climbing && fast_climb) ||
			((in_liquid || in_liquid_stable) && fast_climb)) {
		incH = incV = movement_acceleration_fast * physics_override.acceleration_fast * BS * dtime;
	} else {
		if (g_settings->getBool("BHOP")) {
			incH = incV = (movement_acceleration_default*100) * (physics_override.acceleration_default*100) * BS * dtime;
		} else {
			incH = incV = movement_acceleration_default * physics_override.acceleration_default * BS * dtime;
		}
	}

	float slip_factor = 1.0f;
	if (!free_move && !in_liquid && !in_liquid_stable)
		slip_factor = getSlipFactor(env, speedH);

	// Don't sink when swimming in pitch mode
	if (pitch_move && in_liquid) {
		v3f controlSpeed = speedH + speedV;
		if (controlSpeed.getLength() > 0.01f)
			swimming_pitch = true;
	}

	// Accelerate to target speed with maximum increment
	if (g_settings->getBool("BHOP") && control.jump && g_settings->getBool("BHOP.speed")) {
		accelerate((speedH*1.2 + speedV*1.2) * physics_override.speed, incH * physics_override.speed * slip_factor, incV * physics_override.speed, pitch_move);
	} else {
 	accelerate((speedH + speedV) * physics_override.speed, incH * physics_override.speed * slip_factor, incV * physics_override.speed, pitch_move);
	}
}

v3s16 LocalPlayer::getStandingNodePos()
{
	if (m_sneak_node_exists)
		return m_sneak_node;

	return m_standing_node;
}

v3s16 LocalPlayer::getFootstepNodePos()
{
	v3f feet_pos = getLegitPosition() + v3f(0.0f, m_collisionbox.MinEdge.Y, 0.0f);

	// Emit swimming sound if the player is in liquid
	if (in_liquid_stable)
		return floatToInt(feet_pos, BS);

	// BS * 0.05 below the player's feet ensures a 1/16th height
	// nodebox is detected instead of the node below it.
	if (touching_ground)
		return floatToInt(feet_pos - v3f(0.0f, BS * 0.05f, 0.0f), BS);

	// A larger distance below is necessary for a footstep sound
	// when landing after a jump or fall. BS * 0.5 ensures water
	// sounds when swimming in 1 node deep water.
	return floatToInt(feet_pos - v3f(0.0f, BS * 0.5f, 0.0f), BS);
}

v3s16 LocalPlayer::getLightPosition() const
{
	return floatToInt(getPosition() + v3f(0.0f, BS * 1.5f, 0.0f), BS);
}

v3f LocalPlayer::getSendSpeed()
{
	v3f speed = getLegitSpeed();

	if (m_client->modsLoaded())
		speed = m_client->getScript()->get_send_speed(speed);

	return speed;
}

v3f LocalPlayer::getEyeOffset() const
{
	return v3f(0.0f, BS * m_eye_height, 0.0f);
}

ClientActiveObject *LocalPlayer::getParent() const
{
	return (m_cao && ! g_settings->getBool("entity_speed")) ? m_cao->getParent() : nullptr;
}

bool LocalPlayer::isDead() const
{
	FATAL_ERROR_IF(!getCAO(), "LocalPlayer's CAO isn't initialized");
	return !getCAO()->isImmortal() && hp == 0;
}

void LocalPlayer::tryReattach(int id)
{
	PointedThing pointed(id, v3f(0, 0, 0), v3f(0, 0, 0), v3f(0, 0, 0), 0, PointabilityType::POINTABLE);
	m_client->interact(INTERACT_PLACE, pointed);
	m_cao->m_waiting_for_reattach = 10;
}

bool LocalPlayer::isWaitingForReattach() const
{
	return g_settings->getBool("entity_speed") && m_cao && ! m_cao->getParent() && m_cao->m_waiting_for_reattach > 0;
}

EntityRelationship LocalPlayer::getEntityRelationship(GenericCAO *playerObj) {
	if (!playerObj->isPlayer()) {
		return EntityRelationship::NEUTRAL;
	}

	if (playerObj->isLocalPlayer()) {
		return EntityRelationship::FRIEND;
	}

	GenericCAO *me = getCAO();
	if (!me) {
		return EntityRelationship::NEUTRAL;
	}

	if (!m_client->m_simple_singleplayer_mode) {
		Address serverAddress = m_client->getServerAddress();
		std::string address = m_client->getAddressName().c_str();
		u16 port = serverAddress.getPort();
		std::string server_url = address + ":" + toPaddedString(port);

		Json::Value friends = {};
		try {
			friends = g_settings->getJson("friends");
		} catch (std::exception& e) {
			g_settings->set("friends", "{}");
		}

		if (!friends.isNull() && friends.isMember(server_url) && friends[server_url].isString()) {
			std::vector<std::string> server_friends = str_split(friends[server_url].asString(), ',');
			const std::string& playerName = playerObj->getName();
			if (!playerName.empty()) {
				for (std::vector<std::string>::iterator it = server_friends.begin(); it != server_friends.end(); ++it) {
					if (playerName == *it) {
						return EntityRelationship::FRIEND;
					}
				}
			}
		}

		Json::Value enemies = {};
		try {
			enemies = g_settings->getJson("enemies");
		} catch (std::exception& e) {
			g_settings->set("enemies", "{}");
		}

		if (!enemies.isNull() && enemies.isMember(server_url) && enemies[server_url].isString()) {
			std::vector<std::string> server_enemies = str_split(enemies[server_url].asString(), ',');
			const std::string& playerName = playerObj->getName();
			if (!playerName.empty()) {
				for (std::vector<std::string>::iterator it = server_enemies.begin(); it != server_enemies.end(); ++it) {
					if (playerName == *it) {
						return EntityRelationship::ENEMY;
					}
				}
			}
		}

		Json::Value allies = {};
		try {
			allies = g_settings->getJson("allies");
		} catch (std::exception& e) {
			g_settings->set("allies", "{}");
		}

		if (!allies.isNull() && allies.isMember(server_url) && allies[server_url].isString()) {
			std::vector<std::string> server_allies = str_split(allies[server_url].asString(), ',');
			const std::string& playerName = playerObj->getName();
			if (!playerName.empty()) {
				for (std::vector<std::string>::iterator it = server_allies.begin(); it != server_allies.end(); ++it) {
					if (playerName == *it) {
						return EntityRelationship::ALLY;
					}
				}
			}
		}
	}
	return EntityRelationship::ENEMY;
}


// 3D acceleration
void LocalPlayer::accelerateFreecam(const v3f &target_speed, const f32 max_increase_H, const f32 max_increase_V, const bool use_pitch)
{
	f32 yaw, pitch;
	yaw = getYaw();
	pitch = getPitch();
	v3f flat_speed = m_speed;
	// Rotate speed vector by -yaw and -pitch to make it relative to the player's yaw and pitch
	flat_speed.rotateXZBy(-yaw);
	if (use_pitch)
		flat_speed.rotateYZBy(-pitch);

	v3f d_wanted = target_speed - flat_speed;
	v3f d;
	// Then compare the horizontal and vertical components with the wanted speed
	if (max_increase_H > 0.0f) {
		v3f d_wanted_H = d_wanted * v3f(1.0f, 0.0f, 1.0f);
		if (d_wanted_H.getLength() > max_increase_H)
			d += d_wanted_H.normalize() * max_increase_H;
		else
			d += d_wanted_H;
	}
	
	if (max_increase_V > 0.0f) {
		f32 d_wanted_V = d_wanted.Y;
		if (d_wanted_V > max_increase_V)
			d.Y += max_increase_V;
		else if (d_wanted_V < -max_increase_V)
			d.Y -= max_increase_V;
		else
			d.Y += d_wanted_V;
	}

	// Finally rotate it again
	if (use_pitch)
		d.rotateYZBy(pitch);
	d.rotateXZBy(yaw);

	m_speed += d;
}

void LocalPlayer::accelerate(const v3f &target_speed, const f32 max_increase_H,
	const f32 max_increase_V, const bool use_pitch)
{	
	f32 yaw, pitch;

	if (g_settings->getBool("detached_camera") || g_settings->getBool("freecam")) {
		yaw = getLegitYaw();
		pitch = getLegitPitch();
	} else {
		yaw = getYaw();
		pitch = getPitch();
	}
	v3f speed = getLegitSpeed();
	v3f flat_speed = speed;
	// Rotate speed vector by -yaw and -pitch to make it relative to the player's yaw and pitch
	flat_speed.rotateXZBy(-yaw);
	if (use_pitch)
		flat_speed.rotateYZBy(-pitch);

	v3f d_wanted = target_speed - flat_speed;
	v3f d;

	// Then compare the horizontal and vertical components with the wanted speed
	if (max_increase_H > 0.0f) {
		v3f d_wanted_H = d_wanted * v3f(1.0f, 0.0f, 1.0f);
		if (d_wanted_H.getLength() > max_increase_H)
			d += d_wanted_H.normalize() * max_increase_H;
		else
			d += d_wanted_H;
	}
	if (max_increase_V > 0.0f) {
		f32 d_wanted_V = d_wanted.Y;
		if (d_wanted_V > max_increase_V)
			d.Y += max_increase_V;
		else if (d_wanted_V < -max_increase_V)
			d.Y -= max_increase_V;
		else
			d.Y += d_wanted_V;
	}

	// Finally rotate it again
	if (use_pitch)
		d.rotateYZBy(pitch);
	d.rotateXZBy(yaw);

	speed += d;
	setLegitSpeed(speed);
}

float LocalPlayer::getSlipFactor(Environment *env, const v3f &speedH)
{
	// Slip on slippery nodes
	const NodeDefManager *nodemgr = env->getGameDef()->ndef();
	Map *map = &env->getMap();
	const ContentFeatures &f = nodemgr->get(map->getNode(getStandingNodePos()));
	int slippery = 0;
	if (f.walkable && !g_settings->getBool("antislip"))
		slippery = itemgroup_get(f.groups, "slippery");

	if (slippery >= 1) {
		if (speedH == v3f(0.0f))
			slippery *= 2;

		return core::clamp(1.0f / (slippery + 1), 0.001f, 1.0f);
	}
	return 1.0f;
}

void LocalPlayer::handleAutojump(f32 dtime, Environment *env, const collisionMoveResult &result, v3f initial_position, v3f initial_speed)
{
	PlayerControl &correct_control =
	(m_freecam && !g_settings->getBool("lua_control")) ? empty_control :
	(g_settings->getBool("lua_control"))               ? lua_control :
	                                                     control;
														 
	v3f position = getLegitPosition();
	PlayerSettings &player_settings = getPlayerSettings();
	if (!player_settings.autojump)
		return;

	if (m_autojump)
		return;

	bool could_autojump =
		m_can_jump && !correct_control.jump && !correct_control.sneak && correct_control.isMoving();

	if (!could_autojump)
		return;

	bool horizontal_collision = false;
	for (const auto &colinfo : result.collisions) {
		if (colinfo.type == COLLISION_NODE && colinfo.axis != COLLISION_AXIS_Y) {
			horizontal_collision = true;
			break; // one is enough
		}
	}

	// must be running against something to trigger autojumping
	if (!horizontal_collision)
		return;

	// check for nodes above
	v3f headpos_min = position + m_collisionbox.MinEdge * 0.99f;
	v3f headpos_max = position + m_collisionbox.MaxEdge * 0.99f;
	headpos_min.Y = headpos_max.Y; // top face of collision box
	v3s16 ceilpos_min = floatToInt(headpos_min, BS) + v3s16(0, 1, 0);
	v3s16 ceilpos_max = floatToInt(headpos_max, BS) + v3s16(0, 1, 0);
	const NodeDefManager *ndef = env->getGameDef()->ndef();
	bool is_position_valid;
	for (s16 z = ceilpos_min.Z; z <= ceilpos_max.Z; ++z) {
		for (s16 x = ceilpos_min.X; x <= ceilpos_max.X; ++x) {
			MapNode n = env->getMap().getNode(v3s16(x, ceilpos_max.Y, z), &is_position_valid);

			if (!is_position_valid)
				break;  // won't collide with the void outside
			if (n.getContent() == CONTENT_IGNORE)
				return; // players collide with ignore blocks -> same as walkable
			const ContentFeatures &f = ndef->get(n);
			if (f.walkable)
				return; // would bump head, don't jump
		}
	}

	float jumpspeed = movement_speed_jump * physics_override.jump;
	float peak_dtime = jumpspeed / gravity; // at the peak of the jump v = gt <=> t = v / g
	float jump_height = (jumpspeed - 0.5f * gravity * peak_dtime) * peak_dtime; // s = vt - 1/2 gt^2
	v3f jump_pos = initial_position + v3f(0.0f, jump_height, 0.0f);
	v3f jump_speed = initial_speed;

	// try at peak of jump, zero step height
	collisionMoveResult jump_result = collisionMoveSimple(env, m_client,
		m_collisionbox, 0.0f, dtime, &jump_pos, &jump_speed, v3f(0.0f), m_cao);

	// see if we can get a little bit farther horizontally if we had
	// jumped
	v3f run_delta = position - initial_position;
	run_delta.Y = 0.0f;
	v3f jump_delta = jump_pos - initial_position;
	jump_delta.Y = 0.0f;
	if (jump_delta.getLengthSQ() > run_delta.getLengthSQ() * 1.01f) {
		m_autojump = true;
		m_autojump_time = 0.1f;
	}
}

void LocalPlayer::setPitch(f32 pitch) {
	m_pitch = pitch;
	if (!m_freecam && !g_settings->getBool("detached_camera"))
		m_legit_pitch = m_pitch;
}
void LocalPlayer::setLegitPitch(f32 pitch) {
	if (m_freecam || g_settings->getBool("detached_camera"))
		m_legit_pitch = pitch;
	else
		setPitch(pitch);
}

void LocalPlayer::setYaw(f32 yaw) {
	m_yaw = yaw;
	if (!m_freecam && !g_settings->getBool("detached_camera"))
		m_legit_yaw = m_yaw;
}
void LocalPlayer::setLegitYaw(f32 yaw) {
	if (m_freecam || g_settings->getBool("detached_camera"))
		m_legit_yaw = yaw;
	else
		setYaw(yaw);
}