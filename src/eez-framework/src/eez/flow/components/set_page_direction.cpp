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

#include <eez/conf-internal.h>

#if EEZ_OPTION_GUI

#include <eez/flow/components.h>
#include <eez/flow/flow_defs_v3.h>
#include <eez/flow/expression.h>
#include <eez/flow/private.h>

#include <eez/gui/gui.h>
using namespace eez::gui;

namespace eez {
namespace flow {

#define PAGE_DIRECTION_LTR 0
#define PAGE_DIRECTION_RTL 1

struct SetPageDirectionComponent : public Component {
	uint8_t direction;
};

void executeSetPageDirectionComponent(FlowState *flowState, unsigned componentIndex) {
	auto component = (SetPageDirectionComponent *)flowState->flow->components[componentIndex];
    gui::g_isRTL = component->direction == PAGE_DIRECTION_RTL;
    gui::refreshScreen();
    propagateValueThroughSeqout(flowState, componentIndex);
}

} // namespace flow
} // namespace eez

#endif // EEZ_OPTION_GUI
