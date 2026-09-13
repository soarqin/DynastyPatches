#include "file_identity.h"

#include <bcrypt.h>
#include <string.h>

bool SwordmanHashHandle(HANDLE file, const uint8_t expected[32]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t buffer[16384], digest[32];
    DWORD count = 0;
    DWORD error = ERROR_CRC;
    bool success = false;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0) < 0) goto done;
    for (;;) {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, NULL)) {
            error = GetLastError();
            goto done;
        }
        if (count == 0) break;
        if (BCryptHashData(hash, buffer, count, 0) < 0) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) goto done;
    success = memcmp(digest, expected, sizeof(digest)) == 0;
    error = success ? ERROR_SUCCESS : ERROR_REVISION_MISMATCH;
done:
    if (hash != NULL) BCryptDestroyHash(hash);
    if (algorithm != NULL) BCryptCloseAlgorithmProvider(algorithm, 0);
    SetLastError(error);
    return success;
}

bool SwordmanHashFile(const wchar_t *path, const uint8_t expected[32]) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool success = SwordmanHashHandle(file, expected);
    DWORD error = GetLastError();
    CloseHandle(file);
    SetLastError(error);
    return success;
}
