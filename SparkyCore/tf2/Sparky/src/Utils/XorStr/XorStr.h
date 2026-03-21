#pragma once

#include <string>

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
