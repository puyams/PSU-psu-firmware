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

#if defined(EEZ_DASHBOARD_API)

#include <emscripten.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <eez/core/os.h>

#include <eez/flow/flow.h>
#include <eez/flow/expression.h>
#include <eez/flow/dashboard_api.h>
#include <eez/flow/private.h>
#include <eez/flow/debugger.h>

using namespace eez;
using namespace eez::flow;

namespace eez {
namespace flow {

bool g_dashboardValueFree = false;

int getFlowStateIndex(FlowState *flowState) {
    return (int)((uint8_t *)flowState - ALLOC_BUFFER);
}

FlowState *getFlowStateFromFlowStateIndex(int flowStateIndex) {
    return (FlowState *)(ALLOC_BUFFER + flowStateIndex);
}

struct DashboardComponentExecutionState : public ComponenentExecutionState {
    ~DashboardComponentExecutionState() {
		EM_ASM({
            freeComponentExecutionState($0, $1);
        }, g_wasmModuleId, this);
    }
};

} // flow
} // eez

static void updateArrayValue(ArrayValue *arrayValue1, ArrayValue *arrayValue2) {
    for (uint32_t i = 0; i < arrayValue1->arraySize; i++) {
        if (arrayValue1->values[i].isArray()) {
            updateArrayValue(arrayValue1->values[i].getArray(), arrayValue2->values[i].getArray());
        } else {
            arrayValue1->values[i] = arrayValue2->values[i];
            onValueChanged(&arrayValue1->values[i]);
        }
    }
}

EM_PORT_API(Value *) createUndefinedValue() {
    auto pValue = ObjectAllocator<Value>::allocate(0x2e821285);
    *pValue = Value(0, VALUE_TYPE_UNDEFINED);
    return pValue;
}

EM_PORT_API(Value *) createNullValue() {
    auto pValue = ObjectAllocator<Value>::allocate(0x69debded);
    *pValue = Value(0, VALUE_TYPE_NULL);
    return pValue;
}

EM_PORT_API(Value *) createIntValue(int value) {
    auto pValue = ObjectAllocator<Value>::allocate(0x20ea356c);
    *pValue = Value(value, VALUE_TYPE_INT32);
    return pValue;
}

EM_PORT_API(Value *) createDoubleValue(double value) {
    auto pValue = ObjectAllocator<Value>::allocate(0xecfb69a9);
    *pValue = Value(value, VALUE_TYPE_DOUBLE);
    return pValue;
}

EM_PORT_API(Value *) createBooleanValue(int value) {
    auto pValue = ObjectAllocator<Value>::allocate(0x76071378);
    *pValue = Value(value, VALUE_TYPE_BOOLEAN);
    return pValue;
}

EM_PORT_API(Value *) createStringValue(const char *value) {
    auto pValue = ObjectAllocator<Value>::allocate(0x0a8a7ed1);
    Value stringValue = Value::makeStringRef(value, strlen(value), 0x5b1e51d7);
    *pValue = stringValue;
    return pValue;
}

EM_PORT_API(Value *) createArrayValue(int arraySize, int arrayType) {
    Value value = Value::makeArrayRef(arraySize, arrayType, 0xeabb7edc);
    auto pValue = ObjectAllocator<Value>::allocate(0xbab14c6a);
    if (pValue) {
        *pValue = value;
    }
    return pValue;
}

EM_PORT_API(Value *) createStreamValue(int value) {
    auto pValue = ObjectAllocator<Value>::allocate(0x53a2e660);
    *pValue = Value(value, VALUE_TYPE_STREAM);
    return pValue;
}

EM_PORT_API(Value *) createDateValue(double value) {
    auto pValue = ObjectAllocator<Value>::allocate(0x90b7ce70);
    *pValue = Value(value, VALUE_TYPE_DATE);
    return pValue;
}

EM_PORT_API(Value *) createBlobValue(const uint8_t *buffer, uint32_t bufferLen) {
    auto pValue = ObjectAllocator<Value>::allocate(0x35109c5c);
    *pValue = Value::makeBlobRef(buffer, bufferLen, 0x1100895c);
    return pValue;
}

EM_PORT_API(void) arrayValueSetElementValue(Value *arrayValuePtr, int elementIndex, Value *valuePtr) {
    auto array = arrayValuePtr->getArray();
    array->values[elementIndex] = *valuePtr;
}

EM_PORT_API(void) valueFree(Value *valuePtr) {
    eez::flow::g_dashboardValueFree = true;
    ObjectAllocator<Value>::deallocate(valuePtr);
    eez::flow::g_dashboardValueFree = false;
}

EM_PORT_API(Value *) getGlobalVariable(int globalVariableIndex) {
    auto flowDefinition = static_cast<FlowDefinition *>(eez::g_mainAssets->flowDefinition);
    return flowDefinition->globalVariables[globalVariableIndex];
}

EM_PORT_API(void) setGlobalVariable(int globalVariableIndex, Value *valuePtr) {
    auto flowDefinition = static_cast<FlowDefinition *>(eez::g_mainAssets->flowDefinition);
    Value *globalVariableValuePtr = flowDefinition->globalVariables[globalVariableIndex];
    *globalVariableValuePtr = *valuePtr;
    onValueChanged(globalVariableValuePtr);
}

EM_PORT_API(void) updateGlobalVariable(int globalVariableIndex, Value *valuePtr) {
    auto flowDefinition = static_cast<FlowDefinition *>(eez::g_mainAssets->flowDefinition);
    Value *globalVariableValuePtr = flowDefinition->globalVariables[globalVariableIndex];
    updateArrayValue(globalVariableValuePtr->getArray(), valuePtr->getArray());
}

EM_PORT_API(int) getFlowIndex(int flowStateIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    return flowState->flowIndex;
}

EM_PORT_API(void *) getComponentExecutionState(int flowStateIndex, int componentIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    auto executionState = (DashboardComponentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (executionState) {
        return executionState;
    }
    return nullptr;
}

EM_PORT_API(void *) allocateDashboardComponentExecutionState(int flowStateIndex, int componentIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    auto executionState = (DashboardComponentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (executionState) {
        return executionState;
    }
    return allocateComponentExecutionState<DashboardComponentExecutionState>(flowState, componentIndex);
}

EM_PORT_API(void) deallocateDashboardComponentExecutionState(int flowStateIndex, int componentIndex, int state) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    auto executionState = (DashboardComponentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (executionState) {
        deallocateComponentExecutionState(flowState, componentIndex);
    }
}

EM_PORT_API(uint32_t) getUint8Param(int flowStateIndex, int componentIndex, int offset) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    return *(const uint8_t *)((const uint8_t *)component + sizeof(Component) + offset);
}

EM_PORT_API(uint32_t) getUint32Param(int flowStateIndex, int componentIndex, int offset) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    return *(const uint32_t *)((const uint8_t *)component + sizeof(Component) + offset);
}

EM_PORT_API(const char *) getStringParam(int flowStateIndex, int componentIndex, int offset) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    auto ptr = (AssetsPtr<const char> *)((const uint8_t *)component + sizeof(Component) + offset);
    return *ptr;
}

struct ExpressionList {
    uint32_t count;
    Value values[1];
};

EM_PORT_API(void *) getExpressionListParam(int flowStateIndex, int componentIndex, int offset) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];

    auto &list = *(ListOfAssetsPtr<uint8_t> *)((const uint8_t *)component + sizeof(Component) + offset);

    auto expressionList = (ExpressionList *)::malloc(sizeof(ExpressionList) + (list.count - 1) * sizeof(Value));

    expressionList->count = list.count;

    for (uint32_t i = 0; i < list.count; i++) {
        // call Value constructor
        new (expressionList->values + i) Value();

        auto valueExpression = list[i];
        if (!evalExpression(flowState, componentIndex, valueExpression, expressionList->values[i], "Failed to evaluate expression")) {
            return nullptr;
        }
    }

    return expressionList;
}

EM_PORT_API(void) freeExpressionListParam(void *ptr) {
    auto expressionList = (ExpressionList*)ptr;

    for (uint32_t i = 0; i < expressionList->count; i++) {
        // call Value desctructor
        (expressionList->values + i)->~Value();
    }

    ::free(ptr);
}

struct Expressions {
    AssetsPtr<uint8_t> expressions[1];
};

EM_PORT_API(int) getListParamSize(int flowStateIndex, int componentIndex, int offset) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];
    auto list = (ListOfAssetsPtr<Expressions> *)((const uint8_t *)component + sizeof(Component) + offset);
    return list->count;
}

EM_PORT_API(Value *) evalListParamElementExpression(int flowStateIndex, int componentIndex, int listOffset, int elementIndex, int expressionOffset, const char *errorMessage) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto component = flowState->flow->components[componentIndex];

    ListOfAssetsPtr<Expressions> &list = *(ListOfAssetsPtr<Expressions> *)((const uint8_t *)component + sizeof(Component) + listOffset);
    auto &expressionInstructions = list[elementIndex]->expressions[expressionOffset / 4];

    Value result;
    if (!evalExpression(flowState, componentIndex, expressionInstructions, result, errorMessage)) {
        return nullptr;
    }

    auto pValue = ObjectAllocator<Value>::allocate(0x15cb2009);
    if (!pValue) {
        throwError(flowState, componentIndex, "Out of memory\n");
        return nullptr;
    }

    *pValue = result;

    return pValue;
}

EM_PORT_API(Value*) getInputValue(int flowStateIndex, int inputIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    return flowState->values + inputIndex;
}

EM_PORT_API(void) clearInputValue(int flowStateIndex, int inputIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    clearInputValue(flowState, inputIndex);
}

EM_PORT_API(Value *) evalProperty(int flowStateIndex, int componentIndex, int propertyIndex, int32_t *iterators, bool disableThrowError) {
    if (eez::flow::isFlowStopped()) {
        return nullptr;
    }

    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);

    if (disableThrowError) {
        eez::flow::enableThrowError(false);
    }

    Value result;
    if (!eez::flow::evalProperty(flowState, componentIndex, propertyIndex, result, "Failed to evaluate property", nullptr, iterators)) {
        return nullptr;
    }

    if (disableThrowError) {
        eez::flow::enableThrowError(true);
    }

    auto pValue = ObjectAllocator<Value>::allocate(0xb7e697b8);
    if (!pValue) {
        throwError(flowState, componentIndex, "Out of memory\n");
        return nullptr;
    }

    *pValue = result;

    return pValue;
}

EM_PORT_API(void) assignProperty(int flowStateIndex, int componentIndex, int propertyIndex, int32_t *iterators, Value *srcValuePtr) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);

    Value dstValue;
    if (evalAssignableProperty(flowState, componentIndex, propertyIndex, dstValue, nullptr, nullptr, iterators)) {
        assignValue(flowState, componentIndex, dstValue, *srcValuePtr);
    }
}

EM_PORT_API(void) setPropertyField(int flowStateIndex, int componentIndex, int propertyIndex, int fieldIndex, Value *valuePtr) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);

    Value result;
    if (!eez::flow::evalProperty(flowState, componentIndex, propertyIndex, result, "Failed to evaluate property")) {
        return;
    }

	if (result.getType() == VALUE_TYPE_VALUE_PTR) {
		result = *result.pValueValue;
	}

    if (!result.isArray()) {
        throwError(flowState, componentIndex, "Property is not an array");
        return;
    }

    auto array = result.getArray();

    if (fieldIndex < 0 || fieldIndex >= array->arraySize) {
        throwError(flowState, componentIndex, "Invalid field index");
        return;
    }

    array->values[fieldIndex] = *valuePtr;
    onValueChanged(array->values + fieldIndex);
}

EM_PORT_API(void) propagateValue(int flowStateIndex, int componentIndex, int outputIndex, Value *valuePtr) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::propagateValue(flowState, componentIndex, outputIndex, *valuePtr);
}

EM_PORT_API(void) propagateValueThroughSeqout(int flowStateIndex, int componentIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
	eez::flow::propagateValueThroughSeqout(flowState, componentIndex);
}

EM_PORT_API(void) startAsyncExecution(int flowStateIndex, int componentIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::startAsyncExecution(flowState, componentIndex);
}

EM_PORT_API(void) endAsyncExecution(int flowStateIndex, int componentIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::endAsyncExecution(flowState, componentIndex);
}

EM_PORT_API(void) executeCallAction(int flowStateIndex, int componentIndex, int flowIndex) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::executeCallAction(flowState, componentIndex, flowIndex);
}

EM_PORT_API(void) onEvent(int flowStateIndex, FlowEvent flowEvent, Value *flowValue) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::onEvent(flowState, flowEvent, *flowValue);
}

EM_PORT_API(void) logInfo(int flowStateIndex, int componentIndex, const char *infoMessage) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    eez::flow::logInfo(flowState, componentIndex, infoMessage);
}

EM_PORT_API(void) throwError(int flowStateIndex, int componentIndex, const char *errorMessage) {
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
	eez::flow::throwError(flowState, componentIndex, errorMessage);
}

EM_PORT_API(int) getFirstRootFlowState() {
    if (!g_firstFlowState) {
        return -1;
    }
    return getFlowStateIndex(g_firstFlowState);
}

EM_PORT_API(int) getFirstChildFlowState(int flowStateIndex) {
    if (flowStateIndex == -1) {
        return -1;
    }
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto firstChildFlowState = flowState->firstChild;
    if (!firstChildFlowState) {
        return -1;
    }
    return getFlowStateIndex(firstChildFlowState);
}

EM_PORT_API(int) getNextSiblingFlowState(int flowStateIndex) {
    if (flowStateIndex == -1) {
        return -1;
    }
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    auto nextSiblingFlowState = flowState->nextSibling;
    if (!nextSiblingFlowState) {
        return -1;
    }
    return getFlowStateIndex(nextSiblingFlowState);
}

EM_PORT_API(int) getFlowStateFlowIndex(int flowStateIndex) {
    if (flowStateIndex == -1) {
        return -1;
    }
    auto flowState = getFlowStateFromFlowStateIndex(flowStateIndex);
    return flowState->flowIndex;
}

EM_PORT_API(void) setDebuggerMessageSubsciptionFilter(uint32_t filter) {
    eez::flow::setDebuggerMessageSubsciptionFilter(filter);
}

#if EEZ_OPTION_GUI
EM_PORT_API(bool) isRTL() {
    return g_isRTL;
}
#endif

#endif // EEZ_DASHBOARD_API
