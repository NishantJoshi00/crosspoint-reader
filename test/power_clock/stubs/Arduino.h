#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

unsigned long millis();
void delay(unsigned long ms);
void configTzTime(const char*, const char*, const char*);
