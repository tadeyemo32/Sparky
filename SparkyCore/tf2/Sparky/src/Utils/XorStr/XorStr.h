#pragma once

#include <string>
#include <format>

template<size_t N>
struct XorStr
{
	char m_buf[N]{};
	static constexpr unsigned char KEY = 0x5A;

	consteval XorStr(const char (&str)[N])
	{
		for (size_t i = 0; i < N; i++)
			m_buf[i] = str[i] ^ KEY;
	}

	[[nodiscard]] std::string Decrypt() const
	{
		std::string out(N - 1, '\0');
		for (size_t i = 0; i < N - 1; i++)
			out[i] = static_cast<char>(m_buf[i] ^ KEY);
		return out;
	}
};

// Compile-time XOR string encryption.
// The string is stored encrypted in the binary and decrypted on first use.
// Returns const char* backed by static storage — safe for any context.
#define XS(str)                                                         \
	([]() -> const char*                                                \
	{                                                                   \
		static constexpr XorStr<sizeof(str)> _xs(str);                 \
		static const std::string _s = _xs.Decrypt();                   \
		return _s.c_str();                                              \
	}())

// Runtime-format wrapper for use with XS() format strings.
// std::format requires a consteval format string; use XSFMT when the format
// string is a runtime value such as the result of XS().
// Usage: XSFMT(XS("value: {}"), someValue)
template<typename... Args>
inline std::string XSFMT(const char* fmt, Args&&... args)
{
	return std::vformat(fmt, std::make_format_args(args...));
}
