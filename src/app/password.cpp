#include "app/password.h"

#include <argon2.h>

#include <array>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace ninkasi::app {

namespace {

constexpr std::uint32_t kSaltBytes = 16;
constexpr std::uint32_t kHashBytes = 32;
constexpr std::uint32_t kLanes = 1;  // one thread; signing in needn't be faster

// Fills `out` from the operating system's cryptographic random source.
void random_bytes(unsigned char* out, std::size_t count) {
#ifdef _WIN32
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        throw std::runtime_error("Windows couldn't supply random bytes for the password.");
    }
#else
    std::size_t filled = 0;
    while (filled < count) {
        const ssize_t got = getrandom(out + filled, count - filled, 0);
        if (got < 0) {
            if (errno == EINTR) continue;  // interrupted by a signal: try again
            throw std::runtime_error("The system couldn't supply random bytes for the password.");
        }
        filled += static_cast<std::size_t>(got);
    }
#endif
}

} // namespace

std::string hash_password(std::string_view password, const PasswordCost& cost) {
    std::array<unsigned char, kSaltBytes> salt{};
    random_bytes(salt.data(), salt.size());

    // argon2_encodedlen counts the terminating '\0'.
    std::string encoded(argon2_encodedlen(cost.passes, cost.memory_kib, kLanes, kSaltBytes, kHashBytes, Argon2_id),
                        '\0');
    const int result = argon2id_hash_encoded(cost.passes, cost.memory_kib, kLanes, password.data(), password.size(),
                                             salt.data(), salt.size(), kHashBytes, encoded.data(), encoded.size());
    if (result != ARGON2_OK) {
        throw std::runtime_error(std::string("The password couldn't be hashed: ") + argon2_error_message(result));
    }
    encoded.resize(std::strlen(encoded.c_str()));
    return encoded;
}

bool verify_password(std::string_view password, const std::string& encoded) {
    if (encoded.rfind("$argon2id$", 0) != 0) return false;
    return argon2id_verify(encoded.c_str(), password.data(), password.size()) == ARGON2_OK;
}

} // namespace ninkasi::app
