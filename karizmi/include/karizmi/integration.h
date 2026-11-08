// SPDX-License-Identifier: GPL-3.0-or-later
// C-compatible boundary: the UltraVNC engine can remain C++14.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct KarizmiEngineCallbacks {
    void* context;
    int (*input_available)(void*);
    int (*attach)(void*, uintptr_t socket);
    void (*detach)(void*, uintptr_t socket);
    void (*control)(void*, uintptr_t socket, int enabled);
    void (*release_key)(void*, uint32_t keysym);
    void (*show)(void*, uintptr_t socket, int fullscreen);
    void (*thumbnail)(void*, uintptr_t socket, void* dc, int width, int height);
    uintptr_t (*window_peer)(void*, void* window);
    int (*focus_scope)(void*, uintptr_t socket, void* window);
} KarizmiEngineCallbacks;
int KarizmiRun(void* instance, int student, const KarizmiEngineCallbacks* engine);
int KarizmiHelper(int student, int* result);
int KarizmiRead(uintptr_t socket, void* data, int size);
int KarizmiWrite(uintptr_t socket, const void* data, int size);
int KarizmiReadable(uintptr_t socket, unsigned timeoutMs);
int KarizmiAuthenticated(uintptr_t socket);
int KarizmiControlAllowed(void);
void KarizmiLockInput(void);
void KarizmiUnlockInput(void);
void KarizmiTrackKey(uint32_t key, int down);
void KarizmiTrackButtons(unsigned mask);
void KarizmiToggleControl(void);
void KarizmiDisableControl(void);
void KarizmiFrameReceived(void);
void KarizmiDisconnect(void);
int KarizmiPeerControlAllowed(uintptr_t socket);
void KarizmiPeerToggleControl(uintptr_t socket);
void KarizmiPeerDisableControl(uintptr_t socket);
void KarizmiPeerFrameReceived(uintptr_t socket);
void KarizmiPeerViewerClosed(uintptr_t socket);
void KarizmiObserveKeyEvent(void);
#ifdef __cplusplus
}
// Keep permission checks and injection indivisible with input revocation.
class KarizmiInputGuard {
public:
    KarizmiInputGuard() { KarizmiLockInput(); }
    ~KarizmiInputGuard() { KarizmiUnlockInput(); }
    KarizmiInputGuard(const KarizmiInputGuard&) = delete;
    KarizmiInputGuard& operator=(const KarizmiInputGuard&) = delete;
};
#endif
