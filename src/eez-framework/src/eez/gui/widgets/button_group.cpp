/*
* EEZ Generic Firmware
* Copyright (C) 2015-present, Envox d.o.o.
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

#include <eez/conf-internal.h>

#if EEZ_OPTION_GUI

#include <eez/core/sound.h>
#include <eez/core/util.h>

#include <eez/gui/gui.h>
#include <eez/gui/widgets/button_group.h>

namespace eez {
namespace gui {

void drawButtons(const WidgetCursor &widgetCursor, const Style *style, const Style *selectedStyle, int selectedButton, int count, ArrayValue *buttonLabelsArray = nullptr) {
    auto x = widgetCursor.x;
    auto y = widgetCursor.y;

    WidgetCursor widgetCursorForLabel = widgetCursor;

    if (widgetCursor.w > widgetCursor.h) {
        // horizontal orientation
        int w = widgetCursor.w / count;
        int h = widgetCursor.h;
        for (Cursor i = 0; i < count; i++) {
            char text[32];
            if (buttonLabelsArray) {
                buttonLabelsArray->values[i].toText(text, 32);
            } else {
                widgetCursorForLabel.cursor = i;
                getLabel(widgetCursorForLabel, widgetCursorForLabel.widget->data, text, 32);
            }
            if (i < count - 1) {
                drawText(text, -1, x, y, w, h, i == selectedButton ? selectedStyle : style);
                x += w;
            } else {
                drawText(text, -1, x, y, widgetCursor.x + widgetCursor.w - x, h, i == selectedButton ? selectedStyle : style);
            }
        }
    } else {
        // vertical orientation
        int w = widgetCursor.w;
        int h = widgetCursor.h / count;

        int bottom = y + widgetCursor.h - 1;
        int topPadding = (widgetCursor.h - h * count) / 2;

        if (topPadding > 0) {
            display::setColor(style->backgroundColor);
            display::fillRect(x, y, x + widgetCursor.w - 1, y + topPadding - 1);

            y += topPadding;
        }

        int labelHeight = MIN(w, h);
        int yOffset = (h - labelHeight) / 2;

		for (Cursor i = 0; i < count; i++) {
            if (yOffset > 0) {
                display::setColor(style->backgroundColor);
                display::fillRect(x, y, x + widgetCursor.w - 1, y + yOffset - 1);
            }

            char text[32];
            if (buttonLabelsArray) {
                buttonLabelsArray->values[i].toText(text, 32);
            } else {
			    widgetCursorForLabel.cursor = i;
                getLabel(widgetCursorForLabel, widgetCursorForLabel.widget->data, text, 32);
            }
            drawText(text, -1, x, y + yOffset, w, labelHeight, i == selectedButton ? selectedStyle: style);

            int b = y + yOffset + labelHeight;

            y += h;

            if (b < y) {
                display::setColor(style->backgroundColor);
                display::fillRect(x, b, x + widgetCursor.w - 1, y - 1);
            }
        }

        if (y <= bottom) {
            display::setColor(style->backgroundColor);
            display::fillRect(x, y, x + widgetCursor.w - 1, bottom);
        }
    }
}

bool ButtonGroupWidgetState::updateState() {
    WIDGET_STATE_START(ButtonGroupWidget);

    WIDGET_STATE(flags.active, g_isActiveWidget);
    WIDGET_STATE(data, get(widgetCursor, widget->data));
    if (widgetCursor.flowState) {
        WIDGET_STATE(selectedButton, get(widgetCursor, widget->selectedButton));
    }

    WIDGET_STATE_END()
}

void ButtonGroupWidgetState::render() {
    const WidgetCursor &widgetCursor = g_widgetCursor;

    auto widget = (const ButtonGroupWidget *)widgetCursor.widget;

    const Style* style = getStyle(widget->style);
    const Style* selectedStyle = getStyle(widget->selectedStyle);

    if (widgetCursor.flowState) {
        auto buttonLabelsArray = data.getArray();
        drawButtons(widgetCursor, style, selectedStyle, selectedButton.getInt(), buttonLabelsArray->arraySize, buttonLabelsArray);
    } else {
        drawButtons(widgetCursor, style, selectedStyle, data.getInt(), count(widgetCursor, widget->data), nullptr);
    }
}

bool ButtonGroupWidgetState::hasOnTouch() {
    return true;
}

void ButtonGroupWidgetState::onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
        auto widget = (const ButtonGroupWidget *)widgetCursor.widget;

        int count_ = data.isArray() ? data.getArray()->arraySize : count(widgetCursor, widget->data);

        int selectedButton;
        if (widgetCursor.w > widgetCursor.h) {
            int w = widgetCursor.w / count_;
            int x = widgetCursor.x + (widgetCursor.w - w * count_) / 2;

            selectedButton = (touchEvent.x - x) / w;
        } else {
            int h = widgetCursor.h / count_;
            int y = widgetCursor.y + (widgetCursor.h - h * count_) / 2;
            selectedButton = (touchEvent.y - y) / h;
        }

        if (selectedButton >= 0 && selectedButton < count_) {
            set(widgetCursor, data.isArray() ? widget->selectedButton : widget->data, selectedButton);
            sound::playClick();
        }
    }
}

} // namespace gui
} // namespace eez

#endif // EEZ_OPTION_GUI
