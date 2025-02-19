/*
 * EEZ Modular Firmware
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

#include <eez/core/util.h>
#include <eez/core/sound.h>

#if OPTION_KEYBOARD
#include <eez/core/keyboard.h>
#endif

#include <eez/gui/gui.h>
#include <eez/gui/widgets/up_down.h>

namespace eez {
namespace gui {

bool UpDownWidgetState::updateState() {
    WIDGET_STATE_START(UpDownWidget);

    WIDGET_STATE(flags.active, g_isActiveWidget);
    WIDGET_STATE(data, get(widgetCursor, widget->data));

    if (widgetCursor.flowState) {
        WIDGET_STATE(min, get(widgetCursor, widget->min));
        WIDGET_STATE(max, get(widgetCursor, widget->max));
    }

    WIDGET_STATE_END()
}

void UpDownWidgetState::render() {
    const WidgetCursor &widgetCursor = g_widgetCursor;

    auto widget = (const UpDownWidget *)widgetCursor.widget;
    const Style *buttonsStyle = getStyle(widget->buttonsStyle);

    font::Font buttonsFont = styleGetFont(buttonsStyle);
    int buttonWidth = buttonsStyle->paddingLeft + buttonsFont.getHeight() + buttonsStyle->paddingRight;

    if (widget->downButtonText) {
        drawText(
            static_cast<const char *>(widget->downButtonText), -1,
            widgetCursor.x, widgetCursor.y, buttonWidth, widgetCursor.h,
            buttonsStyle,
            flags.active && segment == UP_DOWN_WIDGET_SEGMENT_DOWN_BUTTON
        );
    }

    char text[64];
    data.toText(text, sizeof(text));
    const Style *style = getStyle(widget->style);
    drawText(
        text, -1,
        widgetCursor.x + buttonWidth, widgetCursor.y, (int)(widgetCursor.w - 2 * buttonWidth), widgetCursor.h,
        style
    );

    if (widget->upButtonText) {
        drawText(
            static_cast<const char *>(widget->upButtonText), -1,
            widgetCursor.x + widgetCursor.w - buttonWidth, widgetCursor.y, buttonWidth, widgetCursor.h,
            buttonsStyle,
            flags.active && segment == UP_DOWN_WIDGET_SEGMENT_UP_BUTTON
        );
    }
}

void UpDownWidgetState::upDown(const WidgetCursor &widgetCursor, UpDownWidgetSegment segment_) {
    segment = segment_;

    const Widget *widget = widgetCursor.widget;

    int value = get(widgetCursor, widget->data).getInt();

    int newValue = value;

    if (segment == UP_DOWN_WIDGET_SEGMENT_DOWN_BUTTON) {
        --newValue;
    } else if (segment == UP_DOWN_WIDGET_SEGMENT_UP_BUTTON) {
        ++newValue;
    }

    int min;
    int max;

    if (widgetCursor.flowState) {
        min = this->min.getInt();
        max = this->max.getInt();
    } else {
    min = getMin(widgetCursor, widget->data).getInt();
    max = getMax(widgetCursor, widget->data).getInt();
    }

    if (newValue < min) {
        newValue = min;
    }
    if (newValue > max) {
        newValue = max;
    }

    if (newValue != value) {
        set(widgetCursor, widget->data, newValue);
    }
}

bool UpDownWidgetState::hasOnTouch() {
    return true;
}

void UpDownWidgetState::onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN || touchEvent.type == EVENT_TYPE_AUTO_REPEAT) {
        if (touchEvent.x < widgetCursor.x + widgetCursor.w / 2) {
            upDown(widgetCursor, UP_DOWN_WIDGET_SEGMENT_DOWN_BUTTON);
        } else {
            upDown(widgetCursor, UP_DOWN_WIDGET_SEGMENT_UP_BUTTON);
        }

        sound::playClick();
    }
}

bool UpDownWidgetState::hasOnKeyboard() {
#if OPTION_KEYBOARD
    return true;
#else
    return false;
#endif
}

bool UpDownWidgetState::onKeyboard(const WidgetCursor &widgetCursor, uint8_t key, uint8_t mod) {
#if OPTION_KEYBOARD
    if (mod == 0) {
        if (key == KEY_LEFTARROW || key == KEY_DOWNARROW) {
            upDown(widgetCursor, UP_DOWN_WIDGET_SEGMENT_DOWN_BUTTON);
            sound::playClick();
            return true;
        } else if (key == KEY_RIGHTARROW || key == KEY_UPARROW) {
            upDown(widgetCursor, UP_DOWN_WIDGET_SEGMENT_UP_BUTTON);
            sound::playClick();
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

} // namespace gui
} // namespace eez

#endif // EEZ_OPTION_GUI
