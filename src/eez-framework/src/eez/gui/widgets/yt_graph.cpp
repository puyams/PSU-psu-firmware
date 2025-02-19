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
#include <limits.h>

#include <eez/core/util.h>

#include <eez/gui/gui.h>
#include <eez/gui/widgets/yt_graph.h>

#define CONF_GUI_YT_GRAPH_BLANK_PIXELS_AFTER_CURSOR 10

namespace eez {
namespace gui {

// used for YT_GRAPH_UPDATE_METHOD_SCROLL and YT_GRAPH_UPDATE_METHOD_SCAN_LINE
struct YTGraphDrawHelper {
    const WidgetCursor &widgetCursor;
    const Widget *widget;

    float min[2];
    float max[2];

    uint16_t color16;
    uint16_t dataColor16[2];

    uint32_t numPositions;
    uint32_t position;

    int x;

    int yPrev[2];
    int y[2];

    Value::YtDataGetValueFunctionPointer ytDataGetValue;

    YTGraphDrawHelper(const WidgetCursor &widgetCursor_) : widgetCursor(widgetCursor_), widget(widgetCursor.widget) {
        min[0] = ytDataGetMin(widgetCursor, widget->data, 0).getFloat();
        max[0] = ytDataGetMax(widgetCursor, widget->data, 0).getFloat();

        min[1] = ytDataGetMin(widgetCursor, widget->data, 1).getFloat();
        max[1] = ytDataGetMax(widgetCursor, widget->data, 1).getFloat();

        const Style* y1Style = ytDataGetStyle(widgetCursor, widget->data, 0);
        const Style* y2Style = ytDataGetStyle(widgetCursor, widget->data, 1);
        dataColor16[0] = display::getColor16FromIndex(y1Style->color);
        dataColor16[1] = display::getColor16FromIndex(y2Style->color);

        ytDataGetValue = ytDataGetGetValueFunc(widgetCursor, widget->data);
    }

    int getYValue(int valueIndex, uint32_t position) {
        if (position >= numPositions) {
            return INT_MIN;
        }

        float value = ytDataGetValue(position, valueIndex, nullptr);

        if (isNaN(value)) {
            return INT_MIN;
        }

        int y = (int)round((widgetCursor.h - 1) * (value - min[valueIndex]) / (max[valueIndex] - min[valueIndex]));
        return widgetCursor.h - 1 - y;
    }

    void drawValue(int valueIndex) {
		int yPrevValue = yPrev[valueIndex];
		int yValue = y[valueIndex];

		if (yValue == INT_MIN) {
            return;
        }

		// clipping
		if (yPrevValue >= 0 && yValue < 0) {
            yValue = 0;
        } else if (yPrevValue < 0 && yValue >= 0) {
            yPrevValue = 0;
        } else if (yPrevValue < widgetCursor.h && yValue >= widgetCursor.h) {
            yValue = widgetCursor.h - 1;
        } else if (yPrevValue >= widgetCursor.h && yValue < widgetCursor.h) {
            yPrevValue = widgetCursor.h - 1;
        }

		if (yValue < 0 || yPrevValue < 0 || yValue >= widgetCursor.h || yPrevValue >= widgetCursor.h) {
			return;
		}

        display::setColor16(dataColor16[valueIndex]);

        if (yPrevValue == INT_MIN || abs(yPrevValue - yValue) <= 1) {
            display::drawPixel(x, widgetCursor.y + yValue);
        } else {
            int y1;
            int y2;
            if (yPrevValue < yValue) {
                //display::drawVLine(x, widgetCursor.y + yPrevValue + 1, yValue - yPrevValue - 1);
                y1 = widgetCursor.y + yPrevValue + 1;
                y2 = y1 + yValue - yPrevValue;
            } else {
                //display::drawVLine(x, widgetCursor.y + yValue, yPrevValue - yValue - 1);
                y1 = widgetCursor.y + yValue;
                y2 = y1 + yPrevValue - yValue;
            }
            for (int y = y1; y < y2; y++) {
                display::drawPixel(x, y);
            }
        }
    }

    void drawStep() {
        if (y[0] != INT_MIN && y[1] != INT_MIN && abs(yPrev[0] - y[0]) <= 1 && abs(yPrev[1] - y[1]) <= 1 && y[0] == y[1]) {
			if (y[0] >= 0 && y[0] < widgetCursor.h) {
				display::setColor16(position % 2 ? dataColor16[1] : dataColor16[0]);
				display::drawPixel(x, widgetCursor.y + y[0]);
			}
        } else {
            drawValue(0);
            drawValue(1);
        }
    }

    void drawScanLine(uint32_t startPosition, uint32_t endPosition, uint16_t graphWidth) {
        numPositions = endPosition;

        int x1 = widgetCursor.x + startPosition % graphWidth;
        int x2 = widgetCursor.x + (endPosition - 1) % graphWidth;
        display::setColor16(color16);
        if (x1 <= x2) {
            display::fillRect(x1, widgetCursor.y, x2, widgetCursor.y + widgetCursor.h - 1);
        } else {
            display::fillRect(x1, widgetCursor.y, widgetCursor.x + widgetCursor.w - 1, widgetCursor.y + widgetCursor.h - 1);
            display::fillRect(widgetCursor.x, widgetCursor.y, x2, widgetCursor.y + widgetCursor.h - 1);
        }

        display::startPixelsDraw();
        for (position = startPosition; position < endPosition; ++position) {
            x = widgetCursor.x + position % graphWidth;

            y[0] = getYValue(0, position);
            yPrev[0] = getYValue(0, position == 0 ? position : position - 1);

            y[1] = getYValue(1, position);
            yPrev[1] = getYValue(1, position == 0 ? position : position - 1);

            drawStep();
        }
        display::endPixelsDraw();
    }

    void drawScrolling(uint32_t previousHistoryValuePosition, uint32_t currentHistoryValuePosition, uint32_t numPositions_, uint16_t graphWidth) {
        uint32_t numPointsToDraw = currentHistoryValuePosition - previousHistoryValuePosition;
        if (numPointsToDraw > graphWidth) {
            numPointsToDraw = graphWidth;
        }

        if (numPointsToDraw < graphWidth) {
            display::bitBlt(
                widgetCursor.x + numPointsToDraw,
                widgetCursor.y,
                widgetCursor.x + graphWidth - 1,
                widgetCursor.y + widgetCursor.h - 1,
                widgetCursor.x,
                widgetCursor.y);
        }

        int endX = widgetCursor.x + graphWidth;
        int startX = endX - numPointsToDraw;

        position = previousHistoryValuePosition + 1;

        numPositions = position + numPointsToDraw;

        yPrev[0] = getYValue(0, previousHistoryValuePosition);
        yPrev[1] = getYValue(1, previousHistoryValuePosition);

        display::setColor16(color16);
        display::fillRect(startX, widgetCursor.y, endX - 1, widgetCursor.y + widgetCursor.h - 1);

        display::startPixelsDraw();
        for (x = startX; x < endX; x++, position++) {
            y[0] = getYValue(0, position);
            y[1] = getYValue(1, position);

            drawStep();

            yPrev[0] = y[0];
            yPrev[1] = y[1];
        }
        display::endPixelsDraw();
    }
};

////////////////////////////////////////////////////////////////////////////////

// used for YT_GRAPH_UPDATE_METHOD_STATIC
struct YTGraphStaticDrawHelper {
    YTGraphWidgetState *widgetState;
    const WidgetCursor &widgetCursor;
    const Widget *widget;

    Style* style;

    uint16_t dataColor16;

    uint32_t numPositions;
    uint32_t position;

    float offset;
    float scale;

    int m_valueIndex;

    int x;

    int yPrevMin;
    int yPrevMax;
    int yMin;
    int yMax;

    Value::YtDataGetValueFunctionPointer ytDataGetValue;

    int xLabels[MAX_NUM_OF_Y_VALUES];
    int yLabels[MAX_NUM_OF_Y_VALUES];

    YTGraphStaticDrawHelper(YTGraphWidgetState *widgetState_, const WidgetCursor &widgetCursor_)
        : widgetState(widgetState_), widgetCursor(widgetCursor_), widget(widgetCursor.widget)
    {
        ytDataGetValue = ytDataGetGetValueFunc(widgetCursor, widget->data);
    }

    void getYValue(uint32_t position, int &min, int &max) {
        if (position >= numPositions) {
            max = INT_MIN;
            min = INT_MIN;
        } else {
            float fMax;
            float fMin = ytDataGetValue(position, m_valueIndex, &fMax);

            if (isNaN(fMin)) {
                max = INT_MIN;
            } else {
                max = widgetCursor.h - 1 - (int)floor(widgetCursor.h / 2.0f + (fMin + offset) * scale);
            }

            if (isNaN(fMax)) {
                min = INT_MIN;
            } else {
                min = widgetCursor.h - 1 - (int)floor(widgetCursor.h / 2.0f + (fMax + offset) * scale);
            }
        }
    }

    void drawValue() {
        if (yMin == INT_MIN) {
            return;
        }

        display::setColor16(dataColor16);

        int yFrom;
        int yTo;

        if (yPrevMax == INT_MIN) {
            yFrom = yMin;
            yTo = yMax;
        } else {
            if (yPrevMax < yMin) {
                yFrom = yPrevMax + 1;
                yTo = yMax;
            } else if (yMax < yPrevMin) {
                yFrom = yMin;
                yTo = yPrevMin - 1;
            } else {
                yFrom = yMin;
                yTo = yMax;
            }
        }

        if ((yFrom < 0 && yTo < 0) || (yFrom >= widgetCursor.h && yTo >= widgetCursor.h)) {
            return;
        }

        if (yFrom < 0) {
            yFrom = 0;
        }

        if (yTo >= widgetCursor.h) {
            yTo = widgetCursor.h - 1;
        }

        if (yFrom == yTo) {
            display::drawPixel(x, widgetCursor.y + yFrom);
        } else {
            // display::drawVLine(x, widgetCursor.y + yFrom, yTo - yFrom);
            int y1 = widgetCursor.y + yFrom;
            int y2 = y1 + yTo - yFrom + 1;
            for (int y = y1; y < y2; y++) {
                display::drawPixel(x, y);
            }
        }
    }

    void getMinMax(int *yLabels, int n, int &yMin, int &yMax) {
        yMin = INT_MAX;
        yMax = INT_MIN;
        for (int i = 0; i < n; i++) {
            if (yLabels[i] < yMin) {
                yMin = yLabels[i];
            }
            if (yLabels[i] > yMax) {
                yMax = yLabels[i];
            }
        }
    }

    void repositionLabels(int labelHeight) {
        for (int valueIndex = 0; valueIndex < MAX_NUM_OF_Y_VALUES; valueIndex++) {
            if (yLabels[valueIndex] != INT_MIN) {
                yLabels[valueIndex] -= labelHeight / 2;
                if (yLabels[valueIndex] < widgetCursor.y) {
                    yLabels[valueIndex] = widgetCursor.y;
                } else if (yLabels[valueIndex] > widgetCursor.y + widgetCursor.h - labelHeight) {
                    yLabels[valueIndex] = widgetCursor.y + widgetCursor.h - labelHeight;
                }
            }
        }
    }

    void drawGraph(uint32_t currentHistoryValuePosition, int startX, int endX, int vertDivisions) {
        //xLabels[m_valueIndex] = INT_MIN;
        yLabels[m_valueIndex] = INT_MIN;

        if (ytDataDataValueIsVisible(widgetCursor, widget->data, m_valueIndex)) {
            position = currentHistoryValuePosition;

            scale = (widgetCursor.h - 1) / widgetState->valueDiv[m_valueIndex] / vertDivisions;
            offset = widgetState->valueOffset[m_valueIndex];

            const Style* style = ytDataGetStyle(widgetCursor, widget->data, m_valueIndex);
            dataColor16 = display::getColor16FromIndex(style->color);

            getYValue(position > 0 ? position - 1 : 0, yPrevMin, yPrevMax);

            for (x = startX; x < endX; x++, position++) {
                getYValue(position, yMin, yMax);
                drawValue();
                yPrevMin = yMin;
                yPrevMax = yMax;

                if (yMin != INT_MIN) {
                    //xLabels[m_valueIndex] = x;
                    yLabels[m_valueIndex] = widgetCursor.y + yMin;
                }
            }
        }

		xLabels[m_valueIndex] = endX;
    }

    void drawLabel(font::Font &font, bool transparent) {
        if (yLabels[m_valueIndex] != INT_MIN) {
            const Style *labelStyle = ytDataGetStyle(widgetCursor, widget->data, m_valueIndex);

            char labelText[64];
            ytDataGetLabel(widgetCursor, widget->data, m_valueIndex, labelText, sizeof(labelText));
            int labelWidth = display::measureStr(labelText, -1, font, widgetCursor.w);

            int xLabel = xLabels[m_valueIndex];
            if (xLabel < widgetCursor.x) {
                xLabel = widgetCursor.x;
            } else if (xLabel > widgetCursor.x + widgetCursor.w - labelWidth) {
                xLabel = widgetCursor.x + widgetCursor.w - labelWidth;
            }

            if (!transparent) {
                display::setColor(labelStyle->backgroundColor);
                display::fillRect(xLabel, yLabels[m_valueIndex], xLabel + labelWidth - 1, yLabels[m_valueIndex] + font.getHeight() - 1);
            }

            display::setColor(labelStyle->color);
            display::drawStr(labelText, -1, xLabel, yLabels[m_valueIndex], widgetCursor.x, widgetCursor.y, widgetCursor.x + widgetCursor.w - 1, widgetCursor.y + widgetCursor.h - 1, font, -1);
        }
    }

    void drawStatic(uint32_t previousHistoryValuePosition, uint32_t currentHistoryValuePosition, uint32_t numPositions_, uint16_t graphWidth, bool showLabels, int selectedValueIndex) {
        // draw background
        const Style* style = getStyle(widget->style);
        display::setColor(style->backgroundColor);
        display::fillRect(widgetCursor.x, widgetCursor.y, widgetCursor.x + (int)widgetCursor.w - 1, widgetCursor.y + (int)widgetCursor.h - 1);

        numPositions = numPositions_;

        int startX = widgetCursor.x;
        int endX = startX + graphWidth;

        int horzDivisions = ytDataGetHorzDivisions(widgetCursor, widget->data);
        int vertDivisions = ytDataGetVertDivisions(widgetCursor, widget->data);

        // draw grid
        display::setColor(style->borderColor);
        for (int x = 1; x < horzDivisions; x++) {
            display::drawVLine(widgetCursor.x + x * widgetCursor.w / horzDivisions, widgetCursor.y, widgetCursor.h - 1);
        }
        for (int y = 1; y < vertDivisions; y++) {
            display::drawHLine(widgetCursor.x, widgetCursor.y + y * widgetCursor.h / vertDivisions, widgetCursor.w - 1);
        }

		// draw bookmarks
		if (widgetState->bookmarks) {
			for (int x = 0; x < widgetCursor.w; x++) {
				if (widgetState->bookmarks[x]) {
					display::setColor(COLOR_ID_BOOKMARK);
					display::drawVLine(startX + x, widgetCursor.y, widgetCursor.h - 1);
				}
			}
		}

        // draw graphs
        display::startPixelsDraw();
        for (m_valueIndex = 0; m_valueIndex < MAX_NUM_OF_Y_VALUES; m_valueIndex++) {
            if (m_valueIndex != selectedValueIndex) {
                drawGraph(currentHistoryValuePosition, startX, endX, vertDivisions);
            }
        }
        if (selectedValueIndex != -1) {
            m_valueIndex = selectedValueIndex;
            drawGraph(currentHistoryValuePosition, startX, endX, vertDivisions);
        }
        display::endPixelsDraw();

		// draw cursor
		if (ytDataIsCursorVisible(widgetCursor, widgetCursor.widget->data)) {
			display::setColor(style->color);
			display::drawVLine(startX + widgetState->cursorPosition - currentHistoryValuePosition, widgetCursor.y, widgetCursor.h - 1);

			char text[64];
			ytDataGetCursorXValue(widgetCursor, widgetCursor.widget->data).toText(text, sizeof(text));

			font::Font font = styleGetFont(style);
			int MIN_CURSOR_TEXT_WIDTH = 80;
			int cursorTextWidth = MAX(display::measureStr(text, -1, font), MIN_CURSOR_TEXT_WIDTH);
			int cursorTextHeight = font.getHeight();
			const int PADDING = 0;
			int xCursorText = widgetCursor.x + widgetState->cursorPosition - currentHistoryValuePosition - cursorTextWidth / 2;
			if (xCursorText < widgetCursor.x + PADDING) {
				xCursorText = widgetCursor.x + PADDING;
			} else if (xCursorText + cursorTextWidth > widgetCursor.x + widgetCursor.w - PADDING) {
				xCursorText = widgetCursor.x + widgetCursor.w - PADDING - cursorTextWidth;
			}
			int yCursorText = widgetCursor.y + widgetCursor.h - cursorTextHeight - PADDING;

			drawText(
                text, -1,
				xCursorText, yCursorText, cursorTextWidth, cursorTextHeight,
				style,
                widgetState->flags.focused
			);
		}

		// draw labels
        if (showLabels) {
            font::Font font = styleGetFont(style);
            int labelHeight = font.getHeight();

            repositionLabels(labelHeight);

            for (m_valueIndex = 0; m_valueIndex < MAX_NUM_OF_Y_VALUES; m_valueIndex++) {
                if (m_valueIndex != selectedValueIndex) {
                    drawLabel(font, true);
                }
            }

            if (selectedValueIndex != -1) {
                m_valueIndex = selectedValueIndex;
                drawLabel(font, false);
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

bool YTGraphWidgetState::updateState() {
    WIDGET_STATE_START(YTGraphWidget);

    refreshBackground = !hasPreviousState;

    WIDGET_STATE(flags.focused, isFocusWidget(widgetCursor));

    WIDGET_STATE(refreshCounter, ytDataGetRefreshCounter(widgetCursor, widget->data));
    WIDGET_STATE(iChannel, widgetCursor.cursor);
    WIDGET_STATE(ytGraphUpdateMethod, ytDataGetGraphUpdateMethod(widgetCursor, widget->data));

	auto new_historyValuePosition = ytDataGetPosition(widgetCursor, widget->data);

    if (hasPreviousState) {
    	previousHistoryValuePosition = historyValuePosition;
    } else {
        uint16_t graphWidth = (uint16_t)widgetCursor.w;
    	previousHistoryValuePosition = new_historyValuePosition - graphWidth;
        hasPreviousState = false;
    }

    WIDGET_STATE(historyValuePosition, new_historyValuePosition);
    WIDGET_STATE(cursorPosition, historyValuePosition + ytDataGetCursorOffset(widgetCursor, widget->data));

    WIDGET_STATE(numHistoryValues, ytDataGetSize(widgetCursor, widget->data));
    WIDGET_STATE(bookmarks, ytDataGetBookmarks(widgetCursor, widget->data));
    WIDGET_STATE(showLabels, ytDataGetShowLabels(widgetCursor, widget->data));
    WIDGET_STATE(selectedValueIndex, ytDataGetSelectedValueIndex(widgetCursor, widget->data));

    if (ytGraphUpdateMethod == YT_GRAPH_UPDATE_METHOD_STATIC) {
        for (int valueIndex = 0; valueIndex < MAX_NUM_OF_Y_VALUES; valueIndex++) {
            WIDGET_STATE(valueIsVisible[valueIndex], ytDataDataValueIsVisible(widgetCursor, widget->data, valueIndex));
            WIDGET_STATE(valueDiv[valueIndex], ytDataGetDiv(widgetCursor, widget->data, valueIndex));
            WIDGET_STATE(valueOffset[valueIndex], ytDataGetOffset(widgetCursor, widget->data, valueIndex));
        }
    }

    WIDGET_STATE_END()
}

void YTGraphWidgetState::render() {
    const WidgetCursor &widgetCursor = g_widgetCursor;

    auto widget = (const YTGraphWidget *)widgetCursor.widget;

    uint16_t graphWidth = (uint16_t)widgetCursor.w;

    if (ytGraphUpdateMethod == YT_GRAPH_UPDATE_METHOD_STATIC) {
        YTGraphStaticDrawHelper drawHelper(this, widgetCursor);
        drawHelper.drawStatic(previousHistoryValuePosition, historyValuePosition, numHistoryValues, graphWidth, showLabels, selectedValueIndex);
    } else {
        const Style* style = getStyle(widget->style);

        if (refreshBackground) {
            display::setColor(style->backgroundColor);
            display::fillRect(widgetCursor.x, widgetCursor.y, widgetCursor.x + (int)widgetCursor.w - 1, widgetCursor.y + (int)widgetCursor.h - 1);
        }

        YTGraphDrawHelper drawHelper(widgetCursor);
        drawHelper.color16 = display::getColor16FromIndex(flags.active ? style->color : style->backgroundColor);
        if (ytGraphUpdateMethod == YT_GRAPH_UPDATE_METHOD_SCAN_LINE) {
            drawHelper.drawScanLine(previousHistoryValuePosition, historyValuePosition, graphWidth);

            int x = widgetCursor.x;

            // draw cursor
            display::setColor(style->color);
            display::drawVLine(x + historyValuePosition % graphWidth, widgetCursor.y, (int)widgetCursor.h - 1);
            display::drawVLine(x + (historyValuePosition + 1) % graphWidth, widgetCursor.y, (int)widgetCursor.h - 1);

            // draw blank lines
            int x1 = x + (historyValuePosition + 2) % graphWidth;
            int x2 = x + (historyValuePosition + CONF_GUI_YT_GRAPH_BLANK_PIXELS_AFTER_CURSOR) % graphWidth;

            display::setColor(style->backgroundColor);
            if (x1 < x2) {
                display::fillRect(x1, widgetCursor.y, x2, widgetCursor.y + (int)widgetCursor.h - 1);
            } else {
                display::fillRect(x1, widgetCursor.y, x + graphWidth - 1, widgetCursor.y + (int)widgetCursor.h - 1);
                display::fillRect(x, widgetCursor.y, x2, widgetCursor.y + (int)widgetCursor.h - 1);
            }
        } else if (ytGraphUpdateMethod == YT_GRAPH_UPDATE_METHOD_SCROLL) {
            drawHelper.drawScrolling(previousHistoryValuePosition, historyValuePosition, numHistoryValues, graphWidth);
        }
    }
}

bool YTGraphWidgetState::hasOnTouch() {
    return true;
}

void YTGraphWidgetState::onTouch(const WidgetCursor &widgetCursor, Event &touchEvent) {
    if (ytDataGetGraphUpdateMethod(widgetCursor, widgetCursor.widget->data) == YT_GRAPH_UPDATE_METHOD_STATIC) {
        if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN || touchEvent.type == EVENT_TYPE_TOUCH_MOVE) {
			TouchDrag touchDrag = {
				widgetCursor,
				touchEvent.type,
				touchEvent.x - widgetCursor.x,
				touchEvent.y - widgetCursor.y
			};
            ytDataTouchDrag(widgetCursor, widgetCursor.widget->data, &touchDrag);
        }
    } else {
        if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
            if (widgetCursor.appContext->isWidgetActionEnabled(widgetCursor)) {
                auto action = getWidgetAction(widgetCursor);
                executeAction(widgetCursor, action);
            }
        }
    }
}

} // namespace gui
} // namespace eez

#endif // EEZ_OPTION_GUI
