#pragma once
// Standalone type definitions extracted from SDK/Definitions/Types.h
// for test compilation without game SDK dependencies.

#include <cmath>
#include <cfloat>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <typeinfo>
#include <string_view>

#undef min
#undef max

#define PI 3.14159265358979323846
#define M_RADPI 57.295779513082
#define DEG2RAD(x) ((float)(x) * (float)((float)(PI) / 180.f))
#define RAD2DEG(x) ((float)(x) * (float)(180.f / (float)(PI)))

#define floatCompare(x, y) (fabsf(x - y) <= FLT_EPSILON * fmaxf(1.f, fmaxf(fabsf(x), fabsf(y))))

using byte = unsigned char;

// Include real SDK types instead of mocks
#include "../Spxarky/src/SDK/Definitions/Types.h"
