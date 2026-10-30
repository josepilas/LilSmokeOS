/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MADS_DRAGONSPHERE_ROOMS_101_H
#define MADS_DRAGONSPHERE_ROOMS_101_H

#include "common/serializer.h"
#include "mads/madsv2/core/general.h"

namespace MADS {
namespace MADSV2 {
namespace Dragonsphere {
namespace Rooms {

#define local (&scratch)
#define ss    local->sprite
#define seq   local->sequence
#define aa    local->animation

/**
 * Room local variables
 */
struct Scratch {
	int16 x02;
	int16 x04;
	int16 x06;
	int16 x08;
	int16 x0a;
	int16 x0c;
	int16 x20;
	int16 x22;
	int16 x24;
	int16 x26;
	int16 x28;
	int16 x2a;
	int16 x3c;
	int16 x3e;
	int16 x44;
	int16 x46;
	int16 x48;
	int16 x4a;
	int16 x4c;
	int16 x4e;
	int16 x50;
	int16 x54;
	int16 x56;
	int32 x58;
	int32 x5c;
	int32 x60;
	int16 x64;
	int16 x66;
	int16 x68;
	int16 x6a;
	int16 x6c;
};


/* ========================= Sprite Series =================== */

#define fx_fire              1  /* rm101x0 */
#define fx_fire_shadow       2  /* rm101x1 */
#define fx_door              3  /* rm101x2 */
#define fx_sconce_fire       4  /* rm101x3 */
#define fx_draped_cape       5  /* rm101a5 */
#define fx_open_door         6  /* kgrd_6  */


/* ========================== Triggers ======================= */

#define ROOM_101_I_AM_SO_REFRESHED  70
#define ROOM_101_DOOR_CLOSES        80


#define CONVERSATION_WITH_QUEEN     0

/* Walk points */
#define START_X_ROOM_102        330
#define START_Y_ROOM_102        126
#define START_X_ROOM_103        114
#define START_Y_ROOM_103        152

#define WALK_TO_X_FROM_102      297
#define WALK_TO_Y_FROM_102      134

#define WALK_TO_DOOR_X          319
#define WALK_TO_DOOR_Y          129

#define START_BEHIND_SCREEN_X   76
#define START_BEHIND_SCREEN_Y   100

#define WALK_FROM_SCREEN_X      102
#define WALK_FROM_SCREEN_Y      100

#define MIDDLE_ROOM_X           109
#define MIDDLE_ROOM_Y           122

/* Queen states (rm101b.aa) */
#define CONV0_WALK_IN         1
#define CONV0_STIR            2
#define CONV0_STAND_1         3
#define CONV0_STAND_2         4
#define CONV0_SHAKE_FIST      5
#define CONV0_TALK_LEAVE      6
#define CONV0_LEAVE           7

/* King states (rm101c.aa) */
#define CONV0_KING_SLEEP      1
#define CONV0_KING_TALK       2
#define CONV0_KING_GET_UP     3

/* Animation running flags */
#define BOOK                  1
#define QUEEN                 2
#define KING                  3

/* Timer macros */
#define TIME_TO_MOVE_1        200
#define TIME_TO_MOVE_2        300
#define TIME_TO_MOVE_3        400
#define TIME_TO_MOVE_4        500
#define TIME_TO_MOVE_5        600
#define TIME_TO_MOVE_6        700
#define TIME_TO_MOVE_7        300

/* Random range */
#define RANDOM_LOWEST_NUMBER  1
#define RANDOM_HIGHEST_NUMBER 6


extern void room_101_preload();
extern void room_101_init();
extern void room_101_daemon();
extern void room_101_pre_parser();
extern void room_101_parser();
extern void room_101_error();
extern void room_101_synchronize(Common::Serializer &s);

} // namespace Rooms
} // namespace Dragonsphere
} // namespace MADSV2
} // namespace MADS

#endif
