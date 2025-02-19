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

#include <math.h>

#include <eez/core/util.h>
#include <eez/core/sound.h>

#if OPTION_KEYBOARD
#include <eez/core/keyboard.h>
#endif

#include <eez/gui/gui.h>
#include <eez/gui/widgets/scroll_bar.h>

namespace eez {
namespace gui {

ScrollBarWidgetSegment dragSegment;
int dragStartX;
int dragStartPosition;

int getSize(const WidgetCursor &widgetCursor) {
    return ytDataGetSize(widgetCursor, widgetCursor.widget->data);
}

int getPosition(const WidgetCursor &widgetCursor) {
    return ytDataGetPosition(widgetCursor, widgetCursor.widget->data);
}

int getPositionIncrement(const WidgetCursor &widgetCursor) {
    return ytDataGetPositionIncrement(widgetCursor, widgetCursor.widget->data);
}

int getPageSize(const WidgetCursor &widgetCursor) {
    return ytDataGetPageSize(widgetCursor, widgetCursor.widget->data);
}

void setPosition(const WidgetCursor &widgetCursor, int position) {
    ytDataSetPosition(widgetCursor, widgetCursor.widget->data, position < 0 ? 0 : position);
}

void getThumbGeometry(int size, int position, int pageSize, int xTrack, int wTrack, int minThumbWidth, int &xThumb, int &widthThumb) {
    widthThumb = (int)round(1.0 * pageSize * wTrack / size);
    if (widthThumb < minThumbWidth) {
		widthThumb = minThumbWidth;
    }
    xThumb = xTrack + (int)round(remap(position, 0, 0, size - pageSize, wTrack - widthThumb));
}

bool ScrollBarWidgetState::updateState() {
    WIDGET_STATE_START(Widget);

    WIDGET_STATE(flags.active, g_isActiveWidget);
    WIDGET_STATE(flags.focused, isFocusWidget(widgetCursor));
    WIDGET_STATE(size, getSize(widgetCursor));
    WIDGET_STATE(position, getPosition(widgetCursor));
    WIDGET_STATE(pageSize, getPageSize(widgetCursor));

    if (!widget->visible) {
        WIDGET_STATE(isVisible, Value(size > pageSize, VALUE_TYPE_BOOLEAN));
    }

    WIDGET_STATE_END()
}

void ScrollBarWidgetState::render() {
    const WidgetCursor &widgetCursor = g_widgetCursor;

    auto widget = (const ScrollBarWidget *)widgetCursor.widget;
    if (pageSize < size) {
        const Style *buttonsStyle = getStyle(widget->buttonsStyle);
        auto isHorizontal = widgetCursor.w > widgetCursor.h;

        int buttonSize = isHorizontal ? widgetCursor.h : widgetCursor.w;

        // draw left button
        if (widget->leftButtonText) {
            drawText(
                static_cast<const char *>(widget->leftButtonText), -1,
                widgetCursor.x,
                widgetCursor.y,
                isHorizontal ? buttonSize : widgetCursor.w,
                isHorizontal ? widgetCursor.h : buttonSize, buttonsStyle,
                flags.active && dragSegment == SCROLL_BAR_WIDGET_SEGMENT_LEFT_BUTTON
            );
        }

        // draw track
        int xTrack;
        int yTrack;
        int wTrack;
        int hTrack;

        if (isHorizontal) {
            xTrack = widgetCursor.x + buttonSize;
            yTrack = widgetCursor.y;
            wTrack = widgetCursor.w - 2 * buttonSize;
            hTrack = widgetCursor.h;
        } else {
            xTrack = widgetCursor.x;
            yTrack = widgetCursor.y + buttonSize;
            wTrack = widgetCursor.w;
            hTrack = widgetCursor.h - 2 * buttonSize;
        }

        const Style *trackStyle = getStyle(widget->style);
        drawRectangle(xTrack, yTrack, wTrack, hTrack, trackStyle, false, false, false);

        // draw thumb
        const Style *thumbStyle = getStyle(widget->thumbStyle);
        if (isHorizontal) {
            int xThumb, wThumb;
            getThumbGeometry(size, position, pageSize, xTrack, wTrack, buttonSize, xThumb, wThumb);
            drawRectangle(xThumb, yTrack, wThumb, hTrack, thumbStyle, false, false, false);
        } else {
            int yThumb, hThumb;
            getThumbGeometry(size, position, pageSize, yTrack, hTrack, buttonSize, yThumb, hThumb);
            drawRectangle(xTrack, yThumb, wTrack, hThumb, thumbStyle, false, false, false);
        }

        // draw right button
        if (widget->rightButtonText) {
            drawText(
                static_cast<const char *>(widget->rightButtonText), -1,
                isHorizontal ? widgetCursor.x + widgetCursor.w - buttonSize : widgetCursor.x,
                isHorizontal ? widgetCursor.y : widgetCursor.y + widgetCursor.h - buttonSize,
                isHorizontal ? buttonSize : widgetCursor.w,
                isHorizontal ? widgetCursor.h : buttonSize,
                buttonsStyle,
                flags.active && dragSegment == SCROLL_BAR_WIDGET_SEGMENT_RIGHT_BUTTON
            );
        }

        auto action = getWidgetAction(widgetCursor);
        if (flags.focused && action == ACTION_ID_SCROLL) {
            const Style *style = getStyle(widgetCursor.widget->style);
            display::setColor(style->focusColor);
            display::drawRect(widgetCursor.x, widgetCursor.y, widgetCursor.x + widgetCursor.w - 1, widgetCursor.y + widgetCursor.h - 1);
            display::drawRect(widgetCursor.x + 1, widgetCursor.y + 1, widgetCursor.x + widgetCursor.w - 2, widgetCursor.y + widgetCursor.h - 2);
        }
    } else {
        // scroll bar is hidden
        const Style *trackStyle = getStyle(widget->style);
        drawRectangle(widgetCursor.x, widgetCursor.y, widgetCursor.w, widgetCursor.h, trackStyle, false, false, false);
    }
}

bool ScrollBarWidgetState::hasOnTouch() {
    return true;
}

void ScrollBarWidgetState::onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    int size = getSize(widgetCursor);
    int pageSize = getPageSize(widgetCursor);

    if (size > pageSize) {
        const Widget *widget = widgetCursor.widget;

        auto isHorizontal = widgetCursor.w > widgetCursor.h;
        int buttonSize = isHorizontal ? widgetCursor.h : widgetCursor.w;

        int xTrack;
        int wTrack;
        int x;

        if (isHorizontal) {
            x = touchEvent.x;
            xTrack = widgetCursor.x + buttonSize;
            wTrack = widgetCursor.w - 2 * buttonSize;
        } else {
            x = touchEvent.y;
            xTrack = widgetCursor.y + buttonSize;
            wTrack = widgetCursor.h - 2 * buttonSize;
        }

        if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN || touchEvent.type == EVENT_TYPE_AUTO_REPEAT || dragSegment == SCROLL_BAR_WIDGET_SEGMENT_UNINITIALIZED) {
            if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
                dragSegment = SCROLL_BAR_WIDGET_SEGMENT_NONE;
            }

            if (touchEvent.type == EVENT_TYPE_AUTO_REPEAT && (dragSegment == SCROLL_BAR_WIDGET_SEGMENT_NONE || dragSegment == SCROLL_BAR_WIDGET_SEGMENT_THUMB)) {
                return;
            }

            if ((isHorizontal && touchEvent.x < widgetCursor.x + buttonSize) || (!isHorizontal && touchEvent.y < widgetCursor.y + buttonSize)) {
                setPosition(widgetCursor, getPosition(widgetCursor) - getPositionIncrement(widgetCursor));
                dragSegment = SCROLL_BAR_WIDGET_SEGMENT_LEFT_BUTTON;
                if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
                    sound::playClick();
                }
            } else if ((isHorizontal && (touchEvent.x >= widgetCursor.x + widgetCursor.w - buttonSize)) || (!isHorizontal && (touchEvent.y >= widgetCursor.y + widgetCursor.h - buttonSize))) {
                setPosition(widgetCursor, getPosition(widgetCursor) + getPositionIncrement(widgetCursor));
                dragSegment = SCROLL_BAR_WIDGET_SEGMENT_RIGHT_BUTTON;
                if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
                    sound::playClick();
                }
            } else {
                int xThumb, wThumb;

                int position = getPosition(widgetCursor);

                getThumbGeometry(size, position, pageSize, xTrack, wTrack, buttonSize, xThumb, wThumb);

                if (x < xThumb) {
                    setPosition(widgetCursor, getPosition(widgetCursor) - pageSize);
                    dragSegment = SCROLL_BAR_WIDGET_SEGMENT_TRACK_LEFT;
                    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
                        sound::playClick();
                    }
                } else if (x >= xThumb + wThumb) {
                    setPosition(widgetCursor, getPosition(widgetCursor) + pageSize);
                    dragSegment = SCROLL_BAR_WIDGET_SEGMENT_TRACK_RIGHT;
                    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
                        sound::playClick();
                    }
                } else if (x >= xThumb || touchEvent.x < xThumb + wThumb) {
                    dragSegment = SCROLL_BAR_WIDGET_SEGMENT_THUMB;
                    dragStartX = x;
                    dragStartPosition = getPosition(widgetCursor);
                }
            }
        }

        if (touchEvent.type == EVENT_TYPE_TOUCH_MOVE) {
            if (dragSegment == SCROLL_BAR_WIDGET_SEGMENT_THUMB) {
                int size = getSize(widgetCursor);
                int position = dragStartPosition + (int)round(1.0 * (x - dragStartX) * size / wTrack);
                int positionIncrement = getPositionIncrement(widgetCursor);
                setPosition(widgetCursor, (int)(roundf(1.0f * position / positionIncrement) * positionIncrement));
            }
        } else if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
            dragSegment = SCROLL_BAR_WIDGET_SEGMENT_NONE;
        }

        auto action = getWidgetAction(widgetCursor);
		if (action == ACTION_ID_SCROLL) {
			g_hooks.setFocusCursor(widgetCursor, widget->data);
		}
    }
}

bool ScrollBarWidgetState::hasOnKeyboard() {
#if OPTION_KEYBOARD
    return true;
#else
    return false;
#endif
}

bool ScrollBarWidgetState::onKeyboard(const WidgetCursor &widgetCursor, uint8_t key, uint8_t mod) {
#if OPTION_KEYBOARD
    if (mod == 0) {
        int position = getPosition(widgetCursor);
        int increment = getPositionIncrement(widgetCursor);
        int size = getSize(widgetCursor);
        int pageSize = getPageSize(widgetCursor);

        if (key == KEY_LEFTARROW || key == KEY_UPARROW) {
            setPosition(widgetCursor, position - increment);
            return true;
        } else if (key == KEY_RIGHTARROW || key == KEY_DOWNARROW) {
            setPosition(widgetCursor, position + increment);
            return true;
        } else if (key == KEY_PAGEUP) {
            setPosition(widgetCursor, position - pageSize);
            return true;
        } else if (key == KEY_PAGEDOWN) {
            setPosition(widgetCursor, position + pageSize);
            return true;
        } else if (key == KEY_HOME) {
            setPosition(widgetCursor, 0);
            return true;
        } else if (key == KEY_END1) {
            setPosition(widgetCursor, position + size - pageSize);
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
