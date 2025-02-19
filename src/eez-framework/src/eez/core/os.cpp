/*
* EEZ Generic Firmware
* Copyright (C) 2018-present, Envox d.o.o.
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

#if defined(EEZ_PLATFORM_STM32)
#include <main.h>
#endif

#if defined(EEZ_PLATFORM_ESP32)
#include <esp_timer.h>
#endif

#if defined(EEZ_PLATFORM_PICO)
#include "pico/stdlib.h"
#endif

#if defined(EEZ_PLATFORM_RASPBERRY)
#include <circle/timer.h>
#endif

#if defined(EEZ_FOR_LVGL)
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#endif

#if defined(__EMSCRIPTEN__)
#include <sys/time.h>
#endif

#include <eez/core/os.h>

namespace eez {

uint32_t millis() {
#if defined(EEZ_PLATFORM_STM32)
	return HAL_GetTick();
#elif defined(__EMSCRIPTEN__)
	return (uint32_t)emscripten_get_now();
#elif defined(EEZ_PLATFORM_SIMULATOR)
	return osKernelGetTickCount();
#elif defined(EEZ_PLATFORM_ESP32)
	return (unsigned long) (esp_timer_get_time() / 1000ULL);
#elif defined(EEZ_PLATFORM_PICO)
    auto abs_time = get_absolute_time();
    return to_ms_since_boot(abs_time);
#elif defined(EEZ_PLATFORM_RASPBERRY)
    unsigned nStartTicks = CTimer::Get()->GetClockTicks();
    return nStartTicks / 1000;
#elif defined(EEZ_FOR_LVGL)
    return lv_tick_get();
#else
    #error "Missing millis implementation";
#endif
}

} // namespace eez
