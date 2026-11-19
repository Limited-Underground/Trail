#pragma once
#include "FreeRTOS.h"
struct StaticSemaphore_t {};
using SemaphoreHandle_t=StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* p) { return p; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
