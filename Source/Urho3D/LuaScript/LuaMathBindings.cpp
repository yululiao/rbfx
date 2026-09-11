//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Precompiled.h"

#include "../LuaScript/LuaBindings.h"

#include "../Math/BoundingBox.h"
#include "../Math/Color.h"
#include "../Math/MathDefs.h"
#include "../Math/Plane.h"
#include "../Math/Random.h"
#include "../Math/Quaternion.h"
#include "../Math/RandomEngine.h"
#include "../Math/Ray.h"
#include "../Math/Rect.h"
#include "../Math/Sphere.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"

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
    lua.new_usertype<Vector2>("Vector2",
        sol::call_constructor, sol::constructors<Vector2(), Vector2(float, float)>(),
        "x", &Vector2::x_,
        "y", &Vector2::y_,
        "Length", &Vector2::Length,
        "LengthSquared", &Vector2::LengthSquared,
        "Normalized", &Vector2::Normalized,
        "Dot", &Vector2::DotProduct,
        "Angle", &Vector2::Angle,
        "Lerp", &Vector2::Lerp,
        sol::meta_function::equal_to, [](const Vector2& a, const Vector2& b) { return a == b; },
        sol::meta_function::addition, [](const Vector2& a, const Vector2& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vector2& a, const Vector2& b) { return a - b; },
        sol::meta_function::unary_minus, [](const Vector2& a) { return -a; },
        sol::meta_function::multiplication, sol::overload(
            [](const Vector2& a, float s) { return a * s; },
            [](const Vector2& a, const Vector2& b) { return a * b; }),
        sol::meta_function::division, [](const Vector2& a, float s) { return a / s; },
        "ZERO", sol::var(Vector2::ZERO),
        "ONE", sol::var(Vector2::ONE),
        "UP", sol::var(Vector2::UP),
        "RIGHT", sol::var(Vector2::RIGHT),
        "DOWN", sol::var(Vector2::DOWN),
        "LEFT", sol::var(Vector2::LEFT)
    );

    lua.new_usertype<Vector3>("Vector3",
        sol::call_constructor, sol::constructors<Vector3(), Vector3(float, float, float)>(),
        "x", &Vector3::x_,
        "y", &Vector3::y_,
        "z", &Vector3::z_,
        "FromXZ", [](const Vector2& v, float y) { return Vector3(v.x_, y, v.y_); },
        "Length", &Vector3::Length,
        "LengthSquared", &Vector3::LengthSquared,
        "Normalized", &Vector3::Normalized,
        "Dot", &Vector3::DotProduct,
        "Cross", &Vector3::CrossProduct,
        // tolua-style aliases used by the samples (23_Water).
        "DotProduct", &Vector3::DotProduct,
        "CrossProduct", &Vector3::CrossProduct,
        "Lerp", &Vector3::Lerp,
        "Angle", &Vector3::Angle,
        sol::meta_function::equal_to, [](const Vector3& a, const Vector3& b) { return a == b; },
        sol::meta_function::addition, [](const Vector3& a, const Vector3& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vector3& a, const Vector3& b) { return a - b; },
        sol::meta_function::unary_minus, [](const Vector3& a) { return -a; },
        sol::meta_function::multiplication, sol::overload(
            [](const Vector3& a, float s) { return a * s; },
            [](const Vector3& a, const Vector3& b) { return a * b; }),
        sol::meta_function::division, [](const Vector3& a, float s) { return a / s; },
        "ZERO", sol::var(Vector3::ZERO),
        "ONE", sol::var(Vector3::ONE),
        "UP", sol::var(Vector3::UP),
        "RIGHT", sol::var(Vector3::RIGHT),
        "DOWN", sol::var(Vector3::DOWN),
        "LEFT", sol::var(Vector3::LEFT),
        "FORWARD", sol::var(Vector3::FORWARD),
        "BACK", sol::var(Vector3::BACK)
    );

    lua.new_usertype<Vector4>("Vector4",
        sol::call_constructor, sol::constructors<Vector4(), Vector4(float, float, float, float)>(),
        "x", &Vector4::x_,
        "y", &Vector4::y_,
        "z", &Vector4::z_,
        "w", &Vector4::w_,
        sol::meta_function::equal_to, [](const Vector4& a, const Vector4& b) { return a == b; },
        sol::meta_function::addition, [](const Vector4& a, const Vector4& b) { return a + b; },
        sol::meta_function::subtraction, [](const Vector4& a, const Vector4& b) { return a - b; },
        sol::meta_function::multiplication, [](const Vector4& a, float s) { return a * s; },
        "ZERO", sol::var(Vector4::ZERO),
        "ONE", sol::var(Vector4::ONE)
    );

    lua.new_usertype<IntVector2>("IntVector2",
        sol::call_constructor, sol::factories(
            []() { return IntVector2(); },
            [](int x, int y) { return IntVector2(x, y); },
            // Lua arithmetic (e.g. Random() * width) yields floats
            [](double x, double y) { return IntVector2(static_cast<int>(x), static_cast<int>(y)); }),
        "x", &IntVector2::x_,
        "y", &IntVector2::y_,
        "ToVector2", &IntVector2::ToVector2,
        sol::meta_function::equal_to, [](const IntVector2& a, const IntVector2& b) { return a == b; },
        sol::meta_function::addition, [](const IntVector2& a, const IntVector2& b) { return a + b; },
        sol::meta_function::subtraction, [](const IntVector2& a, const IntVector2& b) { return a - b; },
        "ZERO", sol::var(IntVector2::ZERO),
        "ONE", sol::var(IntVector2::ONE)
    );

    lua.new_usertype<IntVector3>("IntVector3",
        sol::call_constructor, sol::constructors<IntVector3(), IntVector3(int, int, int)>(),
        "x", &IntVector3::x_,
        "y", &IntVector3::y_,
        "z", &IntVector3::z_,
        sol::meta_function::equal_to, [](const IntVector3& a, const IntVector3& b) { return a == b; },
        "ZERO", sol::var(IntVector3::ZERO),
        "ONE", sol::var(IntVector3::ONE)
    );

    // 3 floats = euler angles, 4 floats = (w, x, y, z) components.
    lua.new_usertype<Quaternion>("Quaternion",
        sol::call_constructor, sol::constructors<Quaternion(), Quaternion(float, float, float), Quaternion(float, float, float, float), Quaternion(float, const Vector3&), Quaternion(const Vector3&, const Vector3&)>(),
        "w", &Quaternion::w_,
        "x", &Quaternion::x_,
        "y", &Quaternion::y_,
        "z", &Quaternion::z_,
        "FromAngleAxis", &Quaternion::FromAngleAxis,
        "FromEulerAngles", &Quaternion::FromEulerAngles,
        "FromRotationTo", &Quaternion::FromRotationTo,
        "FromAxes", &Quaternion::FromAxes,
        "YawAngle", &Quaternion::YawAngle,
        "PitchAngle", &Quaternion::PitchAngle,
        "RollAngle", &Quaternion::RollAngle,
        "Conjugate", &Quaternion::Conjugate,
        "Inverse", &Quaternion::Inverse,
        "Slerp", &Quaternion::Slerp,
        "Normalized", &Quaternion::Normalized,
        sol::meta_function::equal_to, [](const Quaternion& a, const Quaternion& b) { return a == b; },
        sol::meta_function::multiplication, sol::overload(
            [](const Quaternion& a, const Quaternion& b) { return a * b; },
            [](const Quaternion& a, const Vector3& v) { return a * v; },
            [](const Quaternion& a, float s) { return a * s; }),
        "IDENTITY", sol::var(Quaternion::IDENTITY)
    );

    lua.new_usertype<Color>("Color",
        sol::call_constructor, sol::constructors<Color(), Color(float, float, float), Color(float, float, float, float)>(),
        "r", &Color::r_,
        "g", &Color::g_,
        "b", &Color::b_,
        "a", &Color::a_,
        "Lerp", &Color::Lerp,
        "ToHSV", &Color::ToHSV,
        "FromHSV", &Color::FromHSV,
        sol::meta_function::equal_to, [](const Color& a, const Color& b) { return a == b; },
        sol::meta_function::addition, [](const Color& a, const Color& b) { return a + b; },
        sol::meta_function::multiplication, sol::overload(
            [](const Color& a, float s) { return a * s; },
            [](const Color& a, const Color& b) { return a * b; }),
        "WHITE", sol::var(Color::WHITE),
        "GRAY", sol::var(Color::GRAY),
        "BLACK", sol::var(Color::BLACK),
        "RED", sol::var(Color::RED),
        "GREEN", sol::var(Color::GREEN),
        "BLUE", sol::var(Color::BLUE),
        "CYAN", sol::var(Color::CYAN),
        "MAGENTA", sol::var(Color::MAGENTA),
        "YELLOW", sol::var(Color::YELLOW),
        "TRANSPARENT", sol::var(Color::TRANSPARENT_BLACK)
    );

    lua.new_usertype<IntRect>("IntRect",
        sol::call_constructor, sol::factories(
            []() { return IntRect(); },
            [](int left, int top, int right, int bottom) { return IntRect(left, top, right, bottom); },
            [](double left, double top, double right, double bottom) {
                return IntRect(static_cast<int>(left), static_cast<int>(top),
                    static_cast<int>(right), static_cast<int>(bottom));
            }),
        "left", &IntRect::left_,
        "top", &IntRect::top_,
        "right", &IntRect::right_,
        "bottom", &IntRect::bottom_,
        "Size", [](const IntRect& r) { return r.Size(); },
        "Width", [](const IntRect& r) { return r.Width(); },
        "Height", [](const IntRect& r) { return r.Height(); },
        "IsInside", [](const IntRect& r, const IntVector2& point) { return r.IsInside(point); },
        sol::meta_function::equal_to, [](const IntRect& a, const IntRect& b) { return a == b; },
        "ZERO", sol::var(IntRect::ZERO)
    );

    lua.new_usertype<Rect>("Rect",
        sol::call_constructor, sol::constructors<Rect(), Rect(const Vector2&, const Vector2&), Rect(float, float, float, float)>(),
        "min", &Rect::min_,
        "max", &Rect::max_,
        "Size", [](const Rect& r) { return r.Size(); },
        "Center", [](const Rect& r) { return r.Center(); },
        "IsInside", sol::overload(
            [](const Rect& r, const Vector2& point) { return r.IsInside(point); },
            [](const Rect& r, const Rect& rect) { return r.IsInside(rect); }),
        sol::meta_function::equal_to, [](const Rect& a, const Rect& b) { return a == b; },
        "FULL", sol::var(Rect::FULL),
        "POSITIVE", sol::var(Rect::POSITIVE),
        "ZERO", sol::var(Rect::ZERO)
    );

    lua.new_usertype<BoundingBox>("BoundingBox",
        sol::call_constructor, sol::constructors<BoundingBox(), BoundingBox(const Vector3&, const Vector3&), BoundingBox(float, float)>(),
        "min", &BoundingBox::min_,
        "max", &BoundingBox::max_,
        "Center", [](const BoundingBox& b) { return b.Center(); },
        "Size", [](const BoundingBox& b) { return b.Size(); },
        "HalfSize", [](const BoundingBox& b) { return b.HalfSize(); },
        "Merged", sol::overload(
            [](const BoundingBox& a, const BoundingBox& b) { return a.Merged(b); },
            [](const BoundingBox& a, const Vector3& b) { return a.Merged(b); }),
        "IsInside", sol::overload(
            [](const BoundingBox& b, const BoundingBox& box) { return b.IsInside(box); },
            [](const BoundingBox& b, const Vector3& point) { return b.IsInside(point); }),
        sol::meta_function::equal_to, [](const BoundingBox& a, const BoundingBox& b) { return a == b; }
    );

    lua.new_usertype<Plane>("Plane",
        sol::call_constructor, sol::constructors<
            Plane(),
            Plane(const Vector3&, const Vector3&),
            Plane(const Vector3&, const Vector3&, const Vector3&)>(),
        "normal", &Plane::normal_,
        "d", &Plane::d_,
        "Distance", [](const Plane& p, const Vector3& point) { return p.Distance(point); }
    );

    lua.new_usertype<Ray>("Ray",
        sol::call_constructor, sol::constructors<Ray(), Ray(const Vector3&, const Vector3&)>(),
        "origin", &Ray::origin_,
        "direction", &Ray::direction_,
        "HitDistance", sol::overload(
            static_cast<float (Ray::*)(const Plane&) const>(&Ray::HitDistance),
            static_cast<float (Ray::*)(const BoundingBox&) const>(&Ray::HitDistance),
            static_cast<float (Ray::*)(const Sphere&) const>(&Ray::HitDistance)),
        "ClosestPoint", &Ray::ClosestPoint
    );

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
