/*
* EEZ PSU Firmware
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

#if OPTION_DISPLAY

#include <assert.h>

#if defined(EEZ_PLATFORM_STM32)
#include <usbh_hid_keybd.h>
#endif

#include <bb3/firmware.h>
#include <eez/core/sound.h>
#include <bb3/system.h>
#include <bb3/hmi.h>
#include <bb3/keyboard.h>
#include <bb3/mouse.h>
#include <bb3/scripting/scripting.h>
#include <bb3/scripting/flow.h>
#if OPTION_ENCODER
#include <bb3/mcu/encoder.h>
#endif

#include <bb3/scpi/regs.h>

#include <bb3/psu/psu.h>
#include <bb3/psu/channel_dispatcher.h>
#include <bb3/psu/devices.h>
#include <bb3/psu/event_queue.h>
#include <bb3/psu/temperature.h>
#include <bb3/psu/trigger.h>
#include <bb3/psu/dlog_view.h>
#include <bb3/psu/dlog_record.h>
#include <bb3/psu/dlog_view.h>
#include <bb3/psu/sd_card.h>

#include <bb3/psu/gui/psu.h>
#include <bb3/psu/gui/animations.h>
#include <bb3/psu/gui/edit_mode.h>
#include <bb3/psu/gui/keypad.h>
#include <bb3/psu/gui/channel_calibration.h>
#include <bb3/psu/gui/page_ch_settings.h>
#include <bb3/psu/gui/page_event_queue.h>
#include <bb3/psu/gui/page_sys_settings.h>
#include <bb3/psu/gui/page_user_profiles.h>
#include <bb3/psu/gui/password.h>
#include <bb3/psu/gui/file_manager.h>
#include <eez/gui/touch_calibration.h>
#include <bb3/psu/gui/labels_and_colors.h>

#include <bb3/gui/display-hooks.h>

#include <bb3/function_generator.h>

#if OPTION_ENCODER
#include <bb3/mcu/encoder.h>
#endif
#if EEZ_PLATFORM_STM32
#include <bb3/mcu/button.h>
#endif

#if defined(EEZ_PLATFORM_SIMULATOR)
#include <bb3/platform/simulator/front_panel.h>
#endif

#define CONF_GUI_ENTERING_STANDBY_PAGE_TIMEOUT_MS 2000
#define CONF_GUI_STANDBY_PAGE_TIMEOUT_MS 4000
#define CONF_GUI_DISPLAY_OFF_PAGE_TIMEOUT_MS 2000
#define CONF_GUI_WELCOME_PAGE_TIMEOUT_MS 1200

using namespace eez::psu::gui;

////////////////////////////////////////////////////////////////////////////////

namespace eez {
namespace psu {
namespace gui {

PsuAppContext g_psuAppContext;

static unsigned g_skipChannelCalibrations;
static unsigned g_skipDateTimeSetup;
static unsigned g_skipEthernetSetup;

static bool g_showSetupWizardQuestionCalled;
Channel *g_channel = nullptr;
int g_channelIndex = -1;
static WidgetCursor g_toggleOutputWidgetCursor;

#if EEZ_PLATFORM_STM32
static mcu::Button g_userSwitch(USER_SW_GPIO_Port, USER_SW_Pin, true, true);
#endif

Value g_progress;

static WidgetCursor g_externalActionWidgetCursor;
static int16_t g_externalActionId = ACTION_ID_NONE;
static const size_t MAX_NUM_EXTERNAL_DATA_ITEM_VALUES = 20;
static struct {
    Value value;
    char text[128 + 1];
    int textIndex;
} g_externalDataItemValues[MAX_NUM_EXTERNAL_DATA_ITEM_VALUES];

int g_displayTestColorIndex = 0;

bool g_setFocusCursor = false;
WidgetCursor g_focusCursorToSet;
int16_t g_focusDataIdToSet;

bool showSetupWizardQuestion();
void onEncoder(int counter, bool clicked);

static void moveToNextFocusCursor();
static void testIsEncoderEnabledInActivePage();

static bool g_autoRecallExecuted = false;

////////////////////////////////////////////////////////////////////////////////

void showDebugTraceLog() {
    event_queue::setFilter(event_queue::EVENT_TYPE_DEBUG);
    pushPage(PAGE_ID_EVENT_QUEUE);
}

////////////////////////////////////////////////////////////////////////////////

PsuAppContext::PsuAppContext() {
    m_pushProgressPage = false;
    m_popProgressPage = false;
}

void PsuAppContext::stateManagment() {
    AppContext::stateManagment();

    if (getActivePageId() == PAGE_ID_WELCOME) {
        playPowerUp(sound::PLAY_POWER_UP_CONDITION_WELCOME_PAGE_IS_ACTIVE);
    } 

    if (m_popProgressPage) {
        doHideProgressPage();
    }

    if (m_clearTextMessage) {
        m_clearTextMessage = false;
        if (isPageOnStack(PAGE_ID_TEXT_MESSAGE)) {
            removePageFromStack(PAGE_ID_TEXT_MESSAGE);
            m_textMessage[0] = 0;
        }
    }

    if (isPageOnStack(PAGE_ID_TEXT_MESSAGE) && !m_textMessage[0]) {
    	removePageFromStack(PAGE_ID_TEXT_MESSAGE);
    }

    uint32_t tickCount = millis();

    // wait some time for transitional pages
    int activePageId = getActivePageId();
    if (activePageId == PAGE_ID_STANDBY) {
        if (int32_t(tickCount - m_showPageTime) < CONF_GUI_STANDBY_PAGE_TIMEOUT_MS) {
            return;
        }
    } else if (activePageId == PAGE_ID_ENTERING_STANDBY) {
        if (int32_t(tickCount - m_showPageTime) >= CONF_GUI_ENTERING_STANDBY_PAGE_TIMEOUT_MS) {
            uint32_t saved_showPageTime = m_showPageTime;
            showStandbyPage();
            m_showPageTime = saved_showPageTime;
        }
        return;
    } else if (activePageId == PAGE_ID_WELCOME) {
        if (!g_isBooted || int32_t(tickCount - m_showPageTime) < CONF_GUI_WELCOME_PAGE_TIMEOUT_MS) {
            return;
        }
    }

#if EEZ_PLATFORM_STM32
    if (g_userSwitch.isPressed() && mcu::encoder::isButtonPressed()) {
        scripting::stopScript();
    }
#endif

#if EEZ_PLATFORM_STM32
    if (g_userSwitch.isLongPress()) {
        action_select_user_switch_action();
    } else if (g_userSwitch.isClicked()) {
        action_user_switch_clicked();
    }
#endif

    // turn the screen off if power is down and system is booted
    if (!g_powerIsUp) {
    	if (g_isBooted && !g_shutdownInProgress && getActivePageId() != PAGE_ID_NONE) {
    		showPage(PAGE_ID_NONE);
    		eez::gui::display::turnOff();
    	}
        return;
    }

    // turn display on/off depending on displayState
    if (
        psu::persist_conf::devConf.displayState == 0 && 
        (activePageId != PAGE_ID_DISPLAY_OFF && isTouchCalibrated())
    ) {
        showPage(PAGE_ID_DISPLAY_OFF);
        return;
    } else if (psu::persist_conf::devConf.displayState == 1 && activePageId == PAGE_ID_DISPLAY_OFF) {
		eez::gui::display::turnOn();
        showPage(getMainPageId());
        return;
    }

    // select page to go after transitional page
    if (activePageId == PAGE_ID_WELCOME || activePageId == PAGE_ID_STANDBY || activePageId == PAGE_ID_ENTERING_STANDBY) {
#if defined(EEZ_PLATFORM_STM32)
        if (!isTouchCalibrated()) {
            // touch screen is not calibrated
            sound::playBeep(true);
            showPage(PAGE_ID_TOUCH_CALIBRATION_INTRO);
			return;
		}
#endif

        if (activePageId == PAGE_ID_WELCOME) {
            if (!g_autoRecallExecuted) {
                sendMessageToLowPriorityThread(THREAD_MESSAGE_AUTO_RECALL);
                g_autoRecallExecuted = true;
            } else {
                if (!scripting::isAutoStartEnabled()) {
                    showPage(getMainPageId());
                    return;
                }
            }
        }
    }

	uint32_t inactivityPeriod = eez::hmi::getInactivityPeriodMs();

    // start touch screen calibration automatically after period of time
    if (activePageId == PAGE_ID_TOUCH_CALIBRATION_INTRO) {
        if (inactivityPeriod >= 20 * 1000UL) {
            enterTouchCalibration(this);
            return;
        }
    }

    // TODO move this to some other place
#if OPTION_ENCODER
    int counter;
    bool clicked;
    mcu::encoder::read(counter, clicked);
#endif

    // handling of display off page
    if (activePageId == PAGE_ID_DISPLAY_OFF) {
        if (eez::gui::display::isOn()) {
            if (int32_t(tickCount - m_showPageTime) >= CONF_GUI_DISPLAY_OFF_PAGE_TIMEOUT_MS) {
				eez::gui::display::turnOff();
                m_showPageTime = tickCount;
            }
        }
        return;
    }

    // TODO move this to some other place
#if OPTION_ENCODER
    if (counter != 0 || clicked) {
        eez::hmi::noteActivity();
    }
    onEncoder(counter, clicked);
#endif

#if GUI_BACK_TO_MAIN_ENABLED
    uint32_t inactivityPeriod = eez::idle::getInactivityPeriodMs();

    if (
        activePageId == PAGE_ID_EVENT_QUEUE ||
        activePageId == PAGE_ID_USER_PROFILES ||
        activePageId == PAGE_ID_USER_PROFILE_0_SETTINGS ||
        activePageId == PAGE_ID_USER_PROFILE_SETTINGS
    ) {
        if (inactivityPeriod >= GUI_BACK_TO_MAIN_DELAY * 1000UL) {
            showPage(PAGE_ID_MAIN);
        }
    }
#endif

    //
    if (g_rprogAlarm) {
        g_rprogAlarm = false;
        errorMessage("Max. remote prog. voltage exceeded.\nPlease remove it immediately!");
    }

    // show startup wizard
    if (!isFrontPanelLocked() && activePageId == PAGE_ID_MAIN && int32_t(millis() - m_showPageTime) >= 250L) {
        if (showSetupWizardQuestion()) {
            return;
        }
    }

    if (m_pushProgressPage) {
        doShowProgressPage();
    }

    if (m_showTextMessage) {
        m_showTextMessage = false;
        if (!isPageOnStack(PAGE_ID_TEXT_MESSAGE)) {
            pushPage(PAGE_ID_TEXT_MESSAGE);
        } else {
            ++m_textMessageVersion;
        }
    }

    dlog_view::stateManagment();

    if (m_showUncaughtScriptExceptionMessage) {
        m_showUncaughtScriptExceptionMessage = false;
        errorMessageWithAction("Uncaught script exception!", showDebugTraceLog, "Show debug trace log");
    }

    if (!sd_card::isMounted(nullptr, nullptr)) {
        if (
            isPageOnStack(PAGE_ID_DLOG_PARAMS) ||
            isPageOnStack(PAGE_ID_DLOG_VIEW) ||
            isPageOnStack(PAGE_ID_USER_PROFILES) ||
            isPageOnStack(PAGE_ID_USER_PROFILE_SETTINGS) ||
            isPageOnStack(PAGE_ID_USER_PROFILE_0_SETTINGS)
        ) {
            showPage(PAGE_ID_MAIN);
        }
    }

    // call m_checkAsyncOperationStatus
    if (getActivePageId() == PAGE_ID_ASYNC_OPERATION_IN_PROGRESS) {
        if (m_asyncOperationInProgressParams.checkStatus) {
            m_asyncOperationInProgressParams.checkStatus();
        }
    }

    //
    if (getActivePageId() == INTERNAL_PAGE_ID_TOAST_MESSAGE) {
        ToastMessagePage *page = (ToastMessagePage *)getActivePage();
        if (page->messageValue.getType() == VALUE_TYPE_EVENT_MESSAGE) {

            int16_t eventId = page->messageValue.getFirstInt16();
            int channelIndex = page->messageValue.getSecondInt16();
            Channel &channel = Channel::get(channelIndex);

            bool dismissPage = false;
            
            if (eventId == event_queue::EVENT_ERROR_CH_OVP_TRIPPED) {
                dismissPage = !eez::scpi::is_ques_bit_enabled(channelIndex, QUES_ISUM_OVP) || !channel.isTripped();
            } else if (eventId == event_queue::EVENT_ERROR_CH_OCP_TRIPPED) {
                dismissPage = !eez::scpi::is_ques_bit_enabled(channelIndex, QUES_ISUM_OCP) || !channel.isTripped();
            } else if (eventId == event_queue::EVENT_ERROR_CH_OPP_TRIPPED) {
                dismissPage = !eez::scpi::is_ques_bit_enabled(channelIndex, QUES_ISUM_OPP) || !channel.isTripped();
            } else if (eventId == event_queue::EVENT_ERROR_CH_REMOTE_SENSE_REVERSE_POLARITY_DETECTED) {
                dismissPage = !eez::scpi::is_ques_bit_enabled(channelIndex, QUES_ISUM_RPOL);
            } else if (
                eventId >= event_queue::EVENT_ERROR_SLOT1_SYNC_ERROR &&
                eventId <= event_queue::EVENT_ERROR_SLOT3_SYNC_ERROR
            ) {
                dismissPage = g_slots[eventId - event_queue::EVENT_ERROR_SLOT1_SYNC_ERROR]->getTestResult() == TEST_OK;
            }

            if (dismissPage) {
                popPage();
            }
        }
    }
    //
    if (hmi::g_selectedSlotIndex != -1) {
        auto slotSettingsPageId = g_slots[hmi::g_selectedSlotIndex]->getSlotSettingsPageId();
        if (psu::gui::isPageOnStack(slotSettingsPageId) && g_slots[hmi::g_selectedSlotIndex]->getTestResult() != TEST_OK) {
            psu::gui::showPage(PAGE_ID_SLOT_SETTINGS);
        } else if (psu::gui::isPageOnStack(PAGE_ID_SLOT_SETTINGS) && g_slots[hmi::g_selectedSlotIndex]->getTestResult() == TEST_OK) {
            psu::gui::showPage(slotSettingsPageId);
        }
    }

	//
	if (g_setFocusCursor) {
		g_setFocusCursor = false;
		g_focusCursor = g_focusCursorToSet;
		g_focusDataId = g_focusDataIdToSet;
		g_focusEditValue = Value();
	}

    if (g_widgetStateStructureChanged) {
		testIsEncoderEnabledInActivePage();
    }
}

int PsuAppContext::getMainPageId() {
    return PAGE_ID_MAIN;
}

bool isSysSettingsSubPage(int pageId) {
    return pageId == PAGE_ID_SYS_SETTINGS_TEMPERATURE ||
        pageId == PAGE_ID_SYS_SETTINGS_PROTECTIONS ||
        pageId == PAGE_ID_SYS_SETTINGS_IO ||
        pageId == PAGE_ID_SYS_SETTINGS_DATE_TIME ||
        pageId == PAGE_ID_SYS_SETTINGS_ENCODER ||
        pageId == PAGE_ID_SYS_SETTINGS_USB ||
        pageId == PAGE_ID_SYS_SETTINGS_ETHERNET ||
        pageId == PAGE_ID_SYS_SETTINGS_TRIGGER ||
        pageId == PAGE_ID_SYS_SETTINGS_RAMP_AND_DELAY ||
        pageId == PAGE_ID_SYS_SETTINGS_TRACKING ||
        pageId == PAGE_ID_SYS_SETTINGS_DISPLAY ||
        pageId == PAGE_ID_SYS_SETTINGS_SOUND ||
        pageId == PAGE_ID_SYS_SETTINGS_MQTT ||
        pageId == PAGE_ID_SYS_SETTINGS_LABELS_AND_COLORS ||
        pageId == PAGE_ID_SYS_INFO ||
		pageId == PAGE_ID_SYS_SETTINGS_SCRIPTING;
}

bool isChSettingsSubPage(int pageId) {
    return
    	pageId == PAGE_ID_CH_SETTINGS_PROT_OVP ||
        pageId == PAGE_ID_CH_SETTINGS_PROT_OCP ||
        pageId == PAGE_ID_CH_SETTINGS_PROT_OPP ||
        pageId == PAGE_ID_CH_SETTINGS_PROT_OTP ||
        pageId == PAGE_ID_CH_SETTINGS_TRIGGER ||
        pageId == PAGE_ID_CH_SETTINGS_LISTS ||
        pageId == PAGE_ID_CH_SETTINGS_ADV_OPTIONS ||
        pageId == PAGE_ID_CH_SETTINGS_ADV_RANGES ||
        pageId == PAGE_ID_CH_SETTINGS_ADV_VIEW ||
        pageId == PAGE_ID_CH_SETTINGS_INFO;
}

void PsuAppContext::onPageChanged(int previousPageId, int activePageId, bool activePageIsFromStack, bool previousPageIsStillOnStack) {
    AppContext::onPageChanged(previousPageId, activePageId, activePageIsFromStack, previousPageIsStillOnStack);

    g_focusEditValue = Value();

    for (int i = 0; i < NUM_SLOTS; i++) {
        g_slots[i]->animatePageAppearance(previousPageId, activePageId);
    }

    if (previousPageId == activePageId) {
    } else if (previousPageId == PAGE_ID_NONE) {
        if (activePageId == PAGE_ID_WELCOME) {
            animateFadeOutFadeIn();
        }
    } else if (previousPageId == PAGE_ID_WELCOME) {
        animateFadeOutFadeIn();
    } else if (previousPageId == PAGE_ID_EVENT_QUEUE || previousPageId == PAGE_ID_DLOG_PARAMS) {
        if (getActivePageId() == PAGE_ID_MAIN) {
            animateSlideUp();
        }
    } else if (previousPageId == PAGE_ID_MAIN) {
        if (activePageId == PAGE_ID_SYS_SETTINGS) {
			animateSlideDown();
		} else if (isSysSettingsSubPage(activePageId)) {
			animateSlideDown();
		} else if (activePageId == PAGE_ID_CH_SETTINGS) {
            animateSlideDown();
        } else if (isChSettingsSubPage(activePageId)) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_EVENT_QUEUE) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_DLOG_PARAMS) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_USER_PROFILES) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_DLOG_VIEW) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_FILE_MANAGER) {
            animateSlideDown();
        }
    } else if (previousPageId == PAGE_ID_USER_PROFILES) {
        if (activePageId == PAGE_ID_USER_PROFILE_0_SETTINGS) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_USER_PROFILE_SETTINGS) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        }
    } else if (previousPageId == PAGE_ID_USER_PROFILE_0_SETTINGS) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        } else if (activePageId == PAGE_ID_USER_PROFILES) {
            animateSlideUp();
        }
    } else if (previousPageId == PAGE_ID_USER_PROFILE_SETTINGS) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        } else if (activePageId == PAGE_ID_USER_PROFILES) {
            animateSlideUp();
        }
	} else if (previousPageId == PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR) {
		if (activePageId == PAGE_ID_MAIN) {
			animateSlideUp();
		}
	} else if (previousPageId == PAGE_ID_SYS_SETTINGS_TRACKING) {
        if (activePageId == PAGE_ID_SYS_SETTINGS_COUPLING) {
            animateSlideLeft();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS) {
			animateSlideRight();
		}
    } else if (previousPageId == PAGE_ID_SYS_SETTINGS_COUPLING) {
        if (activePageId == PAGE_ID_SYS_SETTINGS_TRACKING) {
            animateSlideRight();
        }
    } else if (previousPageId == PAGE_ID_SYS_SETTINGS) {
        if (activePageId == PAGE_ID_MAIN) {
			animateSlideUp();
		} else if (isSysSettingsSubPage(activePageId)) {
            animateSlideLeft();
        }
    } else if (previousPageId == PAGE_ID_SYS_SETTINGS_TRIGGER || previousPageId == PAGE_ID_SYS_SETTINGS_RAMP_AND_DELAY || previousPageId == PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR) {
        if (activePageId == PAGE_ID_MAIN) {
			animateSlideUp();
		} else if (activePageId == PAGE_ID_CH_SETTINGS_TRIGGER) {
            animateSlideUp();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS) {
            animateSlideRight();
        }
    } else if (isSysSettingsSubPage(previousPageId)) {
        if (activePageId == PAGE_ID_MAIN) {
			animateSlideUp();
		} else if (activePageId == PAGE_ID_SYS_SETTINGS) {
            animateSlideRight();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_MQTT) {
            animateSlideLeft();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_LABELS_AND_COLORS) {
            animateSlideLeft();
        }
    } else if (previousPageId == PAGE_ID_CH_SETTINGS) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        } else if (isChSettingsSubPage(activePageId)) {
            animateSlideLeft();
        }
    } else if (previousPageId == PAGE_ID_CH_SETTINGS_TRIGGER) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_TRIGGER || activePageId == PAGE_ID_SYS_SETTINGS_RAMP_AND_DELAY || activePageId == PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_CH_SETTINGS_LISTS) {
            animateSlideDown();
        } else if (activePageId == PAGE_ID_CH_SETTINGS) {
            animateSlideRight();
        }
    } else if (previousPageId == PAGE_ID_CH_SETTINGS_LISTS) {
        if (activePageId == PAGE_ID_CH_SETTINGS_TRIGGER) {
            animateSlideUp();
        }
    } else if (isChSettingsSubPage(previousPageId)) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        } else if (activePageId == PAGE_ID_CH_SETTINGS) {
            animateSlideRight();
        }
    } else if (previousPageId == PAGE_ID_DLOG_VIEW) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        }
    } else if (previousPageId == PAGE_ID_FILE_MANAGER) {
        if (activePageId == PAGE_ID_MAIN) {
            animateSlideUp();
        }
    } else if (
		previousPageId == PAGE_ID_IMAGE_VIEW || activePageId == PAGE_ID_IMAGE_VIEW ||
		activePageId == PAGE_ID_PINOUT || previousPageId == PAGE_ID_PINOUT
	) {
        animateFadeOutFadeIn();
    } else if (previousPageId == PAGE_ID_SYS_SETTINGS_MQTT) {
        if (activePageId == PAGE_ID_SYS_SETTINGS_ETHERNET) {
            animateSlideRight();
        }
    } else if (previousPageId == PAGE_ID_SYS_SETTINGS_LABELS_AND_COLORS) {
        if (activePageId == PAGE_ID_SYS_SETTINGS_DISPLAY) {
            animateSlideRight();
        }
    }
}

bool PsuAppContext::isFocusWidget(const WidgetCursor &widgetCursor) {
    if (isPageOnStack(PAGE_ID_CH_SETTINGS_LISTS)) {
        return ((ChSettingsListsPage *)getPage(PAGE_ID_CH_SETTINGS_LISTS))->isFocusWidget(widgetCursor);
    }

    auto action = getWidgetAction(widgetCursor);        
    return (widgetCursor.cursor == -1 || widgetCursor == g_focusCursor) && widgetCursor.widget->data == g_focusDataId && action != ACTION_ID_EDIT_NO_FOCUS && isEncoderEnabledInActivePage();
}

bool PsuAppContext::isAutoRepeatAction(int action) {
    return action == ACTION_ID_KEYPAD_BACK ||
        action == ACTION_ID_CHANNEL_LISTS_PREVIOUS_PAGE ||
        action == ACTION_ID_CHANNEL_LISTS_NEXT_PAGE;
}

void PsuAppContext::onPageTouch(const WidgetCursor &foundWidget, Event &touchEvent) {
    if (isFrontPanelLocked()) {
        errorMessage("Front panel is locked!");
        return;
    }

    int activePageId = getActivePageId();

    if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchDown(touchEvent);
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchDown(touchEvent);
        }
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_MOVE) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchMove(touchEvent);
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchMove(touchEvent);
        }        
    } else if (touchEvent.type == EVENT_TYPE_TOUCH_UP) {
        if (activePageId == PAGE_ID_EDIT_MODE_SLIDER) {
            edit_mode_slider::onTouchUp(touchEvent);
        } else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onTouchUp(touchEvent);
        } else if (activePageId == PAGE_ID_TOUCH_CALIBRATION_INTRO) {
            enterTouchCalibration(this);
        }
    } else if (touchEvent.type == EVENT_TYPE_LONG_TOUCH) {
        if (activePageId == PAGE_ID_NONE || activePageId == PAGE_ID_STANDBY) {
            // wake up on long touch
            psu::changePowerState(true);
        } else if (activePageId == PAGE_ID_DISPLAY_OFF) {
            // turn ON display on long touch
            psu::persist_conf::setDisplayState(1);
        }
    } else if (touchEvent.type == EVENT_TYPE_EXTRA_LONG_TOUCH) {
#if !defined(EEZ_PLATFORM_SIMULATOR)
        // start touch screen calibration in case of really long touch
        showPage(PAGE_ID_TOUCH_CALIBRATION_INTRO);
#endif
    }

    AppContext::onPageTouch(foundWidget, touchEvent);
}

bool PsuAppContext::testExecuteActionOnTouchDown(int action) {
    return action == ACTION_ID_CHANNEL_TOGGLE_OUTPUT || isAutoRepeatAction(action);
}

bool PsuAppContext::isBlinking(const WidgetCursor &widgetCursor, int16_t id) {
    if (g_focusCursor == widgetCursor && g_focusDataId == id && g_focusEditValue.getType() != VALUE_TYPE_UNDEFINED) {
        return true;
    }

    return AppContext::isBlinking(widgetCursor, id);
}

bool PsuAppContext::isWidgetActionEnabled(const WidgetCursor &widgetCursor) {
    auto action = getWidgetAction(widgetCursor);        
    if (action) {
        if (isFrontPanelLocked()) {
            int activePageId = getActivePageId();
            if (activePageId == PAGE_ID_KEYPAD ||
                activePageId == PAGE_ID_TOUCH_CALIBRATION_YES_NO ||
                activePageId == PAGE_ID_TOUCH_CALIBRATION_YES_NO_CANCEL ||
                activePageId == INTERNAL_PAGE_ID_TOAST_MESSAGE
            ) {
                return true;
            }
            
            if (action != ACTION_ID_SYS_FRONT_PANEL_UNLOCK) {
                return false;
            }
        }
    
        if (action == ACTION_ID_SHOW_EVENT_QUEUE) {
            static const uint32_t CONF_SHOW_EVENT_QUEUE_INACTIVITY_TIMESPAN_SINCE_LAST_SHOW_PAGE_MS = 500;
            if (millis() - m_showPageTime < CONF_SHOW_EVENT_QUEUE_INACTIVITY_TIMESPAN_SINCE_LAST_SHOW_PAGE_MS) {
                return false;
            }
        }

        if (action == ACTION_ID_FILE_MANAGER_SELECT_FILE) {
            return file_manager::isSelectFileActionEnabled(widgetCursor.cursor);
        }

        if (
            action == ACTION_ID_EDIT ||
            action == ACTION_ID_SCROLL ||
            action == ACTION_ID_EDIT_NO_FOCUS ||
            action == ACTION_ID_CH_SETTINGS_PROT_EDIT_LIMIT
        ) {
            if (widgetCursor.widget->data == DATA_ID_CALIBRATION_POINT_MEASURED_VALUE) {
                auto page = (ChSettingsCalibrationEditPage *)getPage(PAGE_ID_CH_SETTINGS_CALIBRATION_EDIT);
                return page->canEditMeasuredValue();
            }
            return channel_dispatcher::isEditEnabled(widgetCursor);
        }
    }

    return AppContext::isWidgetActionEnabled(widgetCursor);
}

void PsuAppContext::doShowProgressPage() {
    set(WidgetCursor(), DATA_ID_ALERT_MESSAGE, Value(m_progressMessage));
    m_dialogCancelCallback = m_progressAbortCallback;
    pushPage(m_progressWithoutAbort ? PAGE_ID_PROGRESS_WITHOUT_ABORT : PAGE_ID_PROGRESS);
    m_pushProgressPage = false;
}

void PsuAppContext::showProgressPage(const char *message, void (*abortCallback)()) {
    m_progressMessage = message;
    m_progressWithoutAbort = false;
    m_progressAbortCallback = abortCallback;
    m_pushProgressPage = true;
    g_progress = Value();

    if (isGuiThread()) {
    	doShowProgressPage();
    }
}

void PsuAppContext::showProgressPageWithoutAbort(const char *message) {
    m_progressMessage = message;
    m_progressWithoutAbort = true;
    m_pushProgressPage = true;
    g_progress = Value();

    if (isGuiThread()) {
    	doShowProgressPage();
    }
}

bool PsuAppContext::updateProgressPage(size_t processedSoFar, size_t totalSize) {
    if (totalSize > 0) {
        g_progress = Value((int)round((processedSoFar * 1.0f / totalSize) * 100.0f), VALUE_TYPE_PERCENTAGE);
    } else {
        g_progress = Value((uint32_t)processedSoFar, VALUE_TYPE_SIZE);
    }

    if (m_pushProgressPage) {
        return true;
    }

    return isPageOnStack(m_progressWithoutAbort ? PAGE_ID_PROGRESS_WITHOUT_ABORT : PAGE_ID_PROGRESS);
}

void PsuAppContext::doHideProgressPage() {
    removePageFromStack(m_progressWithoutAbort ? PAGE_ID_PROGRESS_WITHOUT_ABORT : PAGE_ID_PROGRESS);
    m_popProgressPage = false;
}

void PsuAppContext::hideProgressPage() {
    if (m_pushProgressPage) {
        m_pushProgressPage = false;
    } else {
        m_popProgressPage = true;
    }

    if (isGuiThread()) {
    	doHideProgressPage();
    }
}

void PsuAppContext::showAsyncOperationInProgress(const char *message, void (*checkStatus)()) {
    m_asyncOperationInProgressParams.message = message;
    m_asyncOperationInProgressParams.checkStatus = checkStatus;

    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_ASYNC_OPERATION_IN_PROGRESS);
}

void PsuAppContext::doShowAsyncOperationInProgress() {
    set(WidgetCursor(), DATA_ID_ALERT_MESSAGE, Value(m_asyncOperationInProgressParams.message));

    if (getActivePageId() != PAGE_ID_ASYNC_OPERATION_IN_PROGRESS) {
        m_asyncOperationInProgressParams.startTime = millis();
        pushPage(PAGE_ID_ASYNC_OPERATION_IN_PROGRESS);
    }
}

void PsuAppContext::hideAsyncOperationInProgress() {
    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_HIDE_ASYNC_OPERATION_IN_PROGRESS);
}

void PsuAppContext::doHideAsyncOperationInProgress() {
    removePageFromStack(PAGE_ID_ASYNC_OPERATION_IN_PROGRESS);
}

uint32_t PsuAppContext::getAsyncInProgressStartTime() {
    return m_asyncOperationInProgressParams.startTime;
}

void PsuAppContext::setTextMessage(const char *message, unsigned int len) {
    stringCopy(m_textMessage, MIN(len + 1, sizeof(m_textMessage)), message);
	DebugTrace("Display text changed: %s\n", m_textMessage);
    m_showTextMessage = true;
}

void PsuAppContext::clearTextMessage() {
    m_clearTextMessage =  true;
}

const char *PsuAppContext::getTextMessage() {
    return m_textMessage;
}

uint8_t PsuAppContext::getTextMessageVersion() {
    return m_textMessageVersion;
}

void PsuAppContext::showUncaughtScriptExceptionMessage() {
    m_showUncaughtScriptExceptionMessage = true;
}

void TextInputParams::onSet(char *value) {
    g_psuAppContext.m_textInputParams.m_input = value;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

void TextInputParams::onCancel() {
    g_psuAppContext.m_textInputParams.m_input = nullptr;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

const char *PsuAppContext::textInput(const char *label, size_t minChars, size_t maxChars, const char *value) {
    m_inputLabel = label[0] ? label : nullptr;
    m_textInputParams.m_minChars = minChars;
    m_textInputParams.m_maxChars = maxChars;
    m_textInputParams.m_input = value;

    m_inputReady = false;

    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_TEXT_INPUT);

    while (!m_inputReady) {
        osDelay(5);
    }

    return m_textInputParams.m_input;
}

void PsuAppContext::doShowTextInput() {
    startTextKeypad(m_inputLabel, m_textInputParams.m_input, m_textInputParams.m_minChars, m_textInputParams.m_maxChars, false, m_textInputParams.onSet, m_textInputParams.onCancel);
}

void NumberInputParams::onSet(float value) {
    g_psuAppContext.m_numberInputParams.m_input = value;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

void NumberInputParams::onCancel() {
    g_psuAppContext.m_numberInputParams.m_input = NAN;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

float PsuAppContext::numberInput(const char *label, Unit unit, float min, float max, float value) {
    m_inputLabel = label[0] ? label : nullptr;

    m_numberInputParams.m_options = NumericKeypadOptions();

    m_numberInputParams.m_options.editValueUnit = unit;
    m_numberInputParams.m_options.min = min;
    m_numberInputParams.m_options.enableMinButton();
    m_numberInputParams.m_options.max = max;
    m_numberInputParams.m_options.enableMaxButton();
    m_numberInputParams.m_options.flags.signButtonEnabled = m_numberInputParams.m_options.min < 0;
    m_numberInputParams.m_options.flags.dotButtonEnabled = true;

    m_numberInputParams.m_input = value;

    m_inputReady = false;
    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_NUMBER_INPUT);

    while (!m_inputReady) {
        osDelay(5);
    }

    return m_numberInputParams.m_input;
}

void PsuAppContext::doShowNumberInput() {
    startNumericKeypad(this, m_inputLabel, Value(m_numberInputParams.m_input, m_numberInputParams.m_options.editValueUnit), m_numberInputParams.m_options, m_numberInputParams.onSet, nullptr, m_numberInputParams.onCancel);
}

void IntegerInputParams::onSet(float value) {
    g_psuAppContext.m_integerInputParams.m_input = (int32_t)value;
    g_psuAppContext.m_integerInputParams.canceled = false;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

void IntegerInputParams::onCancel() {
    g_psuAppContext.m_integerInputParams.canceled = true;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

bool PsuAppContext::integerInput(const char *label, int32_t min, int32_t max, int32_t &value) {
    m_inputLabel = label[0] ? label : nullptr;

    m_numberInputParams.m_options = NumericKeypadOptions();

    m_integerInputParams.m_options.editValueUnit = UNIT_UNKNOWN;
    m_integerInputParams.m_options.min = (float)min;
    m_integerInputParams.m_options.enableMinButton();
    m_integerInputParams.m_options.max = (float)max;
    m_integerInputParams.m_options.enableMaxButton();
    m_integerInputParams.m_options.flags.signButtonEnabled = false;
    m_integerInputParams.m_options.flags.dotButtonEnabled = false;

    m_integerInputParams.m_input = value;

    m_inputReady = false;
    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_INTEGER_INPUT);

    while (!m_inputReady) {
        osDelay(5);
    }

    if (!m_integerInputParams.canceled) {
        value = m_integerInputParams.m_input;
        return true;
    }

    return false;
}

void PsuAppContext::doShowIntegerInput() {
    startNumericKeypad(this, m_inputLabel, Value((float)m_integerInputParams.m_input, UNIT_UNKNOWN), m_integerInputParams.m_options, m_integerInputParams.onSet, nullptr, m_integerInputParams.onCancel);
}

bool PsuAppContext::dialogOpen(int *err) {
    if (isGuiThread()) {
        if (!isExternalPageOnStack()) {
            dialogResetDataItemValues();
            pushPage(getExternalAssetsMainPageId());
        }
    } else {
        sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_DIALOG_OPEN);
        if (err) {
            static const int DIALOG_OPEN_TIMEOUT = 100;
            for (int i = 0; i < DIALOG_OPEN_TIMEOUT; i++) {
                if (isExternalPageOnStack()) {
                    return true;
                }
                osDelay(1);
            }
            *err = SCPI_ERROR_TIME_OUT;
            return false;
        }
    }
    return true;
}

DialogActionResult PsuAppContext::dialogAction(uint32_t timeoutMs, const char *&selectedActionName) {
    if (timeoutMs != 0) {
        timeoutMs = millis() + timeoutMs;
        if (timeoutMs == 0) {
            timeoutMs = 1;
        }
    }

    while (
        (timeoutMs == 0 || (int32_t)(millis() - timeoutMs) < 0) &&
        g_externalActionId == ACTION_ID_NONE &&
        isExternalPageOnStack()
    ) {
        osDelay(5);
    }

    if (g_externalActionId != ACTION_ID_NONE) {
        selectedActionName = getActionName(g_externalActionWidgetCursor, g_externalActionId);
        g_externalActionId = ACTION_ID_NONE;
        return DIALOG_ACTION_RESULT_SELECTED_ACTION;
    }

    return isExternalPageOnStack() ? DIALOG_ACTION_RESULT_TIMEOUT : DIALOG_ACTION_RESULT_EXIT;
}


void PsuAppContext::dialogResetDataItemValues() {
    for (uint32_t i = 0; i < MAX_NUM_EXTERNAL_DATA_ITEM_VALUES; i++) {
        g_externalDataItemValues[i].value = Value();
        g_externalDataItemValues[i].textIndex = 0;
    }
}

void PsuAppContext::dialogSetDataItemValue(int16_t dataId, Value& value) {
    if (dataId < 0) {
        dataId = -dataId;
    }
    dataId--;
    if ((uint16_t)dataId < MAX_NUM_EXTERNAL_DATA_ITEM_VALUES) {
        g_externalDataItemValues[dataId].value = value;
    }
}

void PsuAppContext::dialogSetDataItemValue(int16_t dataId, const char *str) {
    if (dataId < 0) {
        dataId = -dataId;
    }
    dataId--;
    if ((uint16_t)dataId < MAX_NUM_EXTERNAL_DATA_ITEM_VALUES) {
        int textIndex = g_externalDataItemValues[dataId].textIndex;
        if (textIndex == 1) {
            g_externalDataItemValues[dataId].textIndex = 0;
        } else {
            g_externalDataItemValues[dataId].textIndex = 1;
        }
        stringCopy(g_externalDataItemValues[dataId].text + textIndex, sizeof(g_externalDataItemValues[dataId].text) - textIndex, str);
        g_externalDataItemValues[dataId].value = (const char *)g_externalDataItemValues[dataId].text + textIndex;
    }
}

void PsuAppContext::dialogClose() {
    if (isGuiThread()) {
        if (isExternalPageOnStack()) {
            removePageFromStack(getExternalAssetsMainPageId());
        }
    } else {
        sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_DIALOG_CLOSE);
    }
}

int PsuAppContext::getLongTouchActionHook(const WidgetCursor &widgetCursor) {
    auto action = getWidgetAction(widgetCursor);        
    if (
        action == ACTION_ID_SYS_FRONT_PANEL_LOCK ||
        action == ACTION_ID_SYS_FRONT_PANEL_UNLOCK ||
        action == ACTION_ID_HIDE_OVERLAY_LONG_TOUCH
    ) {
        return action;
    }    
    return AppContext::getLongTouchActionHook(widgetCursor);
}

void MenuInputParams::onSet(int value) {
    g_psuAppContext.m_menuInputParams.m_input = value;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

int PsuAppContext::menuInput(const char *label, MenuType menuType, const char **menuItems) {
    m_inputLabel = label[0] ? label : nullptr;

    m_menuInputParams.m_type = menuType;
    m_menuInputParams.m_items = menuItems;

    m_inputReady = false;
    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_MENU_INPUT);

    while (!m_inputReady) {
        osDelay(5);
    }

    return m_menuInputParams.m_input;
}

void PsuAppContext::doShowMenuInput() {
    showMenu(this, m_inputLabel, m_menuInputParams.m_type, m_menuInputParams.m_items, m_menuInputParams.onSet);
}

int PsuAppContext::select(const char **options, int defaultSelection) {
    m_selectParams.m_options = options;
    m_selectParams.m_defaultSelection = defaultSelection;

    m_inputReady = false;
    sendMessageToGuiThread(GUI_QUEUE_MESSAGE_TYPE_SHOW_SELECT);
    do {
        osDelay(1);
    } while (!isPageOnStack(INTERNAL_PAGE_ID_SELECT_FROM_ENUM));

    while (!m_inputReady && isPageOnStack(INTERNAL_PAGE_ID_SELECT_FROM_ENUM)) {
        osDelay(5);
    }

    if (m_inputReady) {
        return m_selectParams.m_input;
    }
    
    return 0;
}

void SelectParams::enumDefinition(DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
    auto cursor = widgetCursor.cursor;
    if (operation == DATA_OPERATION_GET_VALUE) {
        value = (uint16_t)(cursor + 1);
    } else if (operation == DATA_OPERATION_GET_LABEL) {
        const char *label = g_psuAppContext.m_selectParams.m_options[cursor];
        if (label) {
            value = label;
        }
    }
}

void SelectParams::onSelect(uint16_t value) {
    g_psuAppContext.m_selectParams.m_input = value;
    g_psuAppContext.m_inputReady = true;

    popPage();
}

void PsuAppContext::doShowSelect() {
    pushSelectFromEnumPage(SelectParams::enumDefinition, m_selectParams.m_defaultSelection, nullptr, SelectParams::onSelect, false, true);
}

bool PsuAppContext::canExecuteActionWhenTouchedOutsideOfActivePage(int pageId, int action) {
    if (pageId == PAGE_ID_CH_SETTINGS_LISTS_INSERT_MENU) {
        return action == ACTION_ID_SHOW_CHANNEL_LISTS_DELETE_MENU || action == ACTION_ID_SHOW_CHANNEL_LISTS_FILE_MENU;
    }

    if (pageId == PAGE_ID_CH_SETTINGS_LISTS_DELETE_MENU) {
        return action == ACTION_ID_SHOW_CHANNEL_LISTS_INSERT_MENU || action == ACTION_ID_SHOW_CHANNEL_LISTS_FILE_MENU;
    }

    if (pageId == PAGE_ID_CH_SETTINGS_LISTS_FILE_MENU) {
        return action == ACTION_ID_SHOW_CHANNEL_LISTS_INSERT_MENU || action == ACTION_ID_SHOW_CHANNEL_LISTS_DELETE_MENU;
    }

    return false;
}

void PsuAppContext::pageRenderCustom(int i, WidgetCursor &widgetCursor) {
    if (getActivePageId() == PAGE_ID_TOUCH_CALIBRATION_YES_NO || getActivePageId() == PAGE_ID_TOUCH_CALIBRATION_YES_NO_CANCEL) {
		auto touchEvent = touch::getLastEvent();
        if (touchEvent.type == EVENT_TYPE_TOUCH_DOWN || touchEvent.type == EVENT_TYPE_TOUCH_MOVE) {
            int x = MIN(MAX(touchEvent.x, 1), display::getDisplayWidth() - 2);
            int y = MIN(MAX(touchEvent.y, 1), display::getDisplayHeight() - 2);
            display::setColor(255, 255, 255);
            display::fillRect(x - 1, y - 1, x + 1, y + 1);
        }
    } else if (getActivePageId() == PAGE_ID_TOUCH_TEST) {
		if (g_findCallback == nullptr) {
			if (get(widgetCursor, DATA_ID_TOUCH_CALIBRATED_PRESSED).getInt()) {
				int x = MIN(MAX(get(widgetCursor, DATA_ID_TOUCH_CALIBRATED_X).getInt(), 1), display::getDisplayWidth() - 2);
				int y = MIN(MAX(get(widgetCursor, DATA_ID_TOUCH_CALIBRATED_Y).getInt(), 1), display::getDisplayHeight() - 2);
				display::setColor(0, 0, 255);
				display::fillRect(x - 1, y - 1, x + 1, y + 1);
			}

			if (get(widgetCursor, DATA_ID_TOUCH_FILTERED_PRESSED).getInt()) {
				int x = MIN(MAX(get(widgetCursor, DATA_ID_TOUCH_FILTERED_X).getInt(), 1), display::getDisplayWidth() - 2);
				int y = MIN(MAX(get(widgetCursor, DATA_ID_TOUCH_FILTERED_Y).getInt(), 1), display::getDisplayHeight() - 2);
				display::setColor(0, 255, 0);
				display::fillRect(x - 1, y - 1, x + 1, y + 1);
			}
		}
    }
}

////////////////////////////////////////////////////////////////////////////////

bool isChannelCalibrationsDone() {
    for (int i = 0; i < CH_NUM; ++i) {
        Channel &channel = Channel::get(i);
        if (channel.isOk() && !channel.isCalibrationExists()) {
            return false;
        }
    }
    return true;
}

bool isDateTimeSetupDone() {
    return persist_conf::devConf.dateValid && persist_conf::devConf.timeValid;
}

void dateTimeYes() {
	WidgetCursor widgetCursor;
    executeAction(widgetCursor, ACTION_ID_SHOW_SYS_SETTINGS_DATE_TIME);
}

void dateTimeNo() {
    persist_conf::setSkipDateTimeSetup(1);
}

void ethernetYes() {
	WidgetCursor widgetCursor;
	executeAction(widgetCursor, ACTION_ID_SHOW_SYS_SETTINGS_ETHERNET);
}

void ethernetNo() {
    persist_conf::setSkipEthernetSetup(1);
}

bool showSetupWizardQuestion() {
    if (!g_showSetupWizardQuestionCalled) {
        g_showSetupWizardQuestionCalled = true;
        
        g_skipChannelCalibrations = persist_conf::devConf.skipChannelCalibrations;
        g_skipDateTimeSetup = persist_conf::devConf.skipDateTimeSetup;
        g_skipEthernetSetup = persist_conf::devConf.skipEthernetSetup;
    }


#if defined(EEZ_PLATFORM_STM32)

#if OPTION_ETHERNET
    if (!g_skipEthernetSetup) {
        g_skipEthernetSetup = 1;
        if (!persist_conf::isEthernetEnabled()) {
            yesNoLater("Do you want to setup ethernet?", ethernetYes, ethernetNo);
            return true;
        }
    }
#endif // OPTION_ETHERNET

    if (!g_skipDateTimeSetup) {
        g_skipDateTimeSetup = 1;
        if (!isDateTimeSetupDone()) {
            yesNoLater("Do you want to set date and time?", dateTimeYes, dateTimeNo);
            return true;
        }
    }

#endif // EEZ_PLATFORM_STM32

    return false;
}

static int g_iChannelSetValue;

void changeValue(Channel &channel, const Value &value, float minValue, float maxValue, float defValue, void (*onSetValue)(float)) {
    NumericKeypadOptions options;

	options.slotIndex = channel.slotIndex;
	options.subchannelIndex = channel.subchannelIndex;

    options.editValueUnit = value.getUnit();

    options.min = minValue;
    options.max = maxValue;
    options.def = defValue;

    options.enableMaxButton();
    options.enableDefButton();
    options.flags.signButtonEnabled = true;
    options.flags.dotButtonEnabled = true;

    startNumericKeypad(0, value, options, onSetValue, 0, 0);
}

void onSetVoltageLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setVoltageLimit(channel, limit);
    popPage();
    g_psuAppContext.infoMessage("Voltage limit changed!");
}

void changeVoltageLimit(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getUMin(channel);
    float maxLimit = channel_dispatcher::getUMax(channel);
    float defLimit = channel_dispatcher::getUMax(channel);
    changeValue(channel,
                MakeValue(channel_dispatcher::getULimit(channel), UNIT_VOLT),
                minLimit, maxLimit, defLimit, onSetVoltageLimit);
}

void onSetCurrentLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setCurrentLimit(channel, limit);
    popPage();
	g_psuAppContext.infoMessage("Current limit changed!");
}

void changeCurrentLimit(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getIMin(channel);
    float maxLimit = channel_dispatcher::getIMax(channel);
    float defLimit = channel_dispatcher::getIMax(channel);
    changeValue(channel,
                MakeValue(channel_dispatcher::getILimit(channel), UNIT_AMPER),
                minLimit, maxLimit, defLimit, onSetCurrentLimit);
}

void onSetPowerLimit(float limit) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setPowerLimit(channel, limit);
    popPage();
	g_psuAppContext.infoMessage("Power limit changed!");
}

void changePowerLimit(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLimit = channel_dispatcher::getPowerMinLimit(channel);
    float maxLimit = channel_dispatcher::getPowerMaxLimit(channel);
    float defLimit = channel_dispatcher::getPowerDefaultLimit(channel);
    changeValue(channel,
                MakeValue(channel_dispatcher::getPowerLimit(channel), UNIT_WATT),
                minLimit, maxLimit, defLimit, onSetPowerLimit);
}

void onSetPowerTripLevel(float level) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setOppParameters(channel, channel.prot_conf.flags.p_state ? 1 : 0, level, channel.prot_conf.p_delay);
    popPage();
	g_psuAppContext.infoMessage("Power protection level changed!");
}

void changePowerTripLevel(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLevel = channel_dispatcher::getOppMinLevel(channel);
    float maxLevel = channel_dispatcher::getOppMaxLevel(channel);
    float defLevel = channel_dispatcher::getOppDefaultLevel(channel);
    changeValue(channel,
        MakeValue(channel_dispatcher::getPowerProtectionLevel(channel), UNIT_WATT),
        minLevel, maxLevel, defLevel, onSetPowerTripLevel);
}

void onSetPowerTripDelay(float delay) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setOppParameters(channel, channel.prot_conf.flags.p_state ? 1 : 0, channel_dispatcher::getPowerProtectionLevel(channel), delay);
    popPage();
	g_psuAppContext.infoMessage("Power protection delay changed!");
}

void changePowerTripDelay(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minDelay = channel.params.OPP_MIN_DELAY;
    float maxDelay = channel.params.OPP_MAX_DELAY;
    float defaultDelay = channel.params.OPP_DEFAULT_DELAY;
    changeValue(channel,
        MakeValue(channel.prot_conf.p_delay, UNIT_SECOND),
        minDelay, maxDelay, defaultDelay, onSetPowerTripDelay);
}

void onSetTemperatureTripLevel(float level) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setOtpParameters(channel, temperature::getChannelSensorState(&channel) ? 1 : 0, level, temperature::getChannelSensorDelay(&channel));
    popPage();
	g_psuAppContext.infoMessage("Temperature protection level changed!");
}

void changeTemperatureTripLevel(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minLevel = OTP_AUX_MIN_LEVEL;
    float maxLevel = OTP_AUX_MAX_LEVEL;
    float defLevel = OTP_AUX_DEFAULT_LEVEL;
    changeValue(channel,
        MakeValue(temperature::getChannelSensorLevel(&channel), UNIT_CELSIUS),
        minLevel, maxLevel, defLevel, onSetTemperatureTripLevel);
}

void onSetTemperatureTripDelay(float delay) {
    Channel &channel = Channel::get(g_iChannelSetValue);
    channel_dispatcher::setOtpParameters(channel, temperature::getChannelSensorState(&channel) ? 1 : 0, temperature::getChannelSensorLevel(&channel), delay);
    popPage();
	g_psuAppContext.infoMessage("Temperature protection delay changed!");
}

void changeTemperatureTripDelay(int iChannel) {
    g_iChannelSetValue = iChannel;
    Channel &channel = Channel::get(iChannel);
    float minDelay = OTP_AUX_MIN_DELAY;
    float maxDelay = OTP_AUX_MAX_DELAY;
    float defaultDelay = OTP_CH_DEFAULT_DELAY;
    changeValue(channel,
        MakeValue(temperature::getChannelSensorDelay(&channel), UNIT_SECOND),
        minDelay, maxDelay, defaultDelay, onSetTemperatureTripDelay);
}

void show_ovp_settings(int iChannel) {
	selectChannel(&Channel::get(iChannel));
	pushPage(PAGE_ID_CH_SETTINGS_PROT_OVP);
}

void show_ocp_settings(int iChannel) {
	selectChannel(&Channel::get(iChannel));
	pushPage(PAGE_ID_CH_SETTINGS_PROT_OCP);
}

void show_opp_settings(int iChannel) {
	selectChannel(&Channel::get(iChannel));
	pushPage(PAGE_ID_CH_SETTINGS_PROT_OPP);
}

void show_otp_settings(int iChannel) {
	selectChannel(&Channel::get(iChannel));
	pushPage(PAGE_ID_CH_SETTINGS_PROT_OTP);
}

void psuErrorMessage(const Cursor cursor, Value value, void (*ok_callback)()) {
    if (value.getType() == VALUE_TYPE_SCPI_ERROR) {
        int iChannel = cursor >= 0 ? cursor : (g_channel ? g_channel->channelIndex : 0);
        Channel &channel = Channel::get(iChannel);
        if (value.getScpiError() == SCPI_ERROR_VOLTAGE_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getULimit(channel) < channel_dispatcher::getUMaxLimit(channel)) {
                if (ok_callback) {
                    ok_callback();
                }
				g_psuAppContext.errorMessageWithAction(value, changeVoltageLimit, "Change voltage limit", iChannel);
                return;
            }
        } else if (value.getScpiError() == SCPI_ERROR_CURRENT_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getILimit(channel) < channel_dispatcher::getIMaxLimit(channel)) {
                if (ok_callback) {
                    ok_callback();
                }
				g_psuAppContext.errorMessageWithAction(value, changeCurrentLimit, "Change current limit", iChannel);
                return;
            }
        } else if (value.getScpiError() == SCPI_ERROR_POWER_LIMIT_EXCEEDED || value.getScpiError() == SCPI_ERROR_MODULE_TOTAL_POWER_LIMIT_EXCEEDED) {
            if (channel_dispatcher::getPowerLimit(channel) < channel_dispatcher::getPowerMaxLimit(channel)) {
                if (ok_callback) {
                    ok_callback();
                }
				g_psuAppContext.errorMessageWithAction(value, changePowerLimit, "Change power limit", iChannel);
                return;
            }
        }
	} else if (value.getType() == VALUE_TYPE_EVENT_MESSAGE) {
		if (value.getFirstInt16() == event_queue::EVENT_ERROR_CH_OVP_TRIPPED) {
			if (ok_callback) {
				ok_callback();
			}
			g_psuAppContext.errorMessageWithAction(value, show_ovp_settings, "OVP Settings", value.getSecondInt16());
			return;

		} else if (value.getFirstInt16() == event_queue::EVENT_ERROR_CH_OCP_TRIPPED) {
			if (ok_callback) {
				ok_callback();
			}
			g_psuAppContext.errorMessageWithAction(value, show_ocp_settings, "OCP Settings", value.getSecondInt16());
			return;

		} else if (value.getFirstInt16() == event_queue::EVENT_ERROR_CH_OPP_TRIPPED) {
			if (ok_callback) {
				ok_callback();
			}
			g_psuAppContext.errorMessageWithAction(value, show_opp_settings, "OPP Settings", value.getSecondInt16());
			return;

		} else if (value.getFirstInt16() == event_queue::EVENT_ERROR_CH_OTP_TRIPPED) {
			if (ok_callback) {
				ok_callback();
			}
			g_psuAppContext.errorMessageWithAction(value, show_otp_settings, "OTP Settings", value.getSecondInt16());
			return;
		}
	}

    if (ok_callback) {
        ok_callback();
    }

	g_psuAppContext.errorMessage(value);
}

////////////////////////////////////////////////////////////////////////////////

WidgetCursor g_focusCursor;
int16_t g_focusDataId = DATA_ID_CHANNEL_U_EDIT;
Value g_focusEditValue;

} // gui
} // psu
namespace gui {
using namespace psu::gui;

void setFocusCursor(const WidgetCursor &widgetCursor, int16_t dataId) {
    g_setFocusCursor = true;
    g_focusCursorToSet = widgetCursor;
	g_focusDataIdToSet = dataId;
}

} // namespace gui
namespace psu {
namespace gui {

bool isFocusChanged() {
    return g_focusEditValue.getType() != VALUE_TYPE_UNDEFINED;
}

////////////////////////////////////////////////////////////////////////////////

#if OPTION_ENCODER

static bool g_isEncoderEnabledInActivePage;
uint32_t g_focusEditValueChangedTime;

static bool isEncoderEnabledForWidget(const WidgetCursor &widgetCursor) {
    auto action = getWidgetAction(widgetCursor);        
    return (g_psuAppContext.isWidgetActionEnabled(widgetCursor) && (action == ACTION_ID_EDIT || action == ACTION_ID_SCROLL)) || widgetCursor.widget->type == WIDGET_TYPE_INPUT;
}

static bool g_focusCursorIsEnabled;
static int16_t g_focusCursorAction;

static void isEnabledFocusCursorStep() {
    const WidgetCursor& widgetCursor = g_widgetCursor;

    if (isEncoderEnabledForWidget(widgetCursor)) {
        if (g_focusCursor == widgetCursor && g_focusDataId == widgetCursor.widget->data) {
            g_focusCursorIsEnabled = true;
            auto action = getWidgetAction(widgetCursor);        
            g_focusCursorAction = action;
        }
    }
}

static bool isEnabledFocusCursor(const WidgetCursor& cursor, int16_t dataId) {
    g_focusCursorIsEnabled = false;
    g_focusCursorAction = ACTION_ID_NONE;
    forEachWidget(isEnabledFocusCursorStep);
    return g_focusCursorIsEnabled;
}

void isEncoderEnabledInActivePageCheckWidget() {
    const WidgetCursor& widgetCursor = g_widgetCursor;

    if (widgetCursor.isPage()) {
        g_isEncoderEnabledInActivePage = false;
    } else if (isEncoderEnabledForWidget(widgetCursor)) {
        g_isEncoderEnabledInActivePage = true;
    }
}

static void testIsEncoderEnabledInActivePage() {
    // encoder is enabled if active page contains widget with "edit" action
    g_isEncoderEnabledInActivePage = false;
    forEachWidget(isEncoderEnabledInActivePageCheckWidget);
	
	if (!isEnabledFocusCursor(g_focusCursor, g_focusDataId)) {
		moveToNextFocusCursor();
	}
}

bool isEncoderEnabledInActivePage() {
    return g_isEncoderEnabledInActivePage;
}

////////////////////////////////////////////////////////////////////////////////

static void doUnlockFrontPanel() {
    popPage();

    psu::persist_conf::lockFrontPanel(false);
	g_psuAppContext.infoMessage("Front panel is unlocked!");
}

static void checkPasswordToUnlockFrontPanel() {
    checkPassword("Password: ", psu::persist_conf::devConf.systemPassword, doUnlockFrontPanel);
}

void lockFrontPanel() {
    psu::persist_conf::lockFrontPanel(true);
	g_psuAppContext.infoMessage("Front panel is locked!");
}

void unlockFrontPanel() {
    if (strlen(psu::persist_conf::devConf.systemPassword) > 0) {
        checkPasswordToUnlockFrontPanel();
    } else {
        psu::persist_conf::lockFrontPanel(false);
		g_psuAppContext.infoMessage("Front panel is unlocked!");
    }
}

bool isFrontPanelLocked() {
    return psu::g_rlState != psu::RL_STATE_LOCAL;
}

////////////////////////////////////////////////////////////////////////////////

Image g_customLogo;

void loadCustomLogo() {
#if CONF_OPTION_FPGA
    static const uint32_t CONF_MAX_LOGO_WIDTH = 800;
    static const uint32_t CONF_MAX_LOGO_HEIGHT = 240;
#else
    static const uint32_t CONF_MAX_LOGO_WIDTH = 480;
    static const uint32_t CONF_MAX_LOGO_HEIGHT = 136;
#endif

#ifdef EEZ_PLATFORM_STM32
    static bool loaded;
    loaded = false;
    if (g_isBooted && !isLowPriorityThread()) {
        sendMessageToLowPriorityThread(THREAD_MESSAGE_LOAD_CUSTOM_LOGO);
        while (!loaded) {
        	osDelay(1);
        }
        return;
    }
#endif

    if (imageDecode("/logo.jpg", &g_customLogo) != IMAGE_DECODE_OK || g_customLogo.width > CONF_MAX_LOGO_WIDTH || g_customLogo.height > CONF_MAX_LOGO_HEIGHT) {
        if (imageDecode("/logo.bmp", &g_customLogo) != IMAGE_DECODE_OK || g_customLogo.width > CONF_MAX_LOGO_WIDTH || g_customLogo.height > CONF_MAX_LOGO_HEIGHT) {
            g_customLogo.pixels = nullptr;
        }
    }

#ifdef EEZ_PLATFORM_STM32
    loaded = true;
#endif
}

////////////////////////////////////////////////////////////////////////////////

void showWelcomePage() {
    g_autoRecallExecuted = false;
    loadCustomLogo();
    showPage(PAGE_ID_WELCOME);
}

void showEnteringStandbyPage() {
    showPage(PAGE_ID_ENTERING_STANDBY);
}

void showStandbyPage() {
    showPage(PAGE_ID_STANDBY);
}

void showSavingPage() {
    showPage(PAGE_ID_SAVING);
}

void showShutdownPage() {
    showPage(PAGE_ID_SHUTDOWN);
}

////////////////////////////////////////////////////////////////////////////////

Value g_alertMessage2;
Value g_alertMessage3;

////////////////////////////////////////////////////////////////////////////////

void yesNoLater(const char *message, void (*yes_callback)(), void (*no_callback)(), void (*later_callback)()) {
    set(WidgetCursor(), DATA_ID_ALERT_MESSAGE, Value(message));

    g_psuAppContext.m_dialogYesCallback = yes_callback;
    g_psuAppContext.m_dialogNoCallback = no_callback;
    g_psuAppContext.m_dialogLaterCallback = later_callback;

    pushPage(PAGE_ID_YES_NO_LATER);
}

void areYouSure(void (*yes_callback)()) {
	g_psuAppContext.yesNoDialog(PAGE_ID_YES_NO, "Are you sure?", yes_callback, 0, 0);
}

void areYouSureWithMessage(const char *message, void (*yes_callback)(), void (*no_callback)(), void (*cancel_callback)()) {
	g_psuAppContext.yesNoDialog(PAGE_ID_ARE_YOU_SURE_WITH_MESSAGE, message, yes_callback, no_callback, cancel_callback);
}

////////////////////////////////////////////////////////////////////////////////

void pushSelectFromEnumPage(
    AppContext *appContext,
    EnumDefinition enumDefinition,
    uint16_t currentValue,
    bool (*disabledCallback)(uint16_t value),
    void (*onSet)(uint16_t),
    bool smallFont,
    bool showRadioButtonIcon
) {
	SelectFromEnumPage::pushSelectFromEnumPage(appContext, g_enumDefinitions[enumDefinition], currentValue, disabledCallback, onSet, smallFont, showRadioButtonIcon);
}


void showMainPage() {
    showPage(PAGE_ID_MAIN);
}

void goBack() {
    if (getNumPagesOnStack() > 1) {
        action_show_previous_page();
    } else if (getActivePageId() != PAGE_ID_MAIN) {
        showMainPage();
    } else if (persist_conf::isMaxView()) {
        action_toggle_channels_max_view();
    }    
}

void takeScreenshot() {
    using namespace scpi;
    if (!g_screenshotGenerating) {
        g_screenshotGenerating = true;
        sendMessageToLowPriorityThread(THREAD_MESSAGE_SCREENSHOT);
    }
}

////////////////////////////////////////////////////////////////////////////////

static int g_findNextFocusCursorState = 0; 
static WidgetCursor g_nextFocusCursor = 0;
static uint16_t g_nextFocusDataId = DATA_ID_CHANNEL_U_EDIT;

void findNextFocusCursor() {
    const WidgetCursor& widgetCursor = g_widgetCursor;

    if (isEncoderEnabledForWidget(widgetCursor)) {
        if (g_findNextFocusCursorState == 0) {
            g_nextFocusCursor = widgetCursor;
            g_nextFocusDataId = widgetCursor.widget->data;
            g_findNextFocusCursorState = 1;
        }

        if (g_findNextFocusCursorState == 1) {
            if (g_focusCursor == widgetCursor && g_focusDataId == widgetCursor.widget->data) {
                g_findNextFocusCursorState = 2;
            }
        } else if (g_findNextFocusCursorState == 2) {
            g_nextFocusCursor = widgetCursor;
            g_nextFocusDataId = widgetCursor.widget->data;
            g_findNextFocusCursorState = 3;
        }
    }
}

static void moveToNextFocusCursor() {
    g_findNextFocusCursorState = 0;
    forEachWidget(findNextFocusCursor);
    if (g_findNextFocusCursorState > 0) {
        g_focusCursor = g_nextFocusCursor;
        g_focusDataId = g_nextFocusDataId;
    }
}

bool onEncoderConfirmation() {
    if (edit_mode::isActive(&g_psuAppContext) && !edit_mode::isInteractiveMode() && edit_mode::getEditValue() != edit_mode::getCurrentValue()) {
        edit_mode::nonInteractiveSet();
        return true;
    }
    return false;
}

Unit getCurrentEncoderUnit() {
    Page *activePage = getActivePage();
    if (activePage) {
        Unit unit = activePage->getEncoderUnit();
        if (unit != UNIT_UNKNOWN) {
            return unit;
        }
    }

    auto editValue = getEditValue(g_focusCursor, g_focusDataId);
    return editValue.getUnit();
}

void onEncoder(int counter, bool clicked) {
    if (g_shutdownInProgress || isFrontPanelLocked()) {
        return;
    }

    uint32_t tickCount = millis();

    // wait for confirmation of changed value ...
    if (isFocusChanged() && tickCount - g_focusEditValueChangedTime >= ENCODER_CHANGE_TIMEOUT * 1000L) {
        // ... on timeout discard changed value
        g_focusEditValue = Value();
    }

    int activePageId = getActivePageId();
    Page *activePage = getActivePage();

    if (counter != 0) {
#if defined(EEZ_PLATFORM_SIMULATOR)
        if (g_focusCursorAction == ACTION_ID_SCROLL) {
            counter = -counter;
        }
#endif

        bool encoderEnabled = isEncoderEnabledInActivePage();
        if (encoderEnabled) {
            Value value;
            if (persist_conf::devConf.encoderConfirmationMode && g_focusEditValue.getType() != VALUE_TYPE_UNDEFINED) {
                value = g_focusEditValue;
            } else {
                value = getEditValue(g_focusCursor, g_focusDataId);
            }

            float min = getMin(g_focusCursor, g_focusDataId).getFloat();
            float max = getMax(g_focusCursor, g_focusDataId).getFloat();
            
            float step = edit_mode_step::getEncoderStepValue();

            float newValue = roundPrec(value.toFloat() + counter * step, step);
            if (getAllowZero(g_focusCursor, g_focusDataId) && newValue < value.getFloat() && newValue < min) {
                newValue = 0;
            } else {
                newValue = clamp(newValue, min, max);
            }            

            Value limitValue = getLimit(g_focusCursor, g_focusDataId);
            if (limitValue.getType() == VALUE_TYPE_FLOAT) {
                float limit = limitValue.getFloat();
                if (newValue > limit && value.getFloat() < limit) {
                    newValue = limit;
                }
            }

            if (persist_conf::devConf.encoderConfirmationMode) {
                g_focusEditValue = MakeValue(newValue, value.getUnit());
                g_focusEditValueChangedTime = millis();
            } else {
                Value result = set(g_focusCursor, g_focusDataId, MakeValue(newValue, value.getUnit()));
                if (result.getType() == VALUE_TYPE_SCPI_ERROR) {
                    psuErrorMessage(g_focusCursor.cursor, result);
                }
            }
        } else {
            mcu::encoder::enableAcceleration(false);
        }

        if (activePageId == PAGE_ID_EDIT_MODE_KEYPAD || activePageId == PAGE_ID_NUMERIC_KEYPAD) {
            ((eez::psu::gui::NumericKeypad *)getActiveKeypad())->onEncoder(counter);
        }
#if defined(EEZ_PLATFORM_SIMULATOR)
        else if (activePageId == PAGE_ID_FRONT_PANEL_NUMERIC_KEYPAD) {
            ((eez::psu::gui::NumericKeypad *)getActiveKeypad())->onEncoder(counter);
        }
#endif
        else if (activePageId == PAGE_ID_EDIT_MODE_STEP) {
            edit_mode_step::onEncoder(counter);
        } else if (activePageId == PAGE_ID_FILE_MANAGER || activePageId == PAGE_ID_FILE_BROWSER) {
            file_manager::onEncoder(counter);
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_DISPLAY_TEST) {
            if (counter < 0) {
                counter = -counter;
            }
            g_displayTestColorIndex = (g_displayTestColorIndex + counter) % 4;
        } else if (activePage) {
            activePage->onEncoder(counter);
        }
    }

    if (clicked) {
        if (trigger::isInitiated(trigger::SOURCE_MANUAL)) {
            action_trigger_generate_manual();
        } else if (activePageId == PAGE_ID_SYS_SETTINGS_DISPLAY_TEST) {
            popPage();
        } else if (isEncoderEnabledInActivePage()) {
            if (isFocusChanged()) {
                // confirmation
                Value result = set(g_focusCursor, g_focusDataId, g_focusEditValue);
                if (result.getType() == VALUE_TYPE_SCPI_ERROR) {
                    psuErrorMessage(g_focusCursor.cursor, result);
                } else {
                    g_focusEditValue = Value();
                }
            } else if (!onEncoderConfirmation()) {
                moveToNextFocusCursor();
            }

            sound::playClick();
        } else {
            int activePageId = getActivePageId();
            if (activePageId == PAGE_ID_EDIT_MODE_KEYPAD || activePageId == PAGE_ID_NUMERIC_KEYPAD) {
                ((NumericKeypad *)getActiveKeypad())->onEncoderClicked();
            }
#if defined(EEZ_PLATFORM_SIMULATOR)
            else if (activePageId == PAGE_ID_FRONT_PANEL_NUMERIC_KEYPAD) {
                ((NumericKeypad *)getActiveKeypad())->onEncoderClicked();
            }
#endif
            else if (activePage) {
                activePage->onEncoderClicked();
            }
        }
    }
}

#endif

////////////////////////////////////////////////////////////////////////////////

static void channelInitiateTrigger() {
    int err = trigger::initiate();
    if (err != SCPI_RES_OK) {
        if (!(err >= SCPI_ERROR_EXTERNAL_VOLTAGE_ON_CH1_DETECTED && err <= SCPI_ERROR_EXTERNAL_VOLTAGE_ON_CH6_DETECTED)) {
            psuErrorMessage(g_toggleOutputWidgetCursor.cursor, MakeScpiErrorValue(err));
        }
    }
}

void channelReinitiateTrigger() {
    trigger::abort();
    channelInitiateTrigger();
}

void doChannelToggleOutput() {
    Channel &channel = *g_channel;
    bool triggerModeEnabled =
        (channel_dispatcher::getVoltageTriggerMode(channel) != TRIGGER_MODE_FIXED ||
        channel_dispatcher::getCurrentTriggerMode(channel) != TRIGGER_MODE_FIXED) && !channel.isRemoteProgrammingEnabled();

    if (channel.isOutputEnabled()) {
        if (triggerModeEnabled) {
            trigger::abort();
        }

        int err;
        if (!channel_dispatcher::outputEnable(channel, false, &err)) {
            psuErrorMessage(channel.channelIndex, MakeScpiErrorValue(err));
        }
    } else {
        if (triggerModeEnabled) {
            if (trigger::isIdle()) {
                g_toggleOutputWidgetCursor = getFoundWidgetAtDown();
                channelInitiateTrigger();
            } else if (trigger::isInitiated()) {
                trigger::abort();
            } else {
				g_psuAppContext.yesNoDialog(PAGE_ID_YES_NO_L, "Trigger is active. Re-initiate trigger?", channelReinitiateTrigger, 0, 0);
            }
        } else {
            int err;
            if (!channel_dispatcher::outputEnable(channel, true, &err)) {
                if (err >= SCPI_ERROR_EXTERNAL_VOLTAGE_ON_CH1_DETECTED && err <= SCPI_ERROR_EXTERNAL_VOLTAGE_ON_CH6_DETECTED && persist_conf::isOutputProtectionMeasureEnabled()) {
                    // do not show error
                } else {
                    psuErrorMessage(channel.channelIndex, MakeScpiErrorValue(err));
                }
            }
        }
    }
}

void triggerManually() {
    if (trigger::isActive() || trigger::isInitiated() || trigger::isTriggered()) {
        trigger::abort();
    } else if (trigger::isIdle()) {
        channelInitiateTrigger();
    }
}

void channelCalibrationsYes() {
	WidgetCursor widgetCursor;
	executeAction(widgetCursor, ACTION_ID_SHOW_CH_SETTINGS_CAL);
}

void channelCalibrationsNo() {
    persist_conf::setSkipChannelCalibrations(persist_conf::devConf.skipChannelCalibrations | (1 << g_channel->channelIndex));
    doChannelToggleOutput();
}

void channelToggleOutput() {
    selectChannelByCursor();
    Channel &channel = *g_channel;
    int channelIndex;
    if (channel_dispatcher::isTripped(channel, channelIndex)) {
        // auto clearTrip = [] (int channelIndex) {
        //     Channel &channel = Channel::get(channelIndex);
        //     channel_dispatcher::clearProtection(channel);
        //     if (temperature::sensors[temp_sensor::AUX].isTripped()) {
        //         temperature::sensors[temp_sensor::AUX].clearProtection();
        //     }
        //     channelToggleOutput();
        // };
        if (temperature::sensors[temp_sensor::AUX].isTripped()) {
            // errorMessageWithAction("AUX temp. sensor is tripped!", clearTrip, "Clear", channelIndex);
			g_psuAppContext.errorMessage("AUX temp. sensor is tripped!");
        } else {
            // errorMessageWithAction("Channel is tripped!", clearTrip, "Clear", channelIndex);
			g_psuAppContext.errorMessage("Channel is tripped!");
        }
    } else {
#if defined(EEZ_PLATFORM_STM32)
        if (!channel.isOutputEnabled() && !channel.isCalibrationExists()) {
            if (!(g_skipChannelCalibrations & (1 << channel.channelIndex))) {
                g_skipChannelCalibrations |= 1 << channel.channelIndex;
                yesNoLater("Do you want to calibrate channel?", channelCalibrationsYes, channelCalibrationsNo, doChannelToggleOutput);
                return;
            }
        }
#endif // EEZ_PLATFORM_STM32
        doChannelToggleOutput();
    }
}

void selectChannelByCursor() {
	auto cursor = getFoundWidgetAtDown().cursor;
    if (cursor >= 0 && cursor < CH_NUM) {
        selectChannel(&Channel::get(cursor));
    }
}

void selectChannel(Channel *channel) {
    g_channel = channel;
    if (g_channel) {
        hmi::selectSlot(g_channel->slotIndex);
        g_channelIndex = g_channel->channelIndex;
    } else {
        g_channelIndex = -1;
    }
}

bool isDefaultViewVertical() {
    return persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_NUMERIC || persist_conf::devConf.channelsViewMode == CHANNELS_VIEW_MODE_VERT_BAR;
}

} // namespace gui
} // namespace psu
} // namespace eez

////////////////////////////////////////////////////////////////////////////////

namespace eez {
namespace gui {

void stateManagmentHook() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    g_frontPanelAppContext.stateManagment();
#endif
    g_psuAppContext.stateManagment();

	scripting::flowTick();

	WATCHDOG_RESET(WATCHDOG_GUI_THREAD);
}

static bool styleGetSmallerFontHook(font::Font &font) {
    if (font.fontData == getFontData(FONT_ID_OSWALD48)) {
        font.fontData = getFontData(FONT_ID_OSWALD38);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD38)) {
        font.fontData = getFontData(FONT_ID_OSWALD24);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD24)) {
        font.fontData = getFontData(FONT_ID_OSWALD20);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD20)) {
        font.fontData = getFontData(FONT_ID_OSWALD17);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD17)) {
        font.fontData = getFontData(FONT_ID_OSWALD14);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD14)) {
        font.fontData = getFontData(FONT_ID_OSWALD12);
    } else if (font.fontData == getFontData(FONT_ID_OSWALD12)) {
        font.fontData = getFontData(FONT_ID_ROBOTO_CONDENSED_REGULAR);
    } else {
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

using namespace eez::psu::gui;

static EventQueuePage g_EventQueuePage;
static ChSettingsOvpProtectionPage g_ChSettingsOvpProtectionPage;
static ChSettingsOcpProtectionPage g_ChSettingsOcpProtectionPage;
static ChSettingsOppProtectionPage g_ChSettingsOppProtectionPage;
static ChSettingsOtpProtectionPage g_ChSettingsOtpProtectionPage;
static ChSettingsAdvOptionsPage g_ChSettingsAdvOptionPage;
static ChSettingsAdvRangesPage g_ChSettingsAdvRangesPage;
static ChSettingsAdvViewPage g_ChSettingsAdvViewPage;
static ChSettingsTriggerPage g_ChSettingsTriggerPage;
static ChSettingsListsPage g_ChSettingsListsPage;
static ChSettingsCalibrationEditPage g_ChSettingsCalibrationEditPage;
static ChSettingsCalibrationViewPage g_ChSettingsCalibrationViewPage;
static SysSettingsDateTimePage g_SysSettingsDateTimePage;
#if OPTION_ETHERNET
static SysSettingsEthernetPage g_SysSettingsEthernetPage;
static SysSettingsEthernetStaticPage g_SysSettingsEthernetStaticPage;
static SysSettingsMqttPage g_SysSettingsMqttPage;
#endif
static SysSettingsProtectionsPage g_SysSettingsProtectionsPage;
static SysSettingsTriggerPage g_SysSettingsTriggerPage;
static SysSettingsIOPinsPage g_SysSettingsIOPinsPage;
static SysSettingsTemperaturePage g_SysSettingsTemperaturePage;
static SysSettingsSoundPage g_SysSettingsSoundPage;
#if OPTION_ENCODER
static SysSettingsEncoderPage g_SysSettingsEncoderPage;
#endif
static SysSettingsCouplingPage g_sysSettingsCouplingPage;
static UserProfilesPage g_UserProfilesPage;
static file_manager::FileBrowserPage g_FileBrowserPage;
static SysSettingsRampAndDelayPage g_sysSettingsRampAndDelayPage;
static LabelsAndColorsPage g_LabelsAndColorsPage;

////////////////////////////////////////////////////////////////////////////////

Page *getPageFromIdHook(int pageId) {
    Page *page = nullptr;

    switch (pageId) {
    case PAGE_ID_EVENT_QUEUE:
        page = &g_EventQueuePage;
        break;
    case PAGE_ID_CH_SETTINGS_PROT_OVP:
        page = &g_ChSettingsOvpProtectionPage;
        break;
    case PAGE_ID_CH_SETTINGS_PROT_OCP:
        page = &g_ChSettingsOcpProtectionPage;
        break;
    case PAGE_ID_CH_SETTINGS_PROT_OPP:
        page = &g_ChSettingsOppProtectionPage;
        break;
    case PAGE_ID_CH_SETTINGS_PROT_OTP:
        page = &g_ChSettingsOtpProtectionPage;
        break;
    case PAGE_ID_CH_SETTINGS_ADV_OPTIONS:
        page = &g_ChSettingsAdvOptionPage;
        break;
    case PAGE_ID_CH_SETTINGS_ADV_RANGES:
        page = &g_ChSettingsAdvRangesPage;
        break;
    case PAGE_ID_SYS_SETTINGS_TRACKING:
        page = &g_sysSettingsTrackingPage;
        break;
    case PAGE_ID_SYS_SETTINGS_COUPLING:
        page = &g_sysSettingsCouplingPage;
        break;
    case PAGE_ID_CH_SETTINGS_ADV_VIEW:
        page = &g_ChSettingsAdvViewPage;
        break;
    case PAGE_ID_CH_SETTINGS_TRIGGER:
        page = &g_ChSettingsTriggerPage;
        break;
    case PAGE_ID_CH_SETTINGS_LISTS:
        page = &g_ChSettingsListsPage;
        break;
    case PAGE_ID_CH_SETTINGS_CALIBRATION_EDIT:
        page = &g_ChSettingsCalibrationEditPage;
        break;
    case PAGE_ID_CH_SETTINGS_CALIBRATION_VIEW:
        page = &g_ChSettingsCalibrationViewPage;
        break;
    case PAGE_ID_SYS_SETTINGS_DATE_TIME:
        page = &g_SysSettingsDateTimePage;
        break;
#if OPTION_ETHERNET
    case PAGE_ID_SYS_SETTINGS_ETHERNET:
        page = &g_SysSettingsEthernetPage;
        break;
    case PAGE_ID_SYS_SETTINGS_ETHERNET_STATIC:
        page = &g_SysSettingsEthernetStaticPage;
        break;
    case PAGE_ID_SYS_SETTINGS_MQTT:
        page = &g_SysSettingsMqttPage;
        break;
#endif
    case PAGE_ID_SYS_SETTINGS_PROTECTIONS:
        page = &g_SysSettingsProtectionsPage;
        break;
    case PAGE_ID_SYS_SETTINGS_TRIGGER:
        page = &g_SysSettingsTriggerPage;
        break;
    case PAGE_ID_SYS_SETTINGS_IO:
        page = &g_SysSettingsIOPinsPage;
        break;
    case PAGE_ID_SYS_SETTINGS_TEMPERATURE:
        page = &g_SysSettingsTemperaturePage;
        break;
    case PAGE_ID_SYS_SETTINGS_SOUND:
        page = &g_SysSettingsSoundPage;
        break;
#if OPTION_ENCODER
    case PAGE_ID_SYS_SETTINGS_ENCODER:
        page = &g_SysSettingsEncoderPage;
        break;
#endif
    case PAGE_ID_USER_PROFILES:
    case PAGE_ID_USER_PROFILE_0_SETTINGS:
    case PAGE_ID_USER_PROFILE_SETTINGS:
        page = &g_UserProfilesPage;
        break;
    case PAGE_ID_FILE_BROWSER:
        page = &g_FileBrowserPage;
        break;
    case PAGE_ID_SYS_SETTINGS_RAMP_AND_DELAY:
        page = &g_sysSettingsRampAndDelayPage;
        break;
    case PAGE_ID_SYS_SETTINGS_LABELS_AND_COLORS:
        page = &g_LabelsAndColorsPage;
        break;
    case PAGE_ID_DLOG_PARAMS:
        page = psu::dlog_view::getParamsPage();
        break;
    case PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR:
        page = function_generator::g_pFunctionGeneratorPage;
        break;
	case PAGE_ID_SYS_SETTINGS_FUNCTION_GENERATOR_SELECT_CHANNELS:
		page = function_generator::g_pFunctionGeneratorSelectChannelsPage;
		break;
	default :
        for (int i = 0; i < NUM_SLOTS; i++) {
            page = g_slots[i]->getPageFromId(pageId);
            if (page) {
                break;
            }
        }
        break;
    }

    if (page) {
        page->pageAlloc();
    }

    return page;
}

int overrideStyleHook(const WidgetCursor &widgetCursor, int styleId) {
    using namespace psu;
    using namespace psu::gui;

    auto action = getWidgetAction(widgetCursor);        

    if (widgetCursor.widget->data == DATA_ID_CHANNEL_DISPLAY_VALUE1 || widgetCursor.widget->data == DATA_ID_CHANNEL_DISPLAY_VALUE2) {
        if (styleId == STYLE_ID_YT_GRAPH_U_DEFAULT || styleId == STYLE_ID_YT_GRAPH_I_DEFAULT) {
            int iChannel = widgetCursor.cursor >= 0 ? widgetCursor.cursor : (g_channel ? g_channel->channelIndex : 0);
            Channel &channel = Channel::get(iChannel);
            if (widgetCursor.widget->data == DATA_ID_CHANNEL_DISPLAY_VALUE1) {
                if (channel.displayValues[0].type == DISPLAY_VALUE_VOLTAGE) {
                    return STYLE_ID_YT_GRAPH_U_DEFAULT;
                } else if (channel.displayValues[0].type == DISPLAY_VALUE_CURRENT) {
                    return STYLE_ID_YT_GRAPH_I_DEFAULT;
                }
            } else {
                if (channel.displayValues[1].type == DISPLAY_VALUE_VOLTAGE) {
                    return STYLE_ID_YT_GRAPH_U_DEFAULT;
                } else if (channel.displayValues[1].type == DISPLAY_VALUE_CURRENT) {
                    return STYLE_ID_YT_GRAPH_I_DEFAULT;
                }
            }
            return STYLE_ID_YT_GRAPH_P_DEFAULT;
        } else if (styleId == STYLE_ID_BAR_GRAPH_U_DEFAULT || styleId == STYLE_ID_BAR_GRAPH_I_DEFAULT) {
            using namespace psu;
            using namespace psu::gui;
            int iChannel = widgetCursor.cursor >= 0 ? widgetCursor.cursor : (g_channel ? g_channel->channelIndex : 0);
            Channel &channel = Channel::get(iChannel);
            if (widgetCursor.widget->data == DATA_ID_CHANNEL_DISPLAY_VALUE1) {
                if (channel.displayValues[0].type == DISPLAY_VALUE_VOLTAGE) {
                    return STYLE_ID_BAR_GRAPH_U_DEFAULT;
                } else if (channel.displayValues[0].type == DISPLAY_VALUE_CURRENT) {
                    return STYLE_ID_BAR_GRAPH_I_DEFAULT;
                }
            } else {
                if (channel.displayValues[1].type == DISPLAY_VALUE_VOLTAGE) {
                    return STYLE_ID_BAR_GRAPH_U_DEFAULT;
                } else if (channel.displayValues[1].type == DISPLAY_VALUE_CURRENT) {
                    return STYLE_ID_BAR_GRAPH_I_DEFAULT;
                }
            }
            return STYLE_ID_BAR_GRAPH_P_DEFAULT;
        }
    } else if (action != ACTION_ID_NONE && !g_psuAppContext.isWidgetActionEnabled(widgetCursor)) {
        if (styleId == STYLE_ID_ENCODER_CURSOR_14_ENABLED) {
            return STYLE_ID_ENCODER_CURSOR_14_DISABLED;
        } else if (styleId == STYLE_ID_ENCODER_CURSOR_14_RIGHT_ENABLED) {
            return STYLE_ID_ENCODER_CURSOR_14_RIGHT_DISABLED;
        } else if (styleId == STYLE_ID_EDIT_VALUE_ACTIVE_M_CENTER) {
            return STYLE_ID_DEFAULT_M_LEFT;
        }
    } else if (styleId == STYLE_ID_CHANNEL_OFF_BUTTON_20_NOT_PROHIBIT) {
        if (persist_conf::isOutputProtectionMeasureEnabled()) {
            return STYLE_ID_CHANNEL_OFF_BUTTON_20_PROHIBIT;
        }
    }
    return styleId;
}

uint16_t overrideStyleColorHook(const WidgetCursor &widgetCursor, const Style *style) {
    if (widgetCursor.widget->type == WIDGET_TYPE_TEXT && widgetCursor.widget->data == DATA_ID_DLOG_VISIBLE_VALUE_LABEL) {
        auto &recording = psu::dlog_view::getRecording();
        int dlogValueIndex = !psu::dlog_view::isMulipleValuesOverlayHeuristic(recording) || psu::persist_conf::devConf.viewFlags.dlogViewLegendViewOption == psu::persist_conf::DLOG_VIEW_LEGEND_VIEW_OPTION_DOCK
            ? recording.selectedValueIndex : psu::dlog_view::getDlogValueIndex(recording, widgetCursor.cursor);
        style = ytDataGetStyle(widgetCursor, DATA_ID_RECORDING, dlogValueIndex);
    }
    return style->color;
}

uint16_t overrideActiveStyleColorHook(const WidgetCursor &widgetCursor, const Style *style) {
    if (widgetCursor.widget->type == WIDGET_TYPE_TEXT && widgetCursor.widget->data == DATA_ID_DLOG_VISIBLE_VALUE_LABEL) {
        auto &recording = psu::dlog_view::getRecording();
        int dlogValueIndex = !psu::dlog_view::isMulipleValuesOverlayHeuristic(recording) || psu::persist_conf::devConf.viewFlags.dlogViewLegendViewOption == psu::persist_conf::DLOG_VIEW_LEGEND_VIEW_OPTION_DOCK
            ? recording.selectedValueIndex : psu::dlog_view::getDlogValueIndex(recording, widgetCursor.cursor);
        style = ytDataGetStyle(widgetCursor, DATA_ID_RECORDING, dlogValueIndex);
    }
    return style->activeColor;
}

uint16_t transformColorHook(uint16_t color) {
    if (color == COLOR_ID_CHANNEL1 || color == COLOR_ID_CHANNEL1_TEXT) {
        if (g_channelIndex >= 0 && g_channelIndex < psu::CH_NUM) {
            auto &channel = psu::Channel::get(g_channelIndex);
            if (channel.color) {
                return color + channel.color - 1;
            } else {
                return color + channel.channelIndex;
            }
        } else if (hmi::g_selectedSlotIndex >= 0 && hmi::g_selectedSlotIndex < NUM_SLOTS) {
            if (hmi::g_selectedSubchannelIndex >= 0) {
                psu::Channel *channel = psu::Channel::getBySlotIndex(hmi::g_selectedSlotIndex, hmi::g_selectedSubchannelIndex);
                if (channel) {
                    if (channel->color) {
                        return color + channel->color - 1;
                    } else {
                        return color + channel->channelIndex;
                    }
                }
            }
            auto slotColor = g_slots[hmi::g_selectedSlotIndex]->getColor();
            if (slotColor) {
                return color + slotColor - 1;
            } else {
                return color + psu::CH_NUM + hmi::g_selectedSlotIndex;
            }
        }
    } else if (color == COLOR_ID_LABELS_AND_COLORS_PAGE_SLOT_COLOR) {
        uint8_t color = LabelsAndColorsPage::getSlotColor(hmi::g_selectedSlotIndex);
        if (color) {
            return COLOR_ID_CHANNEL1 + color - 1;
        } else {
            return COLOR_ID_CHANNEL1 + psu::CH_NUM + hmi::g_selectedSlotIndex;
        }
    } else if (color == COLOR_ID_LABELS_AND_COLORS_PAGE_SLOT_COLOR_TEXT) {
        uint8_t color = LabelsAndColorsPage::getSlotColor(hmi::g_selectedSlotIndex);
        if (color) {
            return COLOR_ID_CHANNEL1_TEXT + color - 1;
        } else {
            return COLOR_ID_CHANNEL1_TEXT + psu::CH_NUM + hmi::g_selectedSlotIndex;
        }
    } else if (color == COLOR_ID_LABELS_AND_COLORS_PAGE_CHANNEL_COLOR) {
        auto &channel = psu::Channel::get(g_channelIndex);
        uint8_t color = LabelsAndColorsPage::getChannelColor(channel.slotIndex, channel.subchannelIndex);
        if (color) {
            return COLOR_ID_CHANNEL1 + color - 1;
        } else {
            return COLOR_ID_CHANNEL1 + g_channelIndex;
        }
    } else if (color == COLOR_ID_LABELS_AND_COLORS_PAGE_CHANNEL_COLOR_TEXT) {
        auto &channel = psu::Channel::get(g_channelIndex);
        uint8_t color = LabelsAndColorsPage::getChannelColor(channel.slotIndex, channel.subchannelIndex);
        if (color) {
            return COLOR_ID_CHANNEL1_TEXT + color - 1;
        } else {
            return COLOR_ID_CHANNEL1_TEXT + g_channelIndex;
        }
    } else if (color == COLOR_ID_PICK_COLOR) {
        return COLOR_ID_CHANNEL1 + LabelsAndColorsPage::g_colorIndex;
    }
    return color;
}

AppContext *getAppContextFromId(int16_t id) {
    if (id == APP_CONTEXT_ID_DEVICE) {
        return &g_psuAppContext;
    }

#if defined(EEZ_PLATFORM_SIMULATOR)
    if (id == APP_CONTEXT_ID_SIMULATOR_FRONT_PANEL) {
        return &g_frontPanelAppContext;
    }
#endif

    return nullptr;
}

void onGuiQueueMessageHook(uint8_t type, int16_t param) {
	if (type == GUI_QUEUE_MESSAGE_MOUSE_DISCONNECTED) {
		mouse::onMouseDisconnected();
	} else if (type == GUI_QUEUE_MESSAGE_FLOW_START) {
		scripting::startFlowScript();
	} else if (type == GUI_QUEUE_MESSAGE_FLOW_STOP) {
		scripting::stopFlowScript();
	} else if (type == GUI_QUEUE_MESSAGE_TYPE_LISTS_PAGE_IMPORT_LIST_FINISHED) {
        g_ChSettingsListsPage.onImportListFinished(param);
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_LISTS_PAGE_EXPORT_LIST_FINISHED) {
        g_ChSettingsListsPage.onExportListFinished(param);
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_USER_PROFILES_PAGE_ASYNC_OPERATION_FINISHED) {
        g_UserProfilesPage.onAsyncOperationFinished(param);
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_TEXT_INPUT) {
        g_psuAppContext.doShowTextInput();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_NUMBER_INPUT) {
        g_psuAppContext.doShowNumberInput();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_INTEGER_INPUT) {
        g_psuAppContext.doShowIntegerInput();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_MENU_INPUT) {
        g_psuAppContext.doShowMenuInput();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_SELECT) {
        g_psuAppContext.doShowSelect();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_DIALOG_OPEN) {
        g_psuAppContext.dialogOpen(nullptr);
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_DIALOG_CLOSE) {
        g_psuAppContext.dialogClose();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_SHOW_ASYNC_OPERATION_IN_PROGRESS) {
        g_psuAppContext.doShowAsyncOperationInProgress();
    } else if (type == GUI_QUEUE_MESSAGE_TYPE_HIDE_ASYNC_OPERATION_IN_PROGRESS) {
        g_psuAppContext.doHideAsyncOperationInProgress();
    } else if (type == GUI_QUEUE_MESSAGE_KEY_DOWN) {
        keyboard::onKeyDown((uint16_t)param);
    }
}

float getDefaultAnimationDurationHook() {
    return psu::persist_conf::devConf.animationsDuration;
}

void executeExternalActionHook(const WidgetCursor &widgetCursor, int16_t actionId, void *param) {
    if (scripting::isFlowRunning()) {
        scripting::executeFlowAction(widgetCursor, actionId);
    } else {
        g_externalActionWidgetCursor = widgetCursor;
        g_externalActionId = actionId;
    }
}

void externalDataHook(int16_t dataId, DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
    if (scripting::isFlowRunning()) {
        scripting::dataOperation(dataId, operation, widgetCursor, value);
    } else {
        if (dataId < 0) {
            dataId = -dataId;
        }
        dataId--;
        if ((uint16_t)dataId < MAX_NUM_EXTERNAL_DATA_ITEM_VALUES) {
            if (operation == DATA_OPERATION_GET) {
                value = g_externalDataItemValues[dataId].value;
            }
        }
    }
}

bool activePageHasBackdropHook() {
    if (getActivePageId() == INTERNAL_PAGE_ID_TOAST_MESSAGE || getActivePageId() == PAGE_ID_ASYNC_OPERATION_IN_PROGRESS) {
        return false;
    }
    return true;
}

OnTouchFunctionType getWidgetTouchFunctionHook(const WidgetCursor &widgetCursor) {
    if (widgetCursor.widget->data == DATA_ID_KEYPAD_TEXT) {
        return eez::gui::onKeypadTextTouch;
    }
    return nullptr;
}

bool isEventHandlingDisabledHook() {
    return g_shutdownInProgress;
}

} // namespace gui
} // namespace eez

////////////////////////////////////////////////////////////////////////////////

namespace eez {
namespace scripting {

void onUncaughtScriptExceptionHook() {
    g_psuAppContext.dialogClose();
    g_psuAppContext.showUncaughtScriptExceptionMessage();
}

} // scripting
} // eez

////////////////////////////////////////////////////////////////////////////////

namespace eez {
namespace psu {
namespace gui {

static int g_mcuRevisionSelectedByUser;

bool g_askMcuRevisionInProgress;

int askMcuRevision() {
	g_askMcuRevisionInProgress = true;

	if (isTouchCalibrated()) {
		showPage(PAGE_ID_SELECT_MCU_REVISION);
	} else {
		showPage(PAGE_ID_TOUCH_CALIBRATION_INTRO);
	}

	while (!g_mcuRevisionSelectedByUser) {
		WATCHDOG_RESET(WATCHDOG_HIGH_PRIORITY_THREAD);
        psu::tick();
		osDelay(1);
	}

    showPage(PAGE_ID_NONE);
	psu::persist_conf::setMcuRevision(g_mcuRevisionSelectedByUser);

	g_askMcuRevisionInProgress = false;

    return g_mcuRevisionSelectedByUser;
}

} // namespace psu
} // namespace gui

namespace gui {

using namespace psu::gui;

int getExtraLongTouchActionHook() {
#if defined(EEZ_PLATFORM_SIMULATOR)
    return ACTION_ID_NONE;
#else
    return ACTION_ID_SHOW_TOUCH_CALIBRATION_INTRO;
#endif
}

void getTouchScreenCalibrationParamsHook(
	int16_t &touchScreenCalTlx, int16_t &touchScreenCalTly,
	int16_t &touchScreenCalBrx, int16_t &touchScreenCalBry,
	int16_t &touchScreenCalTrx, int16_t &touchScreenCalTry
) {
	touchScreenCalTlx = psu::persist_conf::devConf.touchScreenCalTlx;
	touchScreenCalTly = psu::persist_conf::devConf.touchScreenCalTly;
	touchScreenCalBrx = psu::persist_conf::devConf.touchScreenCalBrx;
	touchScreenCalBry = psu::persist_conf::devConf.touchScreenCalBry;
	touchScreenCalTrx = psu::persist_conf::devConf.touchScreenCalTrx;
	touchScreenCalTry = psu::persist_conf::devConf.touchScreenCalTry;
}

void setTouchScreenCalibrationParamsHook(
	int16_t touchScreenCalTlx, int16_t touchScreenCalTly,
	int16_t touchScreenCalBrx, int16_t touchScreenCalBry,
	int16_t touchScreenCalTrx, int16_t touchScreenCalTry
) {
	psu::persist_conf::setTouchscreenCalParams(
		touchScreenCalTlx, touchScreenCalTly,
		touchScreenCalBrx, touchScreenCalBry,
		touchScreenCalTrx, touchScreenCalTry
	);
}

void onEnterTouchCalibrationHook() {
	replacePage(PAGE_ID_TOUCH_CALIBRATION);
	psu::Channel::saveAndDisableOE();
}

void onTouchCalibrationOkHook() {
	if (isPageOnStack(PAGE_ID_SYS_SETTINGS_DISPLAY)) {
		popPage();
		g_psuAppContext.infoMessage("Touch screen is calibrated.");
	} else if (g_askMcuRevisionInProgress) {
		showPage(PAGE_ID_SELECT_MCU_REVISION);
	} else {
		showPage(PAGE_ID_MAIN);
	}

	psu::Channel::restoreOE();
}

void onTouchCalibrationCancelHook() {
	if (isPageOnStack(PAGE_ID_SYS_SETTINGS_DISPLAY)) {
		popPage();
	} else {
		showPage(PAGE_ID_MAIN);
	}

	psu::Channel::restoreOE();
}

void onTouchCalibrationConfirmHook() {
	g_psuAppContext.yesNoDialog(
		g_psuAppContext.isPageOnStack(PAGE_ID_SYS_SETTINGS_DISPLAY) ? PAGE_ID_TOUCH_CALIBRATION_YES_NO_CANCEL : PAGE_ID_TOUCH_CALIBRATION_YES_NO,
		"Save changes?", touchCalibrationDialogYes, touchCalibrationDialogNo, touchCalibrationDialogCancel
	);
}

void toastMessagePageOnEncoderHook(ToastMessagePage *toast, int counter) {
    if (toast->hasAction()) {
        if (counter < 0) {
            if (toast->messageValue.getType() == VALUE_TYPE_SCPI_ERROR) {
                if (
					toast->messageValue.getFirstInt16() == SCPI_ERROR_VOLTAGE_LIMIT_EXCEEDED ||
					toast->messageValue.getFirstInt16() == SCPI_ERROR_CURRENT_LIMIT_EXCEEDED ||
					toast->messageValue.getFirstInt16() == SCPI_ERROR_POWER_LIMIT_EXCEEDED
                ) {
					toast->appContext->popPage();
                }
            }
        }
    } else {
		toast->appContext->popPage();
    }
}

////////////////////////////////////////////////////////////////////////////////

void initHooks() {
    g_hooks.getExtraLongTouchAction = getExtraLongTouchActionHook;
    g_hooks.getDefaultAnimationDuration = getDefaultAnimationDurationHook;
    g_hooks.executeExternalAction = executeExternalActionHook;
    g_hooks.externalData = externalDataHook;
    g_hooks.getWidgetTouchFunction = getWidgetTouchFunctionHook;
    g_hooks.getPageFromId = getPageFromIdHook;
    g_hooks.setFocusCursor = setFocusCursor;
    g_hooks.stateManagment = stateManagmentHook;
    g_hooks.activePageHasBackdrop = activePageHasBackdropHook;
    g_hooks.isEventHandlingDisabled = isEventHandlingDisabledHook;
    g_hooks.overrideStyle = overrideStyleHook;
    g_hooks.overrideStyleColor = overrideStyleColorHook;
    g_hooks.overrideActiveStyleColor = overrideActiveStyleColorHook;
    g_hooks.transformColor = transformColorHook;
    g_hooks.styleGetSmallerFont = styleGetSmallerFontHook;
    g_hooks.getDisplayBackgroundLuminosityStep = display::getDisplayBackgroundLuminosityStepHook;
    g_hooks.getSelectedThemeIndex = display::getSelectedThemeIndexHook;
    g_hooks.turnOnDisplayStart = display::turnOnDisplayStartHook;
    g_hooks.turnOnDisplayTick = display::turnOnDisplayTickHook;
    g_hooks.turnOffDisplayStart = display::turnOffDisplayStartHook;
    g_hooks.turnOffDisplayTick = display::turnOffDisplayTickHook;
    g_hooks.toastMessagePageOnEncoder = toastMessagePageOnEncoderHook;
    g_hooks.onEnterTouchCalibration = onEnterTouchCalibrationHook;
    g_hooks.onTouchCalibrationOk = onTouchCalibrationOkHook;
    g_hooks.onTouchCalibrationCancel = onTouchCalibrationCancelHook;
    g_hooks.onTouchCalibrationConfirm = onTouchCalibrationConfirmHook;
    g_hooks.getTouchScreenCalibrationParams = getTouchScreenCalibrationParamsHook;
    g_hooks.setTouchScreenCalibrationParams = setTouchScreenCalibrationParamsHook;
    g_hooks.onGuiQueueMessage = onGuiQueueMessageHook;
}

////////////////////////////////////////////////////////////////////////////////

static int g_selectedMcuRevision; // 0 - None, 1 - R2B4, 2 - R3B3

void data_selected_mcu_revision(DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        value = g_selectedMcuRevision;
    }
}

void data_is_mcu_revision_selected(DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
    if (operation == DATA_OPERATION_GET) {
        value = g_selectedMcuRevision != 0;
    }
}

void doSwitchToR3B3() {
    psu::persist_conf::setMcuRevision(MCU_REVISION_R3B3);
    eez::restart();
}

void doSwitchToR2B4() {
    psu::persist_conf::setMcuRevision(MCU_REVISION_R2B4);
    eez::restart();
}

void action_show_select_mcu_revision_page() {
	if (g_mcuRevision == MCU_REVISION_R2B4) {
		g_psuAppContext.yesNoDialog(PAGE_ID_YES_NO, "Switch to R3B3?", doSwitchToR3B3, nullptr, nullptr);
	} else {
		g_psuAppContext.yesNoDialog(PAGE_ID_YES_NO, "Switch to R2B4?", doSwitchToR2B4, nullptr, nullptr);
	}
}

void action_select_r2b4_revision() {
    g_selectedMcuRevision = 1;
}

void action_select_r3b3_revision() {
    g_selectedMcuRevision = 2;
}

void doSelect() {
	if (g_selectedMcuRevision == 1) {
		g_mcuRevisionSelectedByUser = MCU_REVISION_R2B4;
	} else {
		g_mcuRevisionSelectedByUser = MCU_REVISION_R3B3;
	}
}

void action_select_mcu_revision() {
    if (g_selectedMcuRevision != 0) {
		if (g_selectedMcuRevision == 1) {
			areYouSureWithMessage("You selected R2B4.", doSelect);
		} else {
			areYouSureWithMessage("You selected R3B3.", doSelect);
		}
    }
}

} // gui
} // eez

////////////////////////////////////////////////////////////////////////////////

#endif
