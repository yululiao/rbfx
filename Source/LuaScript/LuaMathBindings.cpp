//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Math/BoundingBox.h"
#include "../Urho3D/Math/Color.h"
#include "../Urho3D/Math/MathDefs.h"
#include "../Urho3D/Math/Plane.h"
#include "../Urho3D/Math/Random.h"
#include "../Urho3D/Math/Quaternion.h"
#include "../Urho3D/Math/RandomEngine.h"
#include "../Urho3D/Math/Ray.h"
#include "../Urho3D/Math/Rect.h"
#include "../Urho3D/Math/Sphere.h"
#include "../Urho3D/Math/Vector2.h"
#include "../Urho3D/Math/Vector3.h"
#include "../Urho3D/Math/Vector4.h"

#include <sol/sol.hpp>

namespace sol
{

// Value math types lack the full set of comparison operators required by
// sol3's automagical registration, so all behavior is registered explicitly.
template <> struct is_automagical<Urho3D::Vector2> : std::false_type {};
template <> struct is_automagical<Urho3D::Vector3> : std::false_type {};
template <> struct is_automagical<Urho3D::Vector4> : std::false_type {};
template <> struct is_automagical<Urho3D::IntVector2> : std::false_type {};
template <> struct is_automagical<Urho3D::Quaternion> : std::false_type {};
template <> struct is_automagical<Urho3D::Color> : std::false_type {};
template <> struct is_automagical<Urho3D::IntRect> : std::false_type {};
template <> struct is_automagical<Urho3D::Rect> : std::false_type {};
template <> struct is_automagical<Urho3D::BoundingBox> : std::false_type {};
template <> struct is_automagical<Urho3D::Plane> : std::false_type {};
template <> struct is_automagical<Urho3D::Ray> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterMathBindings(sol::state& lua)
{
    {
        using RBFX_THIS = Vector2;
        RBFX_USERTYPE(Vector2,
            sol::call_constructor, sol::constructors<Vector2(), Vector2(float, float)>()
            RBFX_RAW(x, &Vector2::x_)
            RBFX_RAW(y, &Vector2::y_)
            RBFX_M(Length)
            RBFX_M(LengthSquared)
            RBFX_M(Normalized)
            RBFX_RAW(Dot, &Vector2::DotProduct)
            RBFX_M(Angle)
            RBFX_M(Lerp)
            RBFX_META(equal_to, [](const Vector2& a, const Vector2& b) { return a == b; })
            RBFX_META(addition, [](const Vector2& a, const Vector2& b) { return a + b; })
            RBFX_META(subtraction, [](const Vector2& a, const Vector2& b) { return a - b; })
            RBFX_META(unary_minus, [](const Vector2& a) { return -a; })
            RBFX_META(multiplication, sol::overload(
                [](const Vector2& a, float s) { return a * s; },
                [](const Vector2& a, const Vector2& b) { return a * b; }))
            RBFX_META(division, [](const Vector2& a, float s) { return a / s; })
            RBFX_RAW(ZERO, sol::var(Vector2::ZERO))
            RBFX_RAW(ONE, sol::var(Vector2::ONE))
            RBFX_RAW(UP, sol::var(Vector2::UP))
            RBFX_RAW(RIGHT, sol::var(Vector2::RIGHT))
            RBFX_RAW(DOWN, sol::var(Vector2::DOWN))
            RBFX_RAW(LEFT, sol::var(Vector2::LEFT))
        );
    }

    {
        using RBFX_THIS = Vector3;
        RBFX_USERTYPE(Vector3,
            sol::call_constructor, sol::constructors<Vector3(), Vector3(float, float, float)>()
            RBFX_RAW(x, &Vector3::x_)
            RBFX_RAW(y, &Vector3::y_)
            RBFX_RAW(z, &Vector3::z_)
            RBFX_RAW(FromXZ, [](const Vector2& v, float y) { return Vector3(v.x_, y, v.y_); })
            RBFX_M(Length)
            RBFX_M(LengthSquared)
            RBFX_M(Normalized)
            RBFX_RAW(Dot, &Vector3::DotProduct)
            RBFX_RAW(Cross, &Vector3::CrossProduct)
            // tolua-style aliases used by the samples (23_Water).
            RBFX_M(DotProduct)
            RBFX_M(CrossProduct)
            RBFX_M(Lerp)
            RBFX_M(Angle)
            RBFX_META(equal_to, [](const Vector3& a, const Vector3& b) { return a == b; })
            RBFX_META(addition, [](const Vector3& a, const Vector3& b) { return a + b; })
            RBFX_META(subtraction, [](const Vector3& a, const Vector3& b) { return a - b; })
            RBFX_META(unary_minus, [](const Vector3& a) { return -a; })
            RBFX_META(multiplication, sol::overload(
                [](const Vector3& a, float s) { return a * s; },
                [](const Vector3& a, const Vector3& b) { return a * b; }))
            RBFX_META(division, [](const Vector3& a, float s) { return a / s; })
            RBFX_RAW(ZERO, sol::var(Vector3::ZERO))
            RBFX_RAW(ONE, sol::var(Vector3::ONE))
            RBFX_RAW(UP, sol::var(Vector3::UP))
            RBFX_RAW(RIGHT, sol::var(Vector3::RIGHT))
            RBFX_RAW(DOWN, sol::var(Vector3::DOWN))
            RBFX_RAW(LEFT, sol::var(Vector3::LEFT))
            RBFX_RAW(FORWARD, sol::var(Vector3::FORWARD))
            RBFX_RAW(BACK, sol::var(Vector3::BACK))
        );
    }

    {
        using RBFX_THIS = Vector4;
        RBFX_USERTYPE(Vector4,
            sol::call_constructor, sol::constructors<Vector4(), Vector4(float, float, float, float)>()
            RBFX_RAW(x, &Vector4::x_)
            RBFX_RAW(y, &Vector4::y_)
            RBFX_RAW(z, &Vector4::z_)
            RBFX_RAW(w, &Vector4::w_)
            RBFX_META(equal_to, [](const Vector4& a, const Vector4& b) { return a == b; })
            RBFX_META(addition, [](const Vector4& a, const Vector4& b) { return a + b; })
            RBFX_META(subtraction, [](const Vector4& a, const Vector4& b) { return a - b; })
            RBFX_META(multiplication, [](const Vector4& a, float s) { return a * s; })
            RBFX_RAW(ZERO, sol::var(Vector4::ZERO))
            RBFX_RAW(ONE, sol::var(Vector4::ONE))
        );
    }

    {
        using RBFX_THIS = IntVector2;
        RBFX_USERTYPE(IntVector2,
            sol::call_constructor, sol::factories(
                []() { return IntVector2(); },
                [](int x, int y) { return IntVector2(x, y); },
                // Lua arithmetic (e.g. Random() * width) yields floats
                [](double x, double y) { return IntVector2(static_cast<int>(x), static_cast<int>(y)); })
            RBFX_RAW(x, &IntVector2::x_)
            RBFX_RAW(y, &IntVector2::y_)
            RBFX_M(ToVector2)
            RBFX_META(equal_to, [](const IntVector2& a, const IntVector2& b) { return a == b; })
            RBFX_META(addition, [](const IntVector2& a, const IntVector2& b) { return a + b; })
            RBFX_META(subtraction, [](const IntVector2& a, const IntVector2& b) { return a - b; })
            RBFX_RAW(ZERO, sol::var(IntVector2::ZERO))
            RBFX_RAW(ONE, sol::var(IntVector2::ONE))
        );
    }

    {
        using RBFX_THIS = IntVector3;
        RBFX_USERTYPE(IntVector3,
            sol::call_constructor, sol::constructors<IntVector3(), IntVector3(int, int, int)>()
            RBFX_RAW(x, &IntVector3::x_)
            RBFX_RAW(y, &IntVector3::y_)
            RBFX_RAW(z, &IntVector3::z_)
            RBFX_META(equal_to, [](const IntVector3& a, const IntVector3& b) { return a == b; })
            RBFX_RAW(ZERO, sol::var(IntVector3::ZERO))
            RBFX_RAW(ONE, sol::var(IntVector3::ONE))
        );
    }

    // 3 floats = euler angles, 4 floats = (w, x, y, z) components.
    {
        using RBFX_THIS = Quaternion;
        RBFX_USERTYPE(Quaternion,
            sol::call_constructor, sol::constructors<Quaternion(), Quaternion(float, float, float), Quaternion(float, float, float, float), Quaternion(float, const Vector3&), Quaternion(const Vector3&, const Vector3&)>()
            RBFX_RAW(w, &Quaternion::w_)
            RBFX_RAW(x, &Quaternion::x_)
            RBFX_RAW(y, &Quaternion::y_)
            RBFX_RAW(z, &Quaternion::z_)
            RBFX_M(FromAngleAxis)
            RBFX_M(FromEulerAngles)
            RBFX_M(FromRotationTo)
            RBFX_M(FromAxes)
            RBFX_M(YawAngle)
            RBFX_M(PitchAngle)
            RBFX_M(RollAngle)
            RBFX_M(Conjugate)
            RBFX_M(Inverse)
            RBFX_M(Slerp)
            RBFX_M(Normalized)
            RBFX_META(equal_to, [](const Quaternion& a, const Quaternion& b) { return a == b; })
            RBFX_META(multiplication, sol::overload(
                [](const Quaternion& a, const Quaternion& b) { return a * b; },
                [](const Quaternion& a, const Vector3& v) { return a * v; },
                [](const Quaternion& a, float s) { return a * s; }))
            RBFX_RAW(IDENTITY, sol::var(Quaternion::IDENTITY))
        );
    }

    {
        using RBFX_THIS = Color;
        RBFX_USERTYPE(Color,
            sol::call_constructor, sol::constructors<Color(), Color(float, float, float), Color(float, float, float, float)>()
            RBFX_RAW(r, &Color::r_)
            RBFX_RAW(g, &Color::g_)
            RBFX_RAW(b, &Color::b_)
            RBFX_RAW(a, &Color::a_)
            RBFX_M(Lerp)
            RBFX_M(ToHSV)
            RBFX_M(FromHSV)
            RBFX_META(equal_to, [](const Color& a, const Color& b) { return a == b; })
            RBFX_META(addition, [](const Color& a, const Color& b) { return a + b; })
            RBFX_META(multiplication, sol::overload(
                [](const Color& a, float s) { return a * s; },
                [](const Color& a, const Color& b) { return a * b; }))
            RBFX_RAW(WHITE, sol::var(Color::WHITE))
            RBFX_RAW(GRAY, sol::var(Color::GRAY))
            RBFX_RAW(BLACK, sol::var(Color::BLACK))
            RBFX_RAW(RED, sol::var(Color::RED))
            RBFX_RAW(GREEN, sol::var(Color::GREEN))
            RBFX_RAW(BLUE, sol::var(Color::BLUE))
            RBFX_RAW(CYAN, sol::var(Color::CYAN))
            RBFX_RAW(MAGENTA, sol::var(Color::MAGENTA))
            RBFX_RAW(YELLOW, sol::var(Color::YELLOW))
            RBFX_RAW(TRANSPARENT, sol::var(Color::TRANSPARENT_BLACK))
        );
    }

    {
        using RBFX_THIS = IntRect;
        RBFX_USERTYPE(IntRect,
            sol::call_constructor, sol::factories(
                []() { return IntRect(); },
                [](int left, int top, int right, int bottom) { return IntRect(left, top, right, bottom); },
                [](double left, double top, double right, double bottom) {
                    return IntRect(static_cast<int>(left), static_cast<int>(top),
                        static_cast<int>(right), static_cast<int>(bottom));
                })
            RBFX_RAW(left, &IntRect::left_)
            RBFX_RAW(top, &IntRect::top_)
            RBFX_RAW(right, &IntRect::right_)
            RBFX_RAW(bottom, &IntRect::bottom_)
            RBFX_RAW(Size, [](const IntRect& r) { return r.Size(); })
            RBFX_RAW(Width, [](const IntRect& r) { return r.Width(); })
            RBFX_RAW(Height, [](const IntRect& r) { return r.Height(); })
            RBFX_RAW(IsInside, [](const IntRect& r, const IntVector2& point) { return r.IsInside(point); })
            RBFX_META(equal_to, [](const IntRect& a, const IntRect& b) { return a == b; })
            RBFX_RAW(ZERO, sol::var(IntRect::ZERO))
        );
    }

    {
        using RBFX_THIS = Rect;
        RBFX_USERTYPE(Rect,
            sol::call_constructor, sol::constructors<Rect(), Rect(const Vector2&, const Vector2&), Rect(float, float, float, float)>()
            RBFX_RAW(min, &Rect::min_)
            RBFX_RAW(max, &Rect::max_)
            RBFX_RAW(Size, [](const Rect& r) { return r.Size(); })
            RBFX_RAW(Center, [](const Rect& r) { return r.Center(); })
            RBFX_OVERLOAD(IsInside,
                [](const Rect& r, const Vector2& point) { return r.IsInside(point); },
                [](const Rect& r, const Rect& rect) { return r.IsInside(rect); })
            RBFX_META(equal_to, [](const Rect& a, const Rect& b) { return a == b; })
            RBFX_RAW(FULL, sol::var(Rect::FULL))
            RBFX_RAW(POSITIVE, sol::var(Rect::POSITIVE))
            RBFX_RAW(ZERO, sol::var(Rect::ZERO))
        );
    }

    {
        using RBFX_THIS = BoundingBox;
        RBFX_USERTYPE(BoundingBox,
            sol::call_constructor, sol::constructors<BoundingBox(), BoundingBox(const Vector3&, const Vector3&), BoundingBox(float, float)>()
            RBFX_RAW(min, &BoundingBox::min_)
            RBFX_RAW(max, &BoundingBox::max_)
            RBFX_RAW(Center, [](const BoundingBox& b) { return b.Center(); })
            RBFX_RAW(Size, [](const BoundingBox& b) { return b.Size(); })
            RBFX_RAW(HalfSize, [](const BoundingBox& b) { return b.HalfSize(); })
            RBFX_OVERLOAD(Merged,
                [](const BoundingBox& a, const BoundingBox& b) { return a.Merged(b); },
                [](const BoundingBox& a, const Vector3& b) { return a.Merged(b); })
            RBFX_OVERLOAD(IsInside,
                [](const BoundingBox& b, const BoundingBox& box) { return b.IsInside(box); },
                [](const BoundingBox& b, const Vector3& point) { return b.IsInside(point); })
            RBFX_META(equal_to, [](const BoundingBox& a, const BoundingBox& b) { return a == b; })
        );
    }

    {
        using RBFX_THIS = Plane;
        RBFX_USERTYPE(Plane,
            sol::call_constructor, sol::constructors<
                Plane(),
                Plane(const Vector3&, const Vector3&),
                Plane(const Vector3&, const Vector3&, const Vector3&)>()
            RBFX_RAW(normal, &Plane::normal_)
            RBFX_RAW(d, &Plane::d_)
            RBFX_RAW(Distance, [](const Plane& p, const Vector3& point) { return p.Distance(point); })
        );
    }

    {
        using RBFX_THIS = Ray;
        RBFX_USERTYPE(Ray,
            sol::call_constructor, sol::constructors<Ray(), Ray(const Vector3&, const Vector3&)>()
            RBFX_RAW(origin, &Ray::origin_)
            RBFX_RAW(direction, &Ray::direction_)
            RBFX_OVERLOAD(HitDistance,
                RBFX_CAST_C(HitDistance, float, const Plane&),
                RBFX_CAST_C(HitDistance, float, const BoundingBox&),
                RBFX_CAST_C(HitDistance, float, const Sphere&))
            RBFX_M(ClosestPoint)
        );
    }

    // Global random helpers mirroring MathDefs.h free functions.
    lua.set_function("Random", sol::overload(
        static_cast<float (*)()>(&Random),
        static_cast<float (*)(float)>(&Random),
        static_cast<float (*)(float, float)>(&Random)));
    lua.set_function("RandomNormal", &RandomNormal);
    // Seeding for reproducible or per-run random sequences (49/50 Sample2D
    // background color randomization).
    lua.set_function("SetRandomSeed", &SetRandomSeed);
    lua.set_function("GetRandomSeed", &GetRandomSeed);

    // Scalar helpers from MathDefs.h used across the samples.
    lua.set_function("Clamp", [](float value, float min, float max) { return Clamp(value, min, max); });
    lua.set_function("Lerp", [](float a, float b, float t) { return Lerp(a, b, t); });
    lua.set_function("Min", sol::overload(
        [](float a, float b) { return Min(a, b); },
        [](int a, int b) { return Min(a, b); }));
    lua.set_function("Max", sol::overload(
        [](float a, float b) { return Max(a, b); },
        [](int a, int b) { return Max(a, b); }));
    lua.set_function("Sin", static_cast<float (*)(float)>(&Sin));
    lua.set_function("Cos", static_cast<float (*)(float)>(&Cos));
    lua.set_function("Abs", sol::overload(
        [](float v) { return Abs(v); },
        [](int v) { return Abs(v); }));
}

} // namespace Urho3D
