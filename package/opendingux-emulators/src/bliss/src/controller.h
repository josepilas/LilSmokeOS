#ifndef CONTROLLER_H
#define CONTROLLER_H
/*
	This file is part of FreeIntv.

	FreeIntv is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	FreeIntv is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with FreeIntv; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

extern int controllerSwap;

extern int keypadStates[];

// Keypad button codes (used for controller mapping)
#define K_1 0x81
#define K_2 0x41
#define K_3 0x21
#define K_4 0x82
#define K_5 0x42
#define K_6 0x22
#define K_7 0x84
#define K_8 0x44
#define K_9 0x24
#define K_0 0x48
#define K_C 0x88  // * (asterisk/star)
#define K_E 0x28  // # (hash/pound)

void controllerInit(void);

int getControllerState(int joypad[], int player);

int getKeypadState(int player, int joypad[], int joypre[]);

void setControllerInput(int player, int state); 

void drawMiniKeypad(int player, unsigned int frame[]);

#endif
