/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include "plugin-support.h"

// Forward declaration of the C++ bridge function
void s2_meter_add_dock(void);

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("s2-meter-plugin", "en-US")

bool obs_module_load(void)
{
    // เรียกฟังก์ชัน C++ เพื่อสร้าง Dock Widget
    s2_meter_add_dock();
    
    blog(LOG_INFO, "Sonifex S2 Meter Plugin loaded successfully.");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "Sonifex S2 Meter Plugin unloaded.");
}