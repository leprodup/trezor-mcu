/*
 * This file is part of the TREZOR project, https://trezor.io/
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

#include "bootloader.h"
#include "buttons.h"
#include "setup.h"
#include "usb.h"
#include "oled.h"
#include "util.h"
#include "signatures.h"
#include "layout.h"
#include "rng.h"
#include "timer.h"
#include "memory.h"

const uint32_t FIRMWARE_MAGIC = 0x525a5254; // TRZR

void layoutFirmwareHash(const uint8_t *hash)
{
	char str[4][17];
	for (int i = 0; i < 4; i++) {
		data2hex(hash + i * 8, 8, str[i]);
	}
	layoutDialog(&bmp_icon_question, "Abort", "Continue", "Compare fingerprints", str[0], str[1], str[2], str[3], NULL, NULL);
}

void show_halt(void)
{
	layoutDialog(&bmp_icon_error, NULL, NULL, NULL, "Unofficial firmware", "aborted.", NULL, "Unplug your TREZOR", "contact our support.", NULL);
	shutdown();
}

void show_unofficial_warning(const uint8_t *hash)
{
	layoutDialog(&bmp_icon_warning, "Abort", "I'll take the risk", NULL, "WARNING!", NULL, "Unofficial firmware", "detected.", NULL, NULL);

	do {
		delay(100000);
		buttonUpdate();
	} while (!button.YesUp && !button.NoUp);

	if (button.NoUp) {
		show_halt(); // no button was pressed -> halt
	}

	layoutFirmwareHash(hash);

	do {
		delay(100000);
		buttonUpdate();
	} while (!button.YesUp && !button.NoUp);

	if (button.NoUp) {
		show_halt(); // no button was pressed -> halt
	}

	// everything is OK, user pressed 2x Continue -> continue program
}

void __attribute__((noreturn)) load_app(int signed_firmware)
{
	// zero out SRAM
	memset_reg(_ram_start, _ram_end, 0);

	jump_to_firmware((const vector_table_t *) FLASH_PTR(FLASH_APP_START), signed_firmware);
}

bool firmware_present(void)
{
#ifndef APPVER
	if (memcmp(FLASH_PTR(FLASH_FWHEADER_MAGIC), &FIRMWARE_MAGIC, 4)) { // magic does not match
		return false;
	}
	if (*((const uint32_t *)FLASH_PTR(FLASH_FWHEADER_CODELEN)) < 8192) { // firmware reports smaller size than 8kB
		return false;
	}
	if (*((const uint32_t *)FLASH_PTR(FLASH_FWHEADER_CODELEN)) > FLASH_APP_LEN) { // firmware reports bigger size than flash size
		return false;
	}
#endif
	return true;
}

void bootloader_loop(void)
{
	oledClear();
	oledDrawBitmap(0, 0, &bmp_logo64);
	if (firmware_present()) {
		oledDrawStringCenter(90, 10, "TREZOR", FONT_STANDARD);
		oledDrawStringCenter(90, 30, "Bootloader", FONT_STANDARD);
		oledDrawStringCenter(90, 50, VERSTR(VERSION_MAJOR) "." VERSTR(VERSION_MINOR) "." VERSTR(VERSION_PATCH), FONT_STANDARD);
	} else {
		oledDrawStringCenter(90, 10, "Welcome!", FONT_STANDARD);
		oledDrawStringCenter(90, 30, "Please visit", FONT_STANDARD);
		oledDrawStringCenter(90, 50, "trezor.io/start", FONT_STANDARD);
	}
	oledRefresh();

	usbLoop(firmware_present());
}

int main(void)
{
#ifndef APPVER
	setup();
#endif
	__stack_chk_guard = random32(); // this supports compiler provided unpredictable stack protection checks
#ifndef APPVER
	memory_protect();
	oledInit();
#endif

#ifndef APPVER
	// at least one button is unpressed
	uint16_t state = gpio_port_read(BTN_PORT);
	int unpressed = ((state & BTN_PIN_YES) == BTN_PIN_YES || (state & BTN_PIN_NO) == BTN_PIN_NO);

	if (firmware_present() && unpressed) {

		oledClear();
		oledDrawBitmap(40, 0, &bmp_logo64_empty);
		oledRefresh();

		uint8_t hash[32];
		int signed_firmware = signatures_ok(hash, NULL, 0);
		if (SIG_OK != signed_firmware) {
			show_unofficial_warning(hash);
			timer_init();
		}

		load_app(signed_firmware);
	}
#endif

	bootloader_loop();

	return 0;
}
