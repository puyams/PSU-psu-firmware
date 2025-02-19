/*
* EEZ Generic Firmware
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

#include <eez/flow/private.h>

namespace eez {
namespace flow {

void queueReset();
size_t getQueueSize();
extern unsigned g_numContinuousTaskInQueue;
bool addToQueue(FlowState *flowState, unsigned componentIndex,
    int sourceComponentIndex, int sourceOutputIndex, int targetInputIndex,
    bool continuousTask);
bool peekNextTaskFromQueue(FlowState *&flowState, unsigned &componentIndex, bool &continuousTask);
void removeNextTaskFromQueue();

bool isInQueue(FlowState *flowState, unsigned componentIndex);

bool isThereAnyTaskInQueueForFlowState(FlowState *flowState, bool includingWatchVariable);

} // flow
} // eez
