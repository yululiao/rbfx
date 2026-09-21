#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
rbfx Lua API stub generator (A2 -- names layer, source-parsing based).

Parses the engine / editor sol3 (sol2) binding sources and emits a LuaLS
``---@meta`` definition file (``*.d.lua``) so the editor gets symbol, member
and enum completion for everything the C++ bindings register.

Why source parsing and not a runtime dump?
    sol3 deliberately keeps *instance* members off the "named" metatable
    (usertype_storage.hpp: ``string_for_each_metatable_func`` returns early for
    ``submetatable_type::named``). A runtime walk of _G therefore sees class
    names, enums, global functions and static members -- but NOT instance or
    inherited methods. The binding sources are the only place that still has
    that information (plus ``sol::bases<>`` for the inheritance chain), so we
    read the API surface from there.

This is a names-layer tool: it recovers "class -> method/field", enum keys and
global symbols. It does NOT recover C++ signatures, parameter/return types or
numeric enum values (not expressible from the registration text), so value
propagation across local variables still needs explicit ``---@type`` hints.

Usage:
    python generate_api_docs.py --src <file_or_dir> [...] --out <path.d.lua>
"""

import argparse
import os
import sys
import re


# ---------------------------------------------------------------------------
# Tokenizer
# ---------------------------------------------------------------------------

def tokenize(text):
    """Return a list of (kind, value) tokens with comments stripped.

    kind in: 'id' identifier | 'str' string literal (decoded body) |
    'char' char literal | 'num' number | 'punct' punctuation.
    """
    toks = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in ' \t\r\n':
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j + 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        # raw string  R"delim( ... )delim"   (contents ignored)
        if c == 'R' and i + 1 < n and text[i + 1] == '"':
            j = i + 2
            d = j
            while d < n and text[d] != '(':
                d += 1
            delim = text[j:d]
            closer = ')' + delim + '"'
            end = text.find(closer, d + 1)
            i = (end + len(closer)) if end >= 0 else n
            continue
        if c == '"':
            i += 1
            buf = []
            while i < n:
                ch = text[i]
                if ch == '\\':
                    buf.append(text[i + 1] if i + 1 < n else '')
                    i += 2
                    continue
                if ch == '"':
                    i += 1
                    break
                buf.append(ch)
                i += 1
            toks.append(('str', ''.join(buf)))
            continue
        if c == "'":
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                i += 1
            toks.append(('char', ''))
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            toks.append(('id', text[i:j]))
            i = j
            continue
        if c.isdigit():
            j = i
            while j < n and (text[j].isalnum() or text[j] == '.'):
                j += 1
            toks.append(('num', text[i:j]))
            i = j
            continue
        if c == ':' and i + 1 < n and text[i + 1] == ':':
            toks.append(('punct', '::'))
            i += 2
            continue
        toks.append(('punct', c))
        i += 1
    return toks


# ---------------------------------------------------------------------------
# Token helpers
# ---------------------------------------------------------------------------

def match_seq(toks, i, seq):
    for off, (k, v) in enumerate(seq):
        if i + off >= len(toks):
            return -1
        kk, vv = toks[i + off]
        if kk != k or (v is not None and vv != v):
            return -1
    return i + len(seq)


def skip_balanced(toks, i, open_v, close_v):
    depth = 0
    j = i
    n = len(toks)
    while j < n:
        k, v = toks[j]
        if k == 'punct':
            if v == open_v:
                depth += 1
            elif v == close_v:
                depth -= 1
                if depth == 0:
                    return j + 1
        j += 1
    return n


def split_args(toks):
    """Split a token list on top-level commas (brackets balanced)."""
    groups = []
    start = 0
    dp = db = dc = da = 0
    for k in range(len(toks)):
        kk, vv = toks[k]
        if kk != 'punct':
            continue
        if vv == '(':
            dp += 1
        elif vv == ')':
            dp = max(0, dp - 1)
        elif vv == '[':
            db += 1
        elif vv == ']':
            db = max(0, db - 1)
        elif vv == '{':
            dc += 1
        elif vv == '}':
            dc = max(0, dc - 1)
        elif vv in '<>' and dp == db == dc == 0:
            # only treat angle brackets as templates at the argument surface;
            # inside lambda bodies they are comparison/-> noise.
            if vv == '<':
                da += 1
            elif da > 0:
                da -= 1
        elif vv == ',' and dp == db == dc == da == 0:
            groups.append(toks[start:k])
            start = k + 1
    if toks[start:]:
        groups.append(toks[start:])
    return groups


def split_macro_args(toks):
    """Split MACRO arguments the way the C++ preprocessor does: on commas
    that are not inside PARENTHESES. Braces and angle brackets do not protect
    a comma from the preprocessor's textual splitter, so this deliberately
    drops the bracket/brace/angle tracking of split_args -- keeping the LUA
    expander in lockstep with what MSVC/GCC/Clang actually see (a call the
    compiler would reject as C4002 must fail parity here too, not pass)."""
    groups = []
    start = 0
    dp = 0
    for k in range(len(toks)):
        kk, vv = toks[k]
        if kk != 'punct':
            continue
        if vv == '(':
            dp += 1
        elif vv == ')':
            dp = max(0, dp - 1)
        elif vv == ',' and dp == 0:
            groups.append(toks[start:k])
            start = k + 1
    if toks[start:]:
        groups.append(toks[start:])
    return groups


def qualified_head(toks):
    """Leading id/::/. chain joined as text, e.g. sol::property."""
    parts = []
    i = 0
    while i < len(toks):
        k, v = toks[i]
        if k == 'id':
            parts.append(v)
            i += 1
        elif k == 'punct' and v in ('::', '.'):
            parts.append(v)
            i += 1
        else:
            break
    return ''.join(parts)


def qualified_id(toks):
    """Whole-run id/:: chain as text (std::string), or None if anything else
    appears -- macro TYPE arguments must be a qualified type name and nothing
    more (no cv/ref/brackets), so malformed uses fail loudly in parity."""
    if not toks:
        return None
    parts = []
    for k, v in toks:
        if k == 'id':
            parts.append(v)
        elif k == 'punct' and v == '::':
            parts.append(v)
        else:
            return None
    return ''.join(parts)


def last_ident(toks):
    last = None
    for k, v in toks:
        if k == 'id':
            last = v
    return last


def is_identifier(s):
    return bool(re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', s))


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

class Symbol:
    def __init__(self, name, is_class):
        self.name = name
        self.is_class = is_class
        self.members = {}      # member name -> {'kind': 'method'|'field', 'ret': C++ base or None}
        self.bases = []        # C++ simple type names
        self.constructible = False

    def add(self, name, kind, ret=None):
        if is_identifier(name) and name not in self.members:
            self.members[name] = {'kind': kind, 'ret': ret}


class Api:
    def __init__(self):
        self.symbols = {}   # qualified name -> Symbol
        self.globals = {}   # name -> {'kind': 'function'|'const', 'ret': C++ base or None}

    def symbol(self, name, is_class):
        s = self.symbols.get(name)
        if s is None:
            s = Symbol(name, is_class)
            self.symbols[name] = s
        elif is_class:
            s.is_class = True
        return s


# ---------------------------------------------------------------------------
# Classification
# ---------------------------------------------------------------------------

def usertype_member_kind(value_toks, key):
    head = qualified_head(value_toks)
    if head in ('sol::property', 'sol::readonly_property', 'sol::var', 'sol::readonly'):
        return 'field'
    if value_toks and value_toks[0] == ('punct', '&'):
        tname = last_ident(value_toks)
        if tname and tname.endswith('_'):
            return 'field'  # rbfx data members end with '_'
    return 'method' if (key and key[0].isupper()) else 'field'


def assign_value_kind(value_toks):
    """Classify `tbl["key"] = <value>` (non-usertype)."""
    if not value_toks:
        return 'field'
    first = value_toks[0]
    head = qualified_head(value_toks)
    if first == ('punct', '['):
        return 'method'
    if first == ('punct', '&') or head in ('sol::overload',):
        return 'method'
    if head in ('sol::property', 'sol::readonly_property', 'sol::var'):
        return 'field'
    return 'field'  # bare enum constant / number / string / expression


# C++ -> Lua type mapping for the scalar / builtin returns that appear in the bindings.
# Anything not listed here (sol::object, custom enums, templates, void) is left untyped on
# purpose: a wrong ---@return/---@type is worse than none, because it would propagate bogus
# completions down the chain. Engine classes are resolved separately against the exported set.
_CPP_SCALARS = {
    'string': 'string', 'char': 'string', 'wstring': 'string',
    'bool': 'boolean',
    'int': 'integer', 'unsigned': 'integer', 'long': 'integer', 'size_t': 'integer',
    'int8_t': 'integer', 'int16_t': 'integer', 'int32_t': 'integer', 'int64_t': 'integer',
    'uint8_t': 'integer', 'uint16_t': 'integer', 'uint32_t': 'integer', 'uint64_t': 'integer',
    'float': 'number', 'double': 'number',
    'table': 'table',
}

# rbfx wraps many return values in these; unwrap to the pointed-to class so chains like
# `local child = node:CreateChild()` (returns SharedPtr<Node>) propagate their type.
_CPP_WRAPPERS = {'SharedPtr', 'WeakPtr'}
_TYPE_NOISE_IDS = {'const', 'volatile'}

# Methods / global functions that fetch or create ONE object by a type-name string and return
# a dynamic sol::object. Because the first argument is a literal class name, we emit one
# `---@overload fun(self?, typeName: "X", ...): X` per exported class derived from the given
# base (Component or Resource), so LuaLS resolves the concrete type from the call site
# (e.g. node:GetComponent("Zone") -> Zone, GetResource("Animation", ..) -> Animation).
# extra = trailing (param, type) pairs after typeName. The base signature still accepts any
# string so engine types with no dedicated Lua usertype stay legal (generic fallback).
FACTORY_SPECS = {
    'CreateComponent':      ('Component', []),
    'GetOrCreateComponent': ('Component', []),
    'GetComponent':         ('Component', []),
    'GetResource':          ('Resource', [('resourceName', 'string')]),
    'GetResourceOrThrow':   ('Resource', [('resourceName', 'string')]),
}

# Identifiers that name the (global) sol state/environment. Bindings mostly use `lua.`, but
# LuaVM.cpp registers core globals (SubscribeToEvent, EventData, ...) through `luaState_->`;
# treat those as the global table too (prefix '').
_GLOBAL_STATE_VARS = {'luaState_', 'state', 'solState_'}


def _resolve_cpp_type(seg):
    """Reduce a C++ declarator token run to a simple type name (last identifier of the
    base type, after stripping const/*/& and unwrapping SharedPtr<T>/WeakPtr<T>).
    Returns None for anything else templated (vector/optional/...) which is not nameable."""
    seg = [(k, v) for k, v in seg
           if not (k == 'punct' and v in ('*', '&'))
           and not (k == 'id' and v in _TYPE_NOISE_IDS)]
    if not seg:
        return None
    for idx in range(len(seg) - 1):
        if seg[idx][0] == 'id' and seg[idx + 1] == ('punct', '<'):
            if seg[idx][1] not in _CPP_WRAPPERS:
                return None              # non-smart-pointer template: unknown element type
            depth = 0
            end = idx + 1
            while end < len(seg):
                if seg[end] == ('punct', '<'):
                    depth += 1
                elif seg[end] == ('punct', '>'):
                    depth -= 1
                    if depth == 0:
                        break
                end += 1
            return _resolve_cpp_type(seg[idx + 2:end])   # unwrap SharedPtr<T> -> T
    ids = [v for k, v in seg if k == 'id']
    return ids[-1] if ids else None      # std::string -> 'string', Vector3 -> 'Vector3'


def _wrap_lua_object_as(toks):
    """Recover the Lua-facing type of a cached object accessor: the lambda
    returns sol::object (the identity-cache wrapper, not nameable as a trailing
    return), so the intended type rides on WrapLuaObjectAs<X>(...) in the body."""
    n = len(toks)
    for i in range(n - 1):
        if toks[i] == ('id', 'WrapLuaObjectAs') and toks[i + 1] == ('punct', '<'):
            end = skip_balanced(toks, i + 1, '<', '>')
            return _resolve_cpp_type(toks[i + 2:end - 1])
    return None


def extract_return_base(toks):
    """Return the C++ *simple* name of a lambda's trailing return type, e.g.
    `-> Scene*` -> 'Scene', `-> std::string` -> 'string', `-> SharedPtr<Node>` -> 'Node'.
    Looks at the first `->` (trailing return); returns None when there is no explicit
    trailing return (member pointer, expression lambda) or the type is not cleanly nameable."""
    L = len(toks)
    arrow = -1
    i = 0
    while i < L - 1:
        if toks[i] == ('punct', '-') and toks[i + 1] == ('punct', '>'):
            arrow = i
            break
        i += 1
    if arrow < 0:
        return None
    seg = []
    j = arrow + 2
    while j < L:
        k, v = toks[j]
        if k == 'punct' and v == '{':
            break                        # lambda body begins -> the declarator ended
        if k == 'punct' and v in ('(', ')', ';', '[', ']'):
            return None                  # function-pointer / parenthesized declarator
        seg.append((k, v))
        j += 1
    base = _resolve_cpp_type(seg)
    if base in (None, 'object'):
        # Cached object accessors push sol::object; recover the Lua-facing type
        # from WrapLuaObjectAs<X>(...) so routing an accessor through the
        # identity cache does not drop its ---@type / ---@return annotation.
        wrapped = _wrap_lua_object_as(toks)
        if wrapped:
            return wrapped
    return base


def cpp_to_lua_type(base, exported):
    """Map a C++ return base name to a LuaLS type, or None to omit the annotation."""
    if not base:
        return None
    if base in exported:
        return base
    return _CPP_SCALARS.get(base)


def parse_bases(toks):
    i = 0
    while i < len(toks) and toks[i] != ('punct', '<'):
        i += 1
    if i >= len(toks):
        return []
    end = skip_balanced(toks, i, '<', '>')
    inner = toks[i + 1:end - 1]
    return [last_ident(p) for p in split_args(inner) if last_ident(p)]


def parse_usertype_body(sym, args):
    # In new_usertype("Name", "key", value, "key2", value2, sol::meta_function::x, v, ...)
    # every comma-separated token group is its OWN argument, so keys and values are
    # paired by walking with a cursor rather than inspected one-by-one.
    idx = 0
    na = len(args)
    while idx < na:
        a = args[idx]
        if not a:
            idx += 1
            continue
        if a[0][0] == 'str':  # string key -> following argument is its binding
            key = a[0][1]
            val = args[idx + 1] if idx + 1 < na else []
            sym.add(key, usertype_member_kind(val, key), extract_return_base(val))
            idx += 2
            continue
        head = qualified_head(a)
        if head.startswith('sol::meta_function'):  # meta_function selector + its value
            idx += 2
            continue
        if head.endswith('::base_classes'):  # next argument is sol::bases<...>()
            if idx + 1 < na:
                sym.bases.extend(parse_bases(args[idx + 1]))
            idx += 2
            continue
        if head.endswith('::bases'):
            sym.bases.extend(parse_bases(a))
            idx += 1
            continue
        if head.endswith('::no_constructor') or head.endswith('::call_constructor'):
            idx += 1
            continue
        if (head.startswith('sol::constructor') or head.endswith('::constructors')
                or head.endswith('::factories')):
            sym.constructible = True
            idx += 1
            continue
        idx += 1


def member_access(toks, i):
    """If toks[i] is an id followed by `.` or `->` and an id member, return
    (object_name, member_name, index_after_member); else None. Handles both
    `lua.set_function` and `luaState_->set_function` registration styles."""
    if toks[i][0] != 'id':
        return None
    n = len(toks)
    if i + 2 < n and toks[i + 1] == ('punct', '.'):
        p = i + 2
    elif i + 3 < n and toks[i + 1] == ('punct', '-') and toks[i + 2] == ('punct', '>'):
        p = i + 3
    else:
        return None
    if p >= n or toks[p][0] != 'id':
        return None
    return toks[i][1], toks[p][1], p + 1


# ---------------------------------------------------------------------------
# LuaBindMacros.h one-line family: token-level expansion
# ---------------------------------------------------------------------------
# Source/LuaScript/LuaBindMacros.h collapses the recurring sol3 registration
# patterns into the LUA_* macro family (one line per member, leading-comma
# convention, ejoy-style two-axis naming: LUA_<subject>_<shape>). The parser
# itself understands the hand-written form, so before any pass runs every
# LUA_* call is expanded back to the equivalent hand-written tokens. The
# templates below mirror the #define bodies in LuaBindMacros.h; the parity
# guard locks the two together. A macro the expander does not know, a call
# with the wrong arity, or a call filed in the wrong BUCKET (func raw vs prop
# raw vs const -- enforced by the value-head checks) is kept verbatim -- the
# parser then ignores it, the stub comes out missing that member, and
# check_api_docs.ps1 fails loudly instead of dropping it silently.

def _expand_rbfx(name, args, cls):
    """Expand one LUA_* macro call to the equivalent hand-written token run.
    cls is the usertype bound by the enclosing `using LUA_THIS = X;` block
    (None outside any block). Returns None for an unknown macro, a call that
    breaks its contract, or a class-bearing macro used outside a block --
    parity then fails loudly instead of dropping the member silently."""
    def id_args():
        out = []
        for a in args:
            if len(a) != 1 or a[0][0] != 'id':
                return None
            out.append(a[0][1])
        return out

    if name == 'LUA_CLASS':
        if len(args) < 1 or len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # Registration opener: NAME stringizes into the Lua type name, so
        # the hand-written form sees `lua.new_usertype<NAME>("NAME", ...`.
        # The remaining arguments re-join comma-separated; nested list macros
        # (LUA_BASES and friends) expand on the rescan pass.
        n = args[0][0][1]
        out = tokenize('lua.new_usertype<%s>("%s"' % (n, n))
        for a in args[1:]:
            out += [('punct', ',')] + a
        out.append(('punct', ')'))
        return out
    if name == 'LUA_GLOBAL_FUNC':
        if len(args) < 2 or len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # ejoy lua_global_func: expands to lua.set_function("NAME", ...) so
        # the parser's set_function branch emits the global entry exactly as
        # with the literal form.
        out = tokenize('lua.set_function("%s"' % args[0][0][1])
        for a in args[1:]:
            out += [('punct', ',')] + a
        out.append(('punct', ')'))
        return out
    if name == 'LUA_BASES':
        if not cls:
            return None
        ids = id_args()
        if not ids or len(ids) < 1:
            return None
        return tokenize(', sol::base_classes, LuaBases<%s, %s>::bases(lua)'
                        % (cls, ', '.join(ids)))
    if name == 'LUA_MEMBER_FUNC':
        if not cls:
            return None
        ids = id_args()
        if not ids or len(ids) != 1:
            return None
        member = ids[0]
        return tokenize(', "%s", &%s::%s' % (member, cls, member))
    if name == 'LUA_MEMBER_FUNC_RET':
        if not cls:
            return None
        if len(args) != 2:
            return None
        member = args[0][0][1] if len(args[0]) == 1 and args[0][0][0] == 'id' else None
        ret = qualified_id(args[1])
        if not (member and ret):
            return None
        # Same bare member pointer as LUA_MEMBER_FUNC C++-side; RET only re-synthesizes
        # the `-> RET` trailing return the stub scanner reads (---@return).
        return tokenize(', "%s", [](%s* self) -> %s { return self->%s(); }'
                        % (member, cls, ret, member))
    if name == 'LUA_MEMBER_FUNC_ENUM':
        if not cls:
            return None
        ids = id_args()
        if not ids or len(ids) != 2:
            return None
        member, enum = ids
        return tokenize(', "%s", [](%s* self, int value) '
                        '{ if (self) self->%s(static_cast<%s>(value)); }'
                        % (member, cls, member, enum))
    if name == 'LUA_MEMBER_PROP_OBJ_R':
        if not cls:
            return None
        ids = id_args()
        if not ids or len(ids) != 3:
            return None
        key, ret, getter = ids
        return tokenize(', "%s", sol::readonly_property([](%s* self, sol::this_state s) '
                        '-> sol::object { return self ? WrapLuaObjectAs<%s>'
                        '(sol::state_view(s), self->%s()) : sol::lua_nil; })'
                        % (key, cls, ret, getter))
    if name == 'LUA_MEMBER_FUNC_OBJ':
        if not cls:
            return None
        ids = id_args()
        if not ids or len(ids) != 3:
            return None
        key, ret, getter = ids
        return tokenize(', "%s", [](%s* self, sol::this_state s) '
                        '-> sol::object { return self ? WrapLuaObjectAs<%s>'
                        '(sol::state_view(s), self->%s()) : sol::lua_nil; }'
                        % (key, cls, ret, getter))
    if name == 'LUA_ENUM_TABLE':
        if len(args) < 3 or len(args) % 2 == 0:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        table = args[0][0][1]
        out = tokenize('sol::table %s = lua.create_named_table("%s");'
                       % (table, table))
        for j in range(1, len(args), 2):
            key, val = args[j], args[j + 1]
            if not key or key[0][0] != 'str':
                return None
            # Rebuild the value tokens as text so the loop stays in token land;
            # values here are enum constants / numbers, never nested lambdas.
            text = ''.join(v if k != 'str' else '"%s"' % v for k, v in val)
            out += tokenize('%s["%s"] = %s;' % (table, key[0][1], text))
        return out
    if name == 'LUA_MEMBER_FUNC_RAW':
        if len(args) != 2 or not args[1]:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # Bucket contract: a method-shaped value only. A property wrapper
        # or a sol::var constant belongs to LUA_MEMBER_PROP_RAW / _CONST;
        # misfiling returns None and fails parity loudly.
        if qualified_head(args[1]) in ('sol::property', 'sol::readonly_property',
                                       'sol::var'):
            return None
        return [('punct', ','), ('str', args[0][0][1]), ('punct', ',')] + args[1]
    if name == 'LUA_MEMBER_PROP_RAW':
        if len(args) != 2 or not args[1]:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # Bucket contract: a property wrapper (sol::property family) or a
        # bare rbfx data-member pointer (&X::y_ -- trailing underscore is
        # the generator's own field-vs-method discriminator).
        head = qualified_head(args[1])
        is_data_member = (args[1][0] == ('punct', '&')
                          and args[1][-1][0] == 'id'
                          and args[1][-1][1].endswith('_'))
        if head not in ('sol::property', 'sol::readonly_property') \
                and not is_data_member:
            return None
        return [('punct', ','), ('str', args[0][0][1]), ('punct', ',')] + args[1]
    if name == 'LUA_MEMBER_CONST':
        if len(args) != 2 or not args[1]:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # The C++ macro wraps VALUE in sol::var() itself, so re-add the
        # wrapper here to keep the hand-written token shape.
        return [('punct', ','), ('str', args[0][0][1]), ('punct', ',')] \
            + tokenize('sol::var(') + args[1] + [('punct', ')')]
    if name == 'LUA_META':
        if len(args) != 2 or not args[1]:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # sol::meta_function::SELECTOR passthrough -- not a stub member, the
        # parser skips the non-string key exactly as with the literal form.
        return tokenize(', sol::meta_function::%s' % args[0][0][1]) \
            + [('punct', ',')] + args[1]
    if name == 'LUA_MEMBER_PROP_F':
        if not cls or len(args) != 4:
            return None
        def single(a):
            return a[0][1] if len(a) == 1 and a[0][0] == 'id' else None
        key, getter, setter = (single(args[i]) for i in (0, 2, 3))
        # TYPE may be a qualified chain (std::string, ea::string) -- accept
        # id/:: runs there, not just a single identifier.
        type_ = qualified_id(args[1])
        if not (key and type_ and getter and setter):
            return None
        # The C++ macro binds bare member pointers (no `-> Type` for the stub
        # scanner to read), so re-synthesize the arrow-lambda form the
        # hand-written clusters used: the property's getter half drives the
        # field's ---@type, the getter method's lambda drives ---@return, and
        # the setter stays a bare member pointer (setters carry no annotation).
        return tokenize(', "%s", sol::property([](%s* self) -> %s { return '
                        'self->%s(); }, &%s::%s), '
                        '"%s", [](%s* self) -> %s { return self->%s(); }, '
                        '"%s", &%s::%s'
                        % (key, cls, type_, getter, cls, setter,
                           getter, cls, type_, getter,
                           setter, cls, setter))
    if name == 'LUA_MEMBER_PROP_FR':
        if not cls or len(args) != 3:
            return None
        def single(a):
            return a[0][1] if len(a) == 1 and a[0][0] == 'id' else None
        key, getter = (single(args[i]) for i in (0, 2))
        type_ = qualified_id(args[1])
        if not (key and type_ and getter):
            return None
        # Readonly twin of LUA_MEMBER_PROP_F: the field's ---@type comes from the
        # synthesized arrow; no method entries are emitted.
        return tokenize(', "%s", sol::readonly_property([](%s* self) -> %s '
                        '{ return self->%s(); })'
                        % (key, cls, type_, getter))
    if name == 'LUA_MEMBER_FUNC_OVERLOAD':
        if len(args) < 2 or len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        # MEMBER is an identifier stringized by the C++ macro (#); emit the
        # string-literal key so downstream passes see the hand-written shape.
        # sol::meta_function selectors are not identifiers -- those stay in
        # literal LUA_META(sol::meta_function::..., sol::overload(...)) form.
        out = tokenize(', "%s", sol::overload(' % args[0][0][1])
        for j, a in enumerate(args[1:]):
            if j:
                out.append(('punct', ','))
            out.extend(a)
        out.append(('punct', ')'))
        return out
    if name in ('LUA_CAST', 'LUA_CAST_C'):
        if not cls or len(args) < 2:
            return None
        if len(args[0]) != 1 or args[0][0][0] != 'id':
            return None
        member = args[0][0][1]
        def run_text(toks):
            return ' '.join(v for k, v in toks)
        ret = run_text(args[1])
        params = ', '.join(run_text(a) for a in args[2:])
        suffix = ' const' if name == 'LUA_CAST_C' else ''
        return tokenize('static_cast<%s (%s::*)(%s)%s>(&%s::%s)'
                        % (ret, cls, params, suffix, cls, member))
    return None


def expand_rbfx_macros(toks, cls=None):
    """Expand every LUA_* macro call in a token stream to its hand-written
    equivalent (see _expand_rbfx); unknown or malformed calls pass through.
    Tracks the enclosing `using LUA_THIS = X;` so class-bearing macros know
    which usertype they bind. cls carries the context into nested rescans
    (LUA_CAST inside LUA_MEMBER_FUNC_OVERLOAD)."""
    out = []
    i = 0
    n = len(toks)
    while i < n:
        k, v = toks[i]
        # using LUA_THIS = <qualified-id> ;  -- scoped class context
        if k == 'id' and v == 'using' and i + 3 < n \
                and toks[i + 1] == ('id', 'LUA_THIS') \
                and toks[i + 2] == ('punct', '='):
            end = i + 3
            while end < n and toks[end] != ('punct', ';'):
                end += 1
            if end < n:
                cls = qualified_id(toks[i + 3:end])
        if k == 'id' and v.startswith('LUA_') and i + 1 < n \
                and toks[i + 1] == ('punct', '('):
            close = skip_balanced(toks, i + 1, '(', ')')
            args = split_macro_args(toks[i + 2:close - 1])
            expanded = _expand_rbfx(v, args, cls)
            if expanded is not None:
                # Expansion output can contain nested macro calls (e.g.
                # LUA_CAST inside LUA_MEMBER_FUNC_OVERLOAD); rescan it so they
                # expand too, carrying the class context along.
                out.extend(expand_rbfx_macros(expanded, cls))
                i = close
                continue
        out.append(toks[i])
        i += 1
    return out


# ---------------------------------------------------------------------------
# File parser
# ---------------------------------------------------------------------------

def parse_file(path, api):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()
    toks = expand_rbfx_macros(tokenize(text))
    n = len(toks)

    # Local variable bound to a named table: var -> qualified table name.
    namespace_var = {}

    def resolve(ctx):
        """Return the table prefix for a context variable ('' == global).
        None means: not an API-bearing table (e.g. a local create_table())."""
        if ctx == 'lua' or ctx in _GLOBAL_STATE_VARS:
            return ''
        return namespace_var.get(ctx)

    def join(prefix, name):
        return (prefix + '.' + name) if prefix else name

    # Pass 1: sol::table VAR = <ctx>.create_named_table("Name") and its nested cousin
    # <ctx>.create_named("Name") (a table method). The latter nests under <ctx>'s qualified name,
    # so `Editor.project` (a create_named of the `Editor` named table) resolves one level down.
    i = 0
    while i < n:
        if toks[i][0] == 'id' and toks[i][1] == 'sol':
            j = match_seq(toks, i, [('id', 'sol'), ('punct', '::'), ('id', 'table')])
            if j > 0 and j + 1 < n and toks[j][0] == 'id' and toks[j + 1] == ('punct', '='):
                var = toks[j][1]
                ctx = toks[j + 2][1] if j + 2 < n and toks[j + 2][0] == 'id' else None
                m = match_seq(toks, j + 2, [('id', None), ('punct', '.'),
                                            ('id', 'create_named_table'), ('punct', '('),
                                            ('str', None), ('punct', ')')])
                if m > 0 and ctx is not None:
                    namespace_var[var] = join(resolve(ctx) or '', toks[j + 6][1])
                else:
                    m = match_seq(toks, j + 2, [('id', None), ('punct', '.'),
                                                ('id', 'create_named'), ('punct', '('),
                                                ('str', None), ('punct', ')')])
                    if m > 0 and ctx is not None:
                        prefix = resolve(ctx)
                        if prefix is not None:
                            namespace_var[var] = join(prefix, toks[j + 6][1])
        i += 1

    # Pass 2: API-defining constructs.
    i = 0
    while i < n:
        k, v = toks[i]

        # <ctx> ( . | -> ) new_usertype < T > ( "Name" , ... )   |   set_function ( "name" , ... )
        acc = member_access(toks, i)
        if acc:
            ctx, method, j = acc
            if method == 'new_usertype' and j < n and toks[j] == ('punct', '<'):
                j = skip_balanced(toks, j, '<', '>')
            if method in ('new_usertype', 'set_function') and j < n and toks[j] == ('punct', '('):
                close = skip_balanced(toks, j, '(', ')')
                prefix = resolve(ctx)
                if prefix is not None:
                    if method == 'new_usertype':
                        args = split_args(toks[j + 1:close - 1])
                        if args and args[0] and args[0][0][0] == 'str':
                            sym = api.symbol(join(prefix, args[0][0][1]), is_class=True)
                            parse_usertype_body(sym, args[1:])
                    else:  # set_function
                        inner = toks[j + 1:close]
                        if inner and inner[0][0] == 'str':
                            define_member(api, prefix, inner[0][1], 'method', extract_return_base(inner))
                i = close
                continue
            if method in ('new_usertype', 'set_function'):
                i = max(j, i + 1)
                continue

        # <ctx> [ "name" ] = <value...> ;
        if k == 'id' and i + 4 < n and toks[i + 1] == ('punct', '[') \
                and toks[i + 2][0] == 'str' and toks[i + 3] == ('punct', ']') \
                and toks[i + 4] == ('punct', '='):
            ctx = v
            name = toks[i + 2][1]
            j = i + 5
            depth = 0
            while j < n:
                kk, vv = toks[j]
                if kk == 'punct':
                    if vv in '([{':
                        depth += 1
                    elif vv in ')]}':
                        depth -= 1
                    elif vv == ';' and depth == 0:
                        break
                j += 1
            prefix = resolve(ctx)
            if prefix is not None:
                value = toks[i + 5:j]
                define_member(api, prefix, name, assign_value_kind(value), extract_return_base(value))
            i = j + 1
            continue

        i += 1


def define_member(api, prefix, name, kind, ret=None):
    """Route a member to globals (prefix '') or its owning table symbol."""
    if prefix == '':
        if is_identifier(name):
            api.globals.setdefault(
                name, {'kind': 'function' if kind == 'method' else 'const', 'ret': ret})
    else:
        owner = api.symbol(prefix, is_class=api.symbols.get(prefix, Symbol('', False)).is_class)
        owner.add(name, kind, ret)


# ---------------------------------------------------------------------------
# Emission (LuaLS stub style)
# ---------------------------------------------------------------------------

def emit(api, header):
    out = ['---@meta ' + header, '']
    exported = set(api.symbols.keys())

    # Transitive base closure. Use RAW base names (not only exported ones): a base such as
    # `Resource` has no usertype of its own yet still marks its descendants as resources.
    direct = {n: {b for b in api.symbols[n].bases if b != n} for n in exported}

    def _ancestors(n):
        seen, stack = set(), list(direct.get(n, ()))
        while stack:
            b = stack.pop()
            if b in seen:
                continue
            seen.add(b)
            stack.extend(direct.get(b, ()))
        return seen

    _pools = {}

    def pool_of(root):
        if root not in _pools:
            _pools[root] = sorted(
                n for n in exported
                if is_identifier(n) and (n == root or root in _ancestors(n)))
        return _pools[root]

    def emit_name_keyed(owner, name, spec, is_method):
        root, extra = spec
        tail = ''.join(', %s: %s' % (pn, pt) for pn, pt in extra)
        for c in pool_of(root):
            head = ('self: %s, ' % owner) if is_method else ''
            out.append('---@overload fun(%stypeName: "%s"%s): %s' % (head, c, tail, c))
        out.append('---@param typeName string')
        for pn, pt in extra:
            out.append('---@param %s %s' % (pn, pt))
        out.append('---@return %s' % (root if root in exported else 'any'))
        args = 'typeName' + ''.join(', %s' % pn for pn, _ in extra)
        if is_method:
            out.append('function %s:%s(%s) end' % (owner, name, args))
        else:
            out.append('function %s(%s) end' % (name, args))

    def bases_of(sym):
        real = [b for b in dict.fromkeys(sym.bases) if b in exported and b != sym.name]
        return (' : ' + ', '.join(real)) if real else ''

    def ann(kind, ret):
        return cpp_to_lua_type(ret, exported)

    # A qualified table name like `Editor.project` becomes the alias class `EditorProject`; a plain
    # name is its own alias. Nested tables are declared as alias classes and referenced from the
    # parent via a field, so `Editor.project.markDirty` resolves while `Editor` itself stays flat.
    def alias_name(qname):
        parts = qname.split('.')
        return parts[0] + ''.join(p[:1].upper() + p[1:] for p in parts[1:])

    children = {}
    for name in exported:
        if '.' in name:
            children.setdefault(name.rsplit('.', 1)[0], []).append(name)

    gfuncs = sorted(n for n, g in api.globals.items() if g['kind'] == 'function' and is_identifier(n))
    gconsts = sorted(n for n, g in api.globals.items() if g['kind'] == 'const' and is_identifier(n))

    for name in gfuncs:
        if name in FACTORY_SPECS:
            emit_name_keyed(None, name, FACTORY_SPECS[name], is_method=False)
            continue
        t = ann('function', api.globals[name]['ret'])
        if t:
            out.append('---@return %s' % t)
        out.append('function %s(...) end' % name)
    for name in gconsts:
        t = ann('const', api.globals[name]['ret'])
        if t:
            out.append('---@type %s' % t)
        out.append('%s = nil' % name)
    if gfuncs or gconsts:
        out.append('')

    def emit_class(qname):
        sym = api.symbols[qname]
        cls = alias_name(qname)
        out.append('---@class %s%s' % (cls, bases_of(sym)))
        for child in sorted(children.get(qname, [])):
            out.append('---@field %s %s' % (child.rsplit('.', 1)[1], alias_name(child)))
        out.append('%s = {}' % cls)
        for m in sorted(k for k, mm in sym.members.items() if mm['kind'] == 'method'):
            if m in FACTORY_SPECS:
                emit_name_keyed(cls, m, FACTORY_SPECS[m], is_method=True)
                continue
            t = ann('method', sym.members[m]['ret'])
            if t:
                out.append('---@return %s' % t)
            out.append('function %s.%s(...) end' % (cls, m))
        for f in sorted(k for k, mm in sym.members.items() if mm['kind'] == 'field'):
            t = ann('field', sym.members[f]['ret'])
            if t:
                out.append('---@type %s' % t)
            out.append('%s.%s = %s' % (cls, f, 'nil' if sym.is_class else '0'))
        out.append('')

    for name in sorted(exported):
        if is_identifier(name):
            emit_class(name)
    for name in sorted(exported):
        if '.' in name:
            emit_class(name)

    return '\n'.join(out).rstrip() + '\n'


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def collect_sources(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                if 'ThirdParty' in root:
                    continue
                for nm in names:
                    if nm.endswith('.cpp'):
                        files.append(os.path.join(root, nm))
        elif os.path.isfile(p):
            files.append(p)
    return sorted(set(files))


def main():
    ap = argparse.ArgumentParser(description='Generate LuaLS API definition files from rbfx sol3 bindings.')
    ap.add_argument('--src', action='append', required=True, help='binding .cpp file or directory (repeatable)')
    ap.add_argument('--out', required=True, help='output .d.lua path')
    ap.add_argument('--header',
                    default='rbfx API. Generated by Source/Tools/ApiDocGen. Do not edit by hand.',
                    help='text after ---@meta on the first line')
    args = ap.parse_args()

    files = collect_sources(args.src)
    if not files:
        print('no source files found under: %s' % args.src, file=sys.stderr)
        return 2

    api = Api()
    for fpath in files:
        parse_file(fpath, api)

    txt = emit(api, args.header)
    out_dir = os.path.dirname(os.path.abspath(args.out))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(txt)

    nclasses = sum(1 for s in api.symbols.values() if s.is_class)
    nmembers = sum(len(s.members) for s in api.symbols.values())
    print('wrote %s: %d tables (%d classes), %d members, %d globals, from %d files'
          % (args.out, len(api.symbols), nclasses, nmembers, len(api.globals), len(files)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
