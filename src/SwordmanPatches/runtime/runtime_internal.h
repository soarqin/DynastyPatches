#ifndef SWORDMAN_RUNTIME_INTERNAL_H
#define SWORDMAN_RUNTIME_INTERNAL_H

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../swordman.h"
#include "../file_identity.h"

#define HOOK_ADDRESS(function) ((LPVOID)(uintptr_t)(function))

bool ValidateGameBytes(uintptr_t address, const uint8_t *expected, size_t length);
bool CreateGameHook(uintptr_t address, const uint8_t *expected, size_t length,
                    LPVOID detour, LPVOID *original);
bool CreateGameplayHooks(const SwordmanOptions *options);
bool CreateDisplayHooks(const SwordmanOptions *options);

#endif
