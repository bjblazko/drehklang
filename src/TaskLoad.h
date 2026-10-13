#pragma once

#include <cstdint>

// "TASKS" over serial: each task's share of its core over `ms`, from
// FreeRTOS's run-time stats -- how a change to the audio path is shown to
// save work (2026-10-13). Blocks the loop for `ms`; call from core 1 only.
void printTaskLoad(uint32_t ms);
