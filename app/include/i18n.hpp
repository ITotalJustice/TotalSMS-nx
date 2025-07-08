#pragma once

#include <string>
#include <string_view>

#ifdef STUB_I18n
namespace sphaira::i18n {

static inline bool init(long index) { return true; }
static inline void exit() { }

static inline std::string get(std::string_view str) {
    return {str.data(), str.length()};
}

} // namespace sphaira::i18n

inline namespace literals {

static inline std::string operator""_i18n(const char* str, size_t len) {
    return {str, len};
}

} // namespace literals

#else
#error "bad"
namespace sphaira::i18n {

bool init(long index);
void exit();

std::string get(std::string_view str);

} // namespace sphaira::i18n

inline namespace literals {

std::string operator""_i18n(const char* str, size_t len);

} // namespace literals
#endif
