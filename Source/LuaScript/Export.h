//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#if defined(_WIN32) && defined(RBFXLUA_SHARED)
#   if defined(RBFXLUA_EXPORTS)
#       define RBFXLUA_API __declspec(dllexport)
#   else
#       define RBFXLUA_API __declspec(dllimport)
#   endif
#else
#   define RBFXLUA_API
#endif
