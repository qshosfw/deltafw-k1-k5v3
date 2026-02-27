#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


// --- Modular Components ---
#include "chacha20.h"
#include "poly1305.h"
#include "trng.h"


#ifdef __cplusplus
}
#endif

#endif // CRYPTO_H
