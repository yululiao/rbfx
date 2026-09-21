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
        using LUA_THIS = Vector2;
        LUA_CLASS(Vector2,
            sol::call_constructor, sol::constructors<Vector2(), Vector2(float, float)>()
            LUA_MEMBER_PROP_RAW(x, &Vector2::x_)
            LUA_MEMBER_PROP_RAW(y, &Vector2::y_)
            LUA_MEMBER_FUNC(Length)
            LUA_MEMBER_FUNC(LengthSquared)
            LUA_MEMBER_FUNC(Normalized)
            LUA_MEMBER_FUNC_RAW(Dot, &Vector2::DotProduct)
            LUA_MEMBER_FUNC(Angle)
            LUA_MEMBER_FUNC(Lerp)
            LUA_META(equal_to, [](const Vector2& a, const Vector2& b) { return a == b; })
            LUA_META(addition, [](const Vector2& a, const Vector2& b) { return a + b; })
            LUA_META(subtraction, [](const Vector2& a, const Vector2& b) { return a - b; })
            LUA_META(unary_minus, [](const Vector2& a) { return -a; })
            LUA_META(multiplication, sol::overload(
                [](const Vector2& a, float s) { return a * s; },
                [](const Vector2& a, const Vector2& b) { return a * b; }))
            LUA_META(division, [](const Vector2& a, float s) { return a / s; })
            LUA_MEMBER_CONST(ZERO, Vector2::ZERO)
            LUA_MEMBER_CONST(ONE, Vector2::ONE)
            LUA_MEMBER_CONST(UP, Vector2::UP)
            LUA_MEMBER_CONST(RIGHT, Vector2::RIGHT)
            LUA_MEMBER_CONST(DOWN, Vector2::DOWN)
            LUA_MEMBER_CONST(LEFT, Vector2::LEFT)
        );
    }

    {
        using LUA_THIS = Vector3;
        LUA_CLASS(Vector3,
            sol::call_constructor, sol::constructors<Vector3(), Vector3(float, float, float)>()
            LUA_MEMBER_PROP_RAW(x, &Vector3::x_)
            LUA_MEMBER_PROP_RAW(y, &Vector3::y_)
            LUA_MEMBER_PROP_RAW(z, &Vector3::z_)
            LUA_MEMBER_FUNC_RAW(FromXZ, [](const Vector2& v, float y) { return Vector3(v.x_, y, v.y_); })
            LUA_MEMBER_FUNC(Length)
            LUA_MEMBER_FUNC(LengthSquared)
            LUA_MEMBER_FUNC(Normalized)
            LUA_MEMBER_FUNC_RAW(Dot, &Vector3::DotProduct)
            LUA_MEMBER_FUNC_RAW(Cross, &Vector3::CrossProduct)
            // tolua-style aliases used by the samples (23_Water).
            LUA_MEMBER_FUNC(DotProduct)
            LUA_MEMBER_FUNC(CrossProduct)
            LUA_MEMBER_FUNC(Lerp)
            LUA_MEMBER_FUNC(Angle)
            LUA_META(equal_to, [](const Vector3& a, const Vector3& b) { return a == b; })
            LUA_META(addition, [](const Vector3& a, const Vector3& b) { return a + b; })
            LUA_META(subtraction, [](const Vector3& a, const Vector3& b) { return a - b; })
            LUA_META(unary_minus, [](const Vector3& a) { return -a; })
            LUA_META(multiplication, sol::overload(
                [](const Vector3& a, float s) { return a * s; },
                [](const Vector3& a, const Vector3& b) { return a * b; }))
            LUA_META(division, [](const Vector3& a, float s) { return a / s; })
            LUA_MEMBER_CONST(ZERO, Vector3::ZERO)
            LUA_MEMBER_CONST(ONE, Vector3::ONE)
            LUA_MEMBER_CONST(UP, Vector3::UP)
            LUA_MEMBER_CONST(RIGHT, Vector3::RIGHT)
            LUA_MEMBER_CONST(DOWN, Vector3::DOWN)
            LUA_MEMBER_CONST(LEFT, Vector3::LEFT)
            LUA_MEMBER_CONST(FORWARD, Vector3::FORWARD)
            LUA_MEMBER_CONST(BACK, Vector3::BACK)
        );
    }

    {
        using LUA_THIS = Vector4;
        LUA_CLASS(Vector4,
            sol::call_constructor, sol::constructors<Vector4(), Vector4(float, float, float, float)>()
            LUA_MEMBER_PROP_RAW(x, &Vector4::x_)
            LUA_MEMBER_PROP_RAW(y, &Vector4::y_)
            LUA_MEMBER_PROP_RAW(z, &Vector4::z_)
            LUA_MEMBER_PROP_RAW(w, &Vector4::w_)
            LUA_META(equal_to, [](const Vector4& a, const Vector4& b) { return a == b; })
            LUA_META(addition, [](const Vector4& a, const Vector4& b) { return a + b; })
            LUA_META(subtraction, [](const Vector4& a, const Vector4& b) { return a - b; })
            LUA_META(multiplication, [](const Vector4& a, float s) { return a * s; })
            LUA_MEMBER_CONST(ZERO, Vector4::ZERO)
            LUA_MEMBER_CONST(ONE, Vector4::ONE)
        );
    }

    {
        using LUA_THIS = IntVector2;
        LUA_CLASS(IntVector2,
            sol::call_constructor, sol::factories(
                []() { return IntVector2(); },
                [](int x, int y) { return IntVector2(x, y); },
                // Lua arithmetic (e.g. Random() * width) yields floats
                [](double x, double y) { return IntVector2(static_cast<int>(x), static_cast<int>(y)); })
            LUA_MEMBER_PROP_RAW(x, &IntVector2::x_)
            LUA_MEMBER_PROP_RAW(y, &IntVector2::y_)
            LUA_MEMBER_FUNC(ToVector2)
            LUA_META(equal_to, [](const IntVector2& a, const IntVector2& b) { return a == b; })
            LUA_META(addition, [](const IntVector2& a, const IntVector2& b) { return a + b; })
            LUA_META(subtraction, [](const IntVector2& a, const IntVector2& b) { return a - b; })
            LUA_MEMBER_CONST(ZERO, IntVector2::ZERO)
            LUA_MEMBER_CONST(ONE, IntVector2::ONE)
        );
    }

    {
        using LUA_THIS = IntVector3;
        LUA_CLASS(IntVector3,
            sol::call_constructor, sol::constructors<IntVector3(), IntVector3(int, int, int)>()
            LUA_MEMBER_PROP_RAW(x, &IntVector3::x_)
            LUA_MEMBER_PROP_RAW(y, &IntVector3::y_)
            LUA_MEMBER_PROP_RAW(z, &IntVector3::z_)
            LUA_META(equal_to, [](const IntVector3& a, const IntVector3& b) { return a == b; })
            LUA_MEMBER_CONST(ZERO, IntVector3::ZERO)
            LUA_MEMBER_CONST(ONE, IntVector3::ONE)
        );
    }

    // 3 floats = euler angles, 4 floats = (w, x, y, z) components.
    {
        using LUA_THIS = Quaternion;
        LUA_CLASS(Quaternion,
            sol::call_constructor, sol::constructors<Quaternion(), Quaternion(float, float, float), Quaternion(float, float, float, float), Quaternion(float, const Vector3&), Quaternion(const Vector3&, const Vector3&)>()
            LUA_MEMBER_PROP_RAW(w, &Quaternion::w_)
            LUA_MEMBER_PROP_RAW(x, &Quaternion::x_)
            LUA_MEMBER_PROP_RAW(y, &Quaternion::y_)
            LUA_MEMBER_PROP_RAW(z, &Quaternion::z_)
            LUA_MEMBER_FUNC(FromAngleAxis)
            LUA_MEMBER_FUNC(FromEulerAngles)
            LUA_MEMBER_FUNC(FromRotationTo)
            LUA_MEMBER_FUNC(FromAxes)
            LUA_MEMBER_FUNC(YawAngle)
            LUA_MEMBER_FUNC(PitchAngle)
            LUA_MEMBER_FUNC(RollAngle)
            LUA_MEMBER_FUNC(Conjugate)
            LUA_MEMBER_FUNC(Inverse)
            LUA_MEMBER_FUNC(Slerp)
            LUA_MEMBER_FUNC(Normalized)
            LUA_META(equal_to, [](const Quaternion& a, const Quaternion& b) { return a == b; })
            LUA_META(multiplication, sol::overload(
                [](const Quaternion& a, const Quaternion& b) { return a * b; },
                [](const Quaternion& a, const Vector3& v) { return a * v; },
                [](const Quaternion& a, float s) { return a * s; }))
            LUA_MEMBER_CONST(IDENTITY, Quaternion::IDENTITY)
        );
    }

    {
        using LUA_THIS = Color;
        LUA_CLASS(Color,
            sol::call_constructor, sol::constructors<Color(), Color(float, float, float), Color(float, float, float, float)>()
            LUA_MEMBER_PROP_RAW(r, &Color::r_)
            LUA_MEMBER_PROP_RAW(g, &Color::g_)
            LUA_MEMBER_PROP_RAW(b, &Color::b_)
            LUA_MEMBER_PROP_RAW(a, &Color::a_)
            LUA_MEMBER_FUNC(Lerp)
            LUA_MEMBER_FUNC(ToHSV)
            LUA_MEMBER_FUNC(FromHSV)
            LUA_META(equal_to, [](const Color& a, const Color& b) { return a == b; })
            LUA_META(addition, [](const Color& a, const Color& b) { return a + b; })
            LUA_META(multiplication, sol::overload(
                [](const Color& a, float s) { return a * s; },
                [](const Color& a, const Color& b) { return a * b; }))
            LUA_MEMBER_CONST(WHITE, Color::WHITE)
            LUA_MEMBER_CONST(GRAY, Color::GRAY)
            LUA_MEMBER_CONST(BLACK, Color::BLACK)
            LUA_MEMBER_CONST(RED, Color::RED)
            LUA_MEMBER_CONST(GREEN, Color::GREEN)
            LUA_MEMBER_CONST(BLUE, Color::BLUE)
            LUA_MEMBER_CONST(CYAN, Color::CYAN)
            LUA_MEMBER_CONST(MAGENTA, Color::MAGENTA)
            LUA_MEMBER_CONST(YELLOW, Color::YELLOW)
            LUA_MEMBER_CONST(TRANSPARENT, Color::TRANSPARENT_BLACK)
        );
    }

    {
        using LUA_THIS = IntRect;
        LUA_CLASS(IntRect,
            sol::call_constructor, sol::factories(
                []() { return IntRect(); },
                [](int left, int top, int right, int bottom) { return IntRect(left, top, right, bottom); },
                [](double left, double top, double right, double bottom) {
                    return IntRect(static_cast<int>(left), static_cast<int>(top),
                        static_cast<int>(right), static_cast<int>(bottom));
                })
            LUA_MEMBER_PROP_RAW(left, &IntRect::left_)
            LUA_MEMBER_PROP_RAW(top, &IntRect::top_)
            LUA_MEMBER_PROP_RAW(right, &IntRect::right_)
            LUA_MEMBER_PROP_RAW(bottom, &IntRect::bottom_)
            LUA_MEMBER_FUNC_RAW(Size, [](const IntRect& r) { return r.Size(); })
            LUA_MEMBER_FUNC_RAW(Width, [](const IntRect& r) { return r.Width(); })
            LUA_MEMBER_FUNC_RAW(Height, [](const IntRect& r) { return r.Height(); })
            LUA_MEMBER_FUNC_RAW(IsInside, [](const IntRect& r, const IntVector2& point) { return r.IsInside(point); })
            LUA_META(equal_to, [](const IntRect& a, const IntRect& b) { return a == b; })
            LUA_MEMBER_CONST(ZERO, IntRect::ZERO)
        );
    }

    {
        using LUA_THIS = Rect;
        LUA_CLASS(Rect,
            sol::call_constructor, sol::constructors<Rect(), Rect(const Vector2&, const Vector2&), Rect(float, float, float, float)>()
            LUA_MEMBER_PROP_RAW(min, &Rect::min_)
            LUA_MEMBER_PROP_RAW(max, &Rect::max_)
            LUA_MEMBER_FUNC_RAW(Size, [](const Rect& r) { return r.Size(); })
            LUA_MEMBER_FUNC_RAW(Center, [](const Rect& r) { return r.Center(); })
            LUA_MEMBER_FUNC_OVERLOAD(IsInside,
                [](const Rect& r, const Vector2& point) { return r.IsInside(point); },
                [](const Rect& r, const Rect& rect) { return r.IsInside(rect); })
            LUA_META(equal_to, [](const Rect& a, const Rect& b) { return a == b; })
            LUA_MEMBER_CONST(FULL, Rect::FULL)
            LUA_MEMBER_CONST(POSITIVE, Rect::POSITIVE)
            LUA_MEMBER_CONST(ZERO, Rect::ZERO)
        );
    }

    {
        using LUA_THIS = BoundingBox;
        LUA_CLASS(BoundingBox,
            sol::call_constructor, sol::constructors<BoundingBox(), BoundingBox(const Vector3&, const Vector3&), BoundingBox(float, float)>()
            LUA_MEMBER_PROP_RAW(min, &BoundingBox::min_)
            LUA_MEMBER_PROP_RAW(max, &BoundingBox::max_)
            LUA_MEMBER_FUNC_RAW(Center, [](const BoundingBox& b) { return b.Center(); })
            LUA_MEMBER_FUNC_RAW(Size, [](const BoundingBox& b) { return b.Size(); })
            LUA_MEMBER_FUNC_RAW(HalfSize, [](const BoundingBox& b) { return b.HalfSize(); })
            LUA_MEMBER_FUNC_OVERLOAD(Merged,
                [](const BoundingBox& a, const BoundingBox& b) { return a.Merged(b); },
                [](const BoundingBox& a, const Vector3& b) { return a.Merged(b); })
            LUA_MEMBER_FUNC_OVERLOAD(IsInside,
                [](const BoundingBox& b, const BoundingBox& box) { return b.IsInside(box); },
                [](const BoundingBox& b, const Vector3& point) { return b.IsInside(point); })
            LUA_META(equal_to, [](const BoundingBox& a, const BoundingBox& b) { return a == b; })
        );
    }

    {
        using LUA_THIS = Plane;
        LUA_CLASS(Plane,
            sol::call_constructor, sol::constructors<
                Plane(),
                Plane(const Vector3&, const Vector3&),
                Plane(const Vector3&, const Vector3&, const Vector3&)>()
            LUA_MEMBER_PROP_RAW(normal, &Plane::normal_)
            LUA_MEMBER_PROP_RAW(d, &Plane::d_)
            LUA_MEMBER_FUNC_RAW(Distance, [](const Plane& p, const Vector3& point) { return p.Distance(point); })
        );
    }

    {
        using LUA_THIS = Ray;
        LUA_CLASS(Ray,
            sol::call_constructor, sol::constructors<Ray(), Ray(const Vector3&, const Vector3&)>()
            LUA_MEMBER_PROP_RAW(origin, &Ray::origin_)
            LUA_MEMBER_PROP_RAW(direction, &Ray::direction_)
            LUA_MEMBER_FUNC_OVERLOAD(HitDistance,
                LUA_CAST_C(HitDistance, float, const Plane&),
                LUA_CAST_C(HitDistance, float, const BoundingBox&),
                LUA_CAST_C(HitDistance, float, const Sphere&))
            LUA_MEMBER_FUNC(ClosestPoint)
        );
    }

    // Global random helpers mirroring MathDefs.h free functions.
    LUA_GLOBAL_FUNC(Random, sol::overload(
        static_cast<float (*)()>(&Random),
        static_cast<float (*)(float)>(&Random),
        static_cast<float (*)(float, float)>(&Random)));
    LUA_GLOBAL_FUNC(RandomNormal, &RandomNormal);
    // Seeding for reproducible or per-run random sequences (49/50 Sample2D
    // background color randomization).
    LUA_GLOBAL_FUNC(SetRandomSeed, &SetRandomSeed);
    LUA_GLOBAL_FUNC(GetRandomSeed, &GetRandomSeed);

    // Scalar helpers from MathDefs.h used across the samples.
    LUA_GLOBAL_FUNC(Clamp, [](float value, float min, float max) { return Clamp(value, min, max); });
    LUA_GLOBAL_FUNC(Lerp, [](float a, float b, float t) { return Lerp(a, b, t); });
    LUA_GLOBAL_FUNC(Min, sol::overload(
        [](float a, float b) { return Min(a, b); },
        [](int a, int b) { return Min(a, b); }));
    LUA_GLOBAL_FUNC(Max, sol::overload(
        [](float a, float b) { return Max(a, b); },
        [](int a, int b) { return Max(a, b); }));
    LUA_GLOBAL_FUNC(Sin, static_cast<float (*)(float)>(&Sin));
    LUA_GLOBAL_FUNC(Cos, static_cast<float (*)(float)>(&Cos));
    LUA_GLOBAL_FUNC(Abs, sol::overload(
        [](float v) { return Abs(v); },
        [](int v) { return Abs(v); }));
}

} // namespace Urho3D
