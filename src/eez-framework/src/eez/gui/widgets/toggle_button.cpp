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

#include <eez/core/sound.h>
#include <eez/core/util.h>

#include <eez/gui/gui.h>
#include <eez/gui/widgets/toggle_button.h>

namespace eez {
namespace gui {

bool ToggleButtonWidgetState::updateState() {
    WIDGET_STATE_START(ToggleButtonWidget);

    WIDGET_STATE(flags.active, g_isActiveWidget);
    WIDGET_STATE(flags.enabled, get(widgetCursor, widget->data).getInt() ? 1 : 0);

    WIDGET_STATE_END()
}

void ToggleButtonWidgetState::render() {
    const WidgetCursor &widgetCursor = g_widgetCursor;

    auto widget = (const ToggleButtonWidget *)widgetCursor.widget;
    const Style* style = getStyle(widget->style);
    const Style* checkedStyle = getStyle(widget->checkedStyle);

    auto &text = flags.enabled ? widget->text2 : widget->text1;
    drawText(
        text ? static_cast<const char *>(text) : "",
        -1,
        widgetCursor.x, widgetCursor.y, widgetCursor.w, widgetCursor.h,
        flags.enabled ? checkedStyle : style,
        flags.active
    );
}

bool ToggleButtonWidgetState::hasOnTouch() {
    return true;
}

void ToggleButtonWidgetState::onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
	if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
        set(widgetCursor, widgetCursor.widget->data, get(widgetCursor, widgetCursor.widget->data).getInt() ? 0 : 1);

        if (widgetCursor.widget->action != ACTION_ID_NONE) {
            executeAction(widgetCursor, widgetCursor.widget->action);
        } else {
            sound::playClick();
        }
	}
}

} // namespace gui
} // namespace eez

#endif // EEZ_OPTION_GUI
