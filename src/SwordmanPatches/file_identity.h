#ifndef SWORDMAN_FILE_IDENTITY_H
#define SWORDMAN_FILE_IDENTITY_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

bool SwordmanHashHandle(HANDLE file, const uint8_t expected[32]);
bool SwordmanHashFile(const wchar_t *path, const uint8_t expected[32]);

#endif
