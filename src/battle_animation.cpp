/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "bitmap.h"
#include "rpg_animation.h"
#include "output.h"
#include "game_battle.h"
#include "game_system.h"
#include "game_map.h"
#include "graphics.h"
#include "main_data.h"
#include "filefinder.h"
#include "cache.h"
#include "battle_animation.h"
#include "baseui.h"
#include "spriteset_battle.h"
#include "player.h"
#include "game_temp.h"

BattleAnimation::BattleAnimation(const RPG::Animation& anim, bool only_sound, int cutoff) :
	animation(anim), frame(0), should_only_sound(only_sound)
{
	num_frames = GetRealFrames() * 2;
	if (cutoff >= 0 && cutoff < num_frames) {
		num_frames = cutoff;
	}

	SetZ(Priority_BattleAnimation);

	const std::string& name = animation.animation_name;
	BitmapRef graphic;

	if (name.empty()) return;

	if (animation.large) {
		FileRequestAsync* request = AsyncHandler::RequestFile("Battle2", name);
		request->SetGraphicFile(true);
		request_id = request->Bind(&BattleAnimation::OnBattle2SpriteReady, this);
		request->Start();
	} else {
		FileRequestAsync* request = AsyncHandler::RequestFile("Battle", name);
		request->SetGraphicFile(true);
		request_id = request->Bind(&BattleAnimation::OnBattleSpriteReady, this);
		request->Start();
	}
}

DrawableType BattleAnimation::GetType() const {
	return TypeDefault;
}

void BattleAnimation::Update() {
	if (!IsDone() && (frame & 1) == 0) {
		// Lookup any timed SFX (SE/flash/shake) data for this frame
		for (auto& timing: animation.timings) {
			if (timing.frame == GetRealFrame() + 1) {
				ProcessAnimationTiming(timing);
			}
		}
	}

	auto flash_color = Main_Data::game_screen->GetFlashColor();
	if (flash_color.alpha > 0) {
		Sprite::Flash(flash_color, 0);
	} else {
		Sprite::Flash(Color(), 0);
	}

	Sprite::Update();

	frame++;
}

void BattleAnimation::OnBattleSpriteReady(FileRequestResult* result) {
	BitmapRef bitmap = Cache::Battle(result->file);
	SetBitmap(bitmap);
	SetSrcRect(Rect(0, 0, 0, 0));
}

void BattleAnimation::OnBattle2SpriteReady(FileRequestResult* result) {
	BitmapRef bitmap = Cache::Battle2(result->file);
	SetBitmap(bitmap);
	SetSrcRect(Rect(0, 0, 0, 0));
}

void BattleAnimation::DrawAt(int x, int y) {
	if (IsDone()) {
		return;
	}
	if (!GetVisible()) {
		return;
	}

	const RPG::AnimationFrame& anim_frame = animation.frames[GetRealFrame()];

	std::vector<RPG::AnimationCellData>::const_iterator it;
	for (it = anim_frame.cells.begin(); it != anim_frame.cells.end(); ++it) {
		const RPG::AnimationCellData& cell = *it;

		if (!cell.valid) {
			// Skip unused cells (they are created by deleting cells in the
			// animation editor, resulting in gaps)
			continue;
		}

		SetX(cell.x + x);
		SetY(cell.y + y);
		int sx = cell.cell_id % 5;
		int sy = cell.cell_id / 5;
		int size = animation.large ? 128 : 96;
		SetSrcRect(Rect(sx * size, sy * size, size, size));
		SetOx(size / 2);
		SetOy(size / 2);
		SetTone(Tone(cell.tone_red * 128 / 100,
			cell.tone_green * 128 / 100,
			cell.tone_blue * 128 / 100,
			cell.tone_gray * 128 / 100));
		SetOpacity(255 * (100 - cell.transparency) / 100);
		SetZoomX(cell.zoom / 100.0);
		SetZoomY(cell.zoom / 100.0);
		Sprite::Draw();
	}

	if (anim_frame.cells.empty()) {
		// Draw an empty sprite when no cell is available in the animation
		SetSrcRect(Rect(0, 0, 0, 0));
		Sprite::Draw();
	}
}

bool BattleAnimation::ShouldOnlySound() const {
	return should_only_sound;
}

// FIXME: looks okay, but needs to be measured
static int flash_length = 12;

void BattleAnimation::ProcessAnimationTiming(const RPG::AnimationTiming& timing) {
	// Play the SE.
	Game_System::SePlay(timing.se);
	if (ShouldOnlySound())
		return;

	// Flash.
	if (timing.flash_scope == RPG::AnimationTiming::FlashScope_target) {
		SetFlash(timing.flash_red,
			timing.flash_green,
			timing.flash_blue,
			timing.flash_power);
	} else if (timing.flash_scope == RPG::AnimationTiming::FlashScope_screen && ShouldScreenFlash()) {
		Main_Data::game_screen->FlashOnce(
			timing.flash_red,
			timing.flash_green,
			timing.flash_blue,
			timing.flash_power,
			flash_length);
	}

	// Shake (only happens in battle).
	if (Game_Temp::battle_running) {
		switch (timing.screen_shake) {
		case RPG::AnimationTiming::ScreenShake_nothing:
			break;
		case RPG::AnimationTiming::ScreenShake_target:
			// TODO: shake the targets
			break;
		case RPG::AnimationTiming::ScreenShake_screen:
			Game_Screen* screen = Main_Data::game_screen.get();
			// FIXME: 8,7,3 are made up
			screen->ShakeOnce(8, 7, 3);
			break;
		}
	}

}

// For handling the vertical position.
// (The first argument should be an RPG::Animation::Position,
// but the position member is an int, so take an int.)
static int CalculateOffset(int pos, int target_height) {
	switch (pos) {
	case RPG::Animation::Position_down:
		return target_height / 2;
	case RPG::Animation::Position_up:
		return -(target_height / 2);
	default:
		return 0;
	}
}

/////////

BattleAnimationChara::BattleAnimationChara(const RPG::Animation& anim, Game_Character& chara) :
	BattleAnimation(anim), character(chara)
{
	Graphics::RegisterDrawable(this);
}
BattleAnimationChara::~BattleAnimationChara() {
	Graphics::RemoveDrawable(this);
}
void BattleAnimationChara::Draw() {
	if (ShouldOnlySound())
		return;
	//If animation is targeted on the screen
	if (animation.scope == RPG::Animation::Scope_screen) {
		DrawAt(SCREEN_TARGET_WIDTH / 2, SCREEN_TARGET_HEIGHT / 2);
		return;
	}
	const int character_height = 24;
	int vertical_center = character.GetScreenY() - character_height/2;
	int offset = CalculateOffset(animation.position, character_height);
	DrawAt(character.GetScreenX(), vertical_center + offset);
}
void BattleAnimationChara::SetFlash(int r, int g, int b, int p) {
	character.Flash(r, g, b, p, flash_length);
}

bool BattleAnimationChara::ShouldScreenFlash() const { return true; }

/////////

BattleAnimationBattlers::BattleAnimationBattlers(const RPG::Animation& anim, Game_Battler& batt, bool flash, bool only_sound, int cutoff_frame) :
	BattleAnimation(anim, only_sound, cutoff_frame), battlers(std::vector<Game_Battler*>(1, &batt)), should_flash(flash)
{
	Graphics::RegisterDrawable(this);
}
BattleAnimationBattlers::BattleAnimationBattlers(const RPG::Animation& anim, const std::vector<Game_Battler*>& batts, bool flash, bool only_sound, int cutoff_frame) :
	BattleAnimation(anim, only_sound, cutoff_frame), battlers(batts), should_flash(flash)
{
	Graphics::RegisterDrawable(this);
}
BattleAnimationBattlers::~BattleAnimationBattlers() {
	Graphics::RemoveDrawable(this);
}
void BattleAnimationBattlers::Draw() {
	if (ShouldOnlySound())
		return;
	if (animation.scope == RPG::Animation::Scope_screen) {
		DrawAt(SCREEN_TARGET_WIDTH / 2, SCREEN_TARGET_HEIGHT / 3);
		return;
	}

	for (std::vector<Game_Battler*>::const_iterator it = battlers.begin();
	     it != battlers.end(); ++it) {
		const Game_Battler& battler = **it;
		const Sprite_Battler* sprite = Game_Battle::GetSpriteset().FindBattler(&battler);
		int offset = 0;
		if (sprite && sprite->GetBitmap()) {
			offset = CalculateOffset(animation.position, sprite->GetHeight());
		}
		DrawAt(battler.GetBattleX(), battler.GetBattleY() + offset);
	}
}
void BattleAnimationBattlers::SetFlash(int r, int g, int b, int p) {
	auto color = MakeFlashColor(r, g, b, p);
	for (std::vector<Game_Battler*>::const_iterator it = battlers.begin();
	     it != battlers.end(); ++it) {
		Sprite_Battler* sprite = Game_Battle::GetSpriteset().FindBattler(*it);
		if (sprite)
			sprite->Flash(color, flash_length);
	}
}
bool BattleAnimationBattlers::ShouldScreenFlash() const { return should_flash; }

/////////

BattleAnimationGlobal::BattleAnimationGlobal(const RPG::Animation& anim) :
	BattleAnimation(anim)
{
	Graphics::RegisterDrawable(this);
}
BattleAnimationGlobal::~BattleAnimationGlobal() {
	Graphics::RemoveDrawable(this);
}
void BattleAnimationGlobal::Draw() {
	if (ShouldOnlySound())
		return;
	// The animations are played at the vertices of a regular grid,
	// 20 tiles wide by 10 tiles high, independant of the map.
	// NOTE: not accurate, but see #574
	const int x_stride = 20 * TILE_SIZE;
	const int y_stride = 10 * TILE_SIZE;
	int x_offset = (Game_Map::GetDisplayX()/TILE_SIZE) % x_stride;
	int y_offset = (Game_Map::GetDisplayY()/TILE_SIZE) % y_stride;
	for (int y = 0; y != 3; ++y) {
		for (int x = 0; x != 3; ++x) {
			DrawAt(x_stride*x - x_offset, y_stride*y - y_offset);
		}
	}
}
void BattleAnimationGlobal::SetFlash(int r, int g, int b, int p) {
	// nop
}
bool BattleAnimationGlobal::ShouldScreenFlash() const { return true; }
