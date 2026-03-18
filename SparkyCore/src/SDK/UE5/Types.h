#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Unreal Engine 5 core types — layout matches UE 5.1/5.2 shipping builds.
// Adjust offsets if your clone diverges from stock UE5.
// ---------------------------------------------------------------------------

// --- FName -----------------------------------------------------------------
struct FName
{
    uint32_t ComparisonIndex = 0;
    uint32_t Number          = 0;

    bool operator==(const FName& o) const
    {
        return ComparisonIndex == o.ComparisonIndex && Number == o.Number;
    }
};

// --- FString ---------------------------------------------------------------
struct FString
{
    wchar_t* Data  = nullptr;
    int32_t  Count = 0;
    int32_t  Max   = 0;

    const wchar_t* CStr() const { return Data ? Data : L""; }
    bool IsValid() const        { return Data && Count > 0; }
};

// --- TArray ----------------------------------------------------------------
template<typename T>
struct TArray
{
    T*      Data  = nullptr;
    int32_t Count = 0;
    int32_t Max   = 0;

    T&       operator[](int32_t i)       { return Data[i]; }
    const T& operator[](int32_t i) const { return Data[i]; }
    T*       begin()       { return Data; }
    T*       end()         { return Data + Count; }
    const T* begin() const { return Data; }
    const T* end()   const { return Data + Count; }
    bool     IsValidIndex(int32_t i) const { return i >= 0 && i < Count; }
};

// --- FVector ---------------------------------------------------------------
struct FVector
{
    double X = 0.0, Y = 0.0, Z = 0.0;

    FVector() = default;
    FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}

    FVector operator+(const FVector& o) const { return {X+o.X, Y+o.Y, Z+o.Z}; }
    FVector operator-(const FVector& o) const { return {X-o.X, Y-o.Y, Z-o.Z}; }
    FVector operator*(double s)         const { return {X*s,   Y*s,   Z*s};   }

    double  Dot(const FVector& o)  const { return X*o.X + Y*o.Y + Z*o.Z; }
    double  SizeSquared()          const { return X*X + Y*Y + Z*Z; }
    double  Size()                 const { return sqrt(SizeSquared()); }
    FVector GetSafeNormal()        const
    {
        double sq = SizeSquared();
        if (sq < 1e-8) return {};
        double inv = 1.0 / sqrt(sq);
        return {X*inv, Y*inv, Z*inv};
    }
};

struct FVector2D { double X = 0.0, Y = 0.0; };
struct FRotator  { double Pitch = 0.0, Yaw = 0.0, Roll = 0.0; };
struct FQuat     { double X = 0.0, Y = 0.0, Z = 0.0, W = 1.0; };

struct FTransform
{
    FQuat   Rotation;
    FVector Translation;
    double  _pad0 = 0.0;
    FVector Scale3D;
    double  _pad1 = 0.0;
};

// --- FMatrix (4x4 row-major) -----------------------------------------------
struct FMatrix
{
    float M[4][4]{};

    bool ProjectWorldToScreen(const FVector& world,
                               const FVector2D& screenSize,
                               FVector2D& out) const
    {
        float x = (float)world.X, y = (float)world.Y, z = (float)world.Z;
        float w = M[0][3]*x + M[1][3]*y + M[2][3]*z + M[3][3];
        if (w < 0.0001f) return false;
        float sx = M[0][0]*x + M[1][0]*y + M[2][0]*z + M[3][0];
        float sy = M[0][1]*x + M[1][1]*y + M[2][1]*z + M[3][1];
        out.X = (screenSize.X * 0.5) + (sx / w) * (screenSize.X * 0.5);
        out.Y = (screenSize.Y * 0.5) - (sy / w) * (screenSize.Y * 0.5);
        return true;
    }
};

// --- Color -----------------------------------------------------------------
struct FLinearColor { float R=0.f, G=0.f, B=0.f, A=1.f; };

// --- Gameplay enums --------------------------------------------------------
enum class ENetRole : uint8_t
{
    ROLE_None           = 0,
    ROLE_SimulatedProxy = 1,
    ROLE_AutonomousProxy= 2,
    ROLE_Authority      = 3,
};

enum class ETeamIndex : uint8_t
{
    Neutral = 0,
    Team1   = 1,
    Team2   = 2,
};
