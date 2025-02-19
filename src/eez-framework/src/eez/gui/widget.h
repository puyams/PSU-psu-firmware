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

#pragma once

#include <eez/core/memory.h>

#include <eez/gui/geometry.h>
#include <eez/gui/event.h>

namespace eez {

struct Assets;

template<typename T>
struct ListOfAssetsPtr;

namespace flow {
	struct FlowState;
}

namespace gui {

#define WIDGET_TYPES \
    WIDGET_TYPE(None,          NONE,            0) \
    WIDGET_TYPE(Container,     CONTAINER,       1) \
    WIDGET_TYPE(List,          LIST,            2) \
    WIDGET_TYPE(Grid,          GRID,            3) \
    WIDGET_TYPE(Select,        SELECT,          4) \
    WIDGET_TYPE(DisplayData,   DISPLAY_DATA,    5) \
    WIDGET_TYPE(Text,          TEXT,            6) \
    WIDGET_TYPE(MultilineText, MULTILINE_TEXT,  7) \
    WIDGET_TYPE(Rectangle,     RECTANGLE,       8) \
    WIDGET_TYPE(Bitmap,        BITMAP,          9) \
    WIDGET_TYPE(Button,        BUTTON,         10) \
    WIDGET_TYPE(ToggleButton,  TOGGLE_BUTTON,  11) \
    WIDGET_TYPE(ButtonGroup,   BUTTON_GROUP,   12) \
    WIDGET_TYPE(Reserved,      RESERVED,       13) \
    WIDGET_TYPE(BarGraph,      BAR_GRAPH,      14) \
    WIDGET_TYPE(UserWidget,    USER_WIDGET,    15) \
    WIDGET_TYPE(YTGraph,       YT_GRAPH,       16) \
    WIDGET_TYPE(UpDown,        UP_DOWN,        17) \
    WIDGET_TYPE(ListGraph,     LIST_GRAPH,     18) \
    WIDGET_TYPE(AppView,       APP_VIEW,       19) \
    WIDGET_TYPE(ScrollBar,     SCROLL_BAR,     20) \
    WIDGET_TYPE(Progress,      PROGRESS,       21) \
    WIDGET_TYPE(Canvas,        CANVAS,         22) \
    WIDGET_TYPE(Gauge,         GAUGE,          23) \
    WIDGET_TYPE(Input,         INPUT,          24) \
	WIDGET_TYPE(Roller,        ROLLER,         25) \
	WIDGET_TYPE(Switch,        SWITCH,         26) \
	WIDGET_TYPE(Slider,        SLIDER,         27) \
	WIDGET_TYPE(DropDownList,  DROP_DOWN_LIST, 28) \
    WIDGET_TYPE(LineChart,     LINE_CHART,     29) \
    WIDGET_TYPE(QRCode,        QR_CODE,        30) \

#define WIDGET_TYPE(NAME_PASCAL_CASE, NAME, ID) WIDGET_TYPE_##NAME = ID,
enum WidgetTypes {
	WIDGET_TYPES
};
#undef WIDGET_TYPE

struct Widget;

////////////////////////////////////////////////////////////////////////////////

struct WidgetState;
class AppContext;
typedef int Cursor;

struct BackgroundStyle {
	int x;
	int y;
	const Style *style;
	bool active;
};

static const size_t BACKGROUND_STYLE_STACK_SIZE = 10;

struct WidgetCursor {
	Assets *assets;
	AppContext *appContext;
    const Widget *widget;
    Cursor cursor;
	int32_t iterators[MAX_ITERATORS];
    flow::FlowState *flowState;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t opacity;

	WidgetState *currentState;
	bool refreshed;
	bool hasPreviousState;

	BackgroundStyle backgroundStyleStack[BACKGROUND_STYLE_STACK_SIZE];
	size_t backgroundStyleStackPointer;

	WidgetCursor()
		: assets(nullptr)
		, appContext(nullptr)
		, widget(nullptr)
		, cursor(-1)
		, flowState(nullptr)
		, x(0)
		, y(0)
        , opacity(255)
		, currentState(nullptr)
		, refreshed(true)
		, hasPreviousState(false)
		, backgroundStyleStackPointer(0)
	{
		iterators[0] = -1; iterators[1] = -1; iterators[2] = -1; iterators[3] = -1;
	}

    WidgetCursor(
		Assets *assets_,
		AppContext *appContext_,
		const Widget *widget_,
		int16_t x_,
		int16_t y_,
		WidgetState *currentState_,
		bool refreshed_,
		bool hasPreviousState_
    )
		: assets(assets_)
		, appContext(appContext_)
		, widget(widget_)
		, cursor(-1)
		, flowState(nullptr)
		, x(x_)
		, y(y_)
        , opacity(255)
		, currentState(currentState_)
		, refreshed(refreshed_)
		, hasPreviousState(hasPreviousState_)
		, backgroundStyleStackPointer(0)
    {
		iterators[0] = -1; iterators[1] = -1; iterators[2] = -1; iterators[3] = -1;
	}

	WidgetCursor(Cursor cursor_)
		: assets(nullptr)
		, appContext(nullptr)
		, widget(nullptr)
		, cursor(cursor_)
		, flowState(nullptr)
		, x(0)
		, y(0)
        , opacity(255)
		, currentState(nullptr)
		, refreshed(true)
		, hasPreviousState(false)
		, backgroundStyleStackPointer(0)
	{
		iterators[0] = -1; iterators[1] = -1; iterators[2] = -1; iterators[3] = -1;
	}

    bool operator!=(const WidgetCursor &rhs) const {
		if (widget != rhs.widget || cursor != rhs.cursor) {
			return true;
		}
		if (iterators[0] != rhs.iterators[0] || iterators[1] != rhs.iterators[1] || iterators[2] != rhs.iterators[2] || iterators[3] != rhs.iterators[3]) {
			return true;
		}
		return false;
	}

    bool operator==(const WidgetCursor &rhs) const {
        return !(*this != rhs);
    }

    explicit operator bool() const {
        return widget != nullptr;
    }

	void pushIterator(int32_t it) {
		iterators[3] = iterators[2]; iterators[2] = iterators[1]; iterators[1] = iterators[0]; iterators[0] = it;
	}

	void popIterator() {
		iterators[0] = iterators[1]; iterators[1] = iterators[2]; iterators[2] = iterators[3]; iterators[3] = -1;
	}

	bool isPage() const;

	void pushBackground(int x, int y, const Style *style, bool active);
	void popBackground();
};

struct WidgetStateFlags {
    unsigned active : 1;
    unsigned focused : 1;
    unsigned blinking : 1;
    unsigned enabled : 1;
};

struct WidgetState {
	uint16_t type;
    int x;
    int y;
    int w;
    int h;
    Value isVisible;

	virtual ~WidgetState() {}

	virtual bool updateState();
	virtual void render();
	virtual void enumChildren();

	virtual bool hasOnTouch();
	virtual void onTouch(const WidgetCursor &widgetCursor, Event &touchEvent);

	virtual bool hasOnKeyboard();
	virtual bool onKeyboard(const WidgetCursor &widgetCursor, uint8_t key, uint8_t mod);
};

#define WIDGET_STATE_START(WidgetClass) \
    const WidgetCursor &widgetCursor = g_widgetCursor; \
	auto widget = (const WidgetClass *)widgetCursor.widget; \
	bool hasPreviousState = widgetCursor.hasPreviousState; \
	if (widget->visible) { \
    	WIDGET_STATE(isVisible, get(widgetCursor, widget->visible)); \
	} \
    if (x != widgetCursor.x || y != widgetCursor.y || w != widgetCursor.w || h != widgetCursor.h) { \
        x = widgetCursor.x; \
        y = widgetCursor.y; \
        w = widgetCursor.w; \
        h = widgetCursor.h; \
        hasPreviousState = false; \
    }

#define WIDGET_STATE(A, B) \
	if (hasPreviousState) { \
		auto temp = B; \
		if (A != temp) { \
			hasPreviousState = false; \
			A = temp; \
		} \
	} else { \
		A = B; \
	}

#define WIDGET_STATE_END() \
    return !hasPreviousState;

////////////////////////////////////////////////////////////////////////////////

extern bool g_isActiveWidget;
extern bool g_isRTL;

void enumRootWidget();
void enumWidget();
void enumNoneWidget();

extern bool g_foundWidgetAtDownInvalid;
void freeWidgetStates(WidgetState *topWidgetState);

typedef void (*EnumWidgetsCallback)();
extern EnumWidgetsCallback g_findCallback;
void forEachWidget(EnumWidgetsCallback callback);

WidgetCursor findWidget(int16_t x, int16_t y, bool clicked = true);

typedef void (*OnTouchFunctionType)(const WidgetCursor &widgetCursor, Event &touchEvent);
OnTouchFunctionType getWidgetTouchFunction(const WidgetCursor &widgetCursor);

void resizeWidget(
    const Widget *widget,
    Rect &widgetRect,
    int containerOriginalWidth,
    int containerOriginalHeight,
    int containerWidth,
    int containerHeight
);

void applyTimeline(
    WidgetCursor& widgetCursor,
    Rect &widgetRect
);

void doStaticLayout(
    WidgetCursor& widgetCursor,
    const ListOfAssetsPtr<Widget> &widgets,
    int containerOriginalWidth,
    int containerOriginalHeight,
    int containerWidth,
    int containerHeight
);

} // namespace gui
} // namespace eez
