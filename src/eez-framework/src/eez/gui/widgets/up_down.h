/*
 * EEZ Modular Firmware
 * Copyright (C) 2021-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

namespace eez {
namespace gui {

struct UpDownWidget : public Widget {
    AssetsPtr<const char> downButtonText;
	AssetsPtr<const char> upButtonText;
    int16_t buttonsStyle;
    int16_t min;
    int16_t max;
};

enum UpDownWidgetSegment {
    UP_DOWN_WIDGET_SEGMENT_TEXT,
    UP_DOWN_WIDGET_SEGMENT_DOWN_BUTTON,
    UP_DOWN_WIDGET_SEGMENT_UP_BUTTON
};

struct UpDownWidgetState : public WidgetState {
	WidgetStateFlags flags;
	Value data;
	UpDownWidgetSegment segment;
    Value min;
    Value max;

	static WidgetCursor g_selectedWidget;

	bool updateState() override;
    void render() override;

	bool hasOnTouch() override;
	void onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) override;
	bool hasOnKeyboard() override;
	bool onKeyboard(const WidgetCursor &widgetCursor, uint8_t key, uint8_t mod) override;

private:
	void upDown(const WidgetCursor &widgetCursor, UpDownWidgetSegment segment_);
};

} // namespace gui
} // namespace eez
