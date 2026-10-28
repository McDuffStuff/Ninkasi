#pragma once
// Password hashing for signing in, with Argon2id: the winner of the Password
// Hashing Competition, and what OWASP recommends for storing passwords.
//
// A hash is kept in Argon2's standard encoded form,
//   $argon2id$v=19$m=19456,t=2,p=1$<salt>$<hash>
// which carries its own salt and cost. The cost can be raised later without
// breaking the hashes already saved.

#include <cstdint>
#include <string>
#include <string_view>

namespace ninkasi::app {

// How much work one hash takes. The default is OWASP's recommendation for
// Argon2id: 19 MiB of memory and 2 passes over it, in one lane.
struct PasswordCost {
    std::uint32_t memory_kib = 19 * 1024;
    std::uint32_t passes = 2;
};

// Hashes `password` with a new random salt, so two people with the same
// password get different hashes. Throws std::runtime_error if the operating
// system can't supply random bytes.
std::string hash_password(std::string_view password, const PasswordCost& cost = {});

// Whether `password` matches `encoded` (from hash_password). False for a
// wrong password, and for anything that isn't an Argon2id hash.
bool verify_password(std::string_view password, const std::string& encoded);

// A new recovery code: 20 random letters and digits in groups of four, like
// "K7QF-3M9X-TR2D-8HWC-P4NZ" (about 99 bits). It leaves out 0, 1, I, L and
// O, which are easy to misread. Throws std::runtime_error if the operating
// system can't supply random bytes.
std::string new_recovery_code();

// A recovery code as typed, reduced to what's compared: letters and digits
// only, in upper case, so spaces, dashes and case don't matter.
std::string normalize_recovery_code(std::string_view typed);

} // namespace ninkasi::app
