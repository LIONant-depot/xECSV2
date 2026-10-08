#ifndef XECS_MSVC_FUNCSIG_H
#define XECS_MSVC_FUNCSIG_H
#pragma once

// Non-MSVC compilers only (never included by MSVC, whose code path uses the real __FUNCSIG__).
// Type guids that fall back to hashing __FUNCSIG__ end up saved in scene files (ComponentDeps.txt,
// entity files), so another compiler must hash the SAME text MSVC would produce. This rebuilds
// MSVC's __FUNCSIG__ text for a "<prefix><T><suffix>" signature from clang/gcc's __PRETTY_FUNCTION__:
//   - class/union/enum names get MSVC's "struct "/"union "/"enum " key (class vs struct can not be
//     told apart portably: "struct" is assumed - every component in the engine is a struct)
//   - "(anonymous namespace)" -> "`anonymous-namespace'", "unsigned long" -> "unsigned __int64", ...
//   - template argument lists lose the space after each comma, like MSVC prints them
#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace xecs::details::msvc_funcsig
{
    template< std::size_t N >
    struct literal
    {
        char m[N]{};
        consteval literal(const char(&s)[N]) noexcept { for (std::size_t i = 0; i < N; ++i) m[i] = s[i]; }
        consteval std::string_view view() const noexcept { return { m, N - 1 }; }
    };

    template< std::size_t N >
    struct chars { char m[N]{}; };

    template< typename T >
    consteval std::string_view RawName() noexcept
    {
        constexpr std::string_view s = __PRETTY_FUNCTION__;   // "... RawName() [T = name]"
        constexpr auto b = s.find("[T = ") + 5;
        constexpr auto e = s.rfind(']');
        return s.substr(b, e - b);
    }

    struct writer
    {
        char*       m_p;
        std::size_t m_n = 0;
        consteval void put(char c) noexcept { if (m_p) m_p[m_n] = c; ++m_n; }
        consteval void put(std::string_view s) noexcept { for (char c : s) put(c); }
    };

    consteval bool IsIdentChar(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':'; }

    consteval std::string_view Trim(std::string_view s) noexcept
    {
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (!s.empty() && s.back()  == ' ') s.remove_suffix(1);
        return s;
    }

    // One template argument (or the top-level name when no key is known): fundamental types are mapped
    // to MSVC's spelling, numbers are kept, anything else is taken as a struct.
    consteval void WriteArg(writer& W, std::string_view a, std::string_view Key) noexcept;

    consteval void WriteName(writer& W, std::string_view a) noexcept
    {
        // the name with its template arguments, each converted
        constexpr std::string_view anon_clang = "(anonymous namespace)";
        std::size_t i = 0;
        while (i < a.size())
        {
            if (a.substr(i).starts_with(anon_clang)) { W.put("`anonymous-namespace'"); i += anon_clang.size(); continue; }
            if (a[i] == '<')
            {
                W.put('<'); ++i;
                int depth = 0; std::size_t start = i;
                for (; i < a.size(); ++i)
                {
                    const char c = a[i];
                    if (c == '<' || c == '(') ++depth;
                    else if ((c == '>' || c == ')') && depth > 0) --depth;
                    else if ((c == ',' || c == '>') && depth == 0)
                    {
                        WriteArg(W, Trim(a.substr(start, i - start)), {});
                        if (c == ',') { W.put(','); start = i + 1; }
                        else { W.put('>'); ++i; break; }
                    }
                }
                continue;
            }
            W.put(a[i]); ++i;
        }
    }

    consteval void WriteArg(writer& W, std::string_view a, std::string_view Key) noexcept
    {
        struct map { std::string_view from, to; };
        constexpr map Fundamentals[] =
        { {"unsigned long long", "unsigned __int64"}, {"unsigned long", "unsigned __int64"}, {"long long", "__int64"}, {"long", "__int64"}
        , {"unsigned int", "unsigned int"}, {"unsigned short", "unsigned short"}, {"unsigned char", "unsigned char"}, {"signed char", "signed char"}
        , {"int", "int"}, {"short", "short"}, {"char", "char"}, {"bool", "bool"}, {"float", "float"}, {"double", "double"}, {"void", "void"}
        , {"wchar_t", "wchar_t"}, {"char8_t", "char8_t"}, {"char16_t", "char16_t"}, {"char32_t", "char32_t"}
        };
        for (auto& F : Fundamentals) if (a == F.from) { W.put(F.to); return; }
        if (!a.empty() && ((a[0] >= '0' && a[0] <= '9') || a[0] == '-' || a == "true" || a == "false")) { W.put(a); return; }
        W.put(Key.empty() ? std::string_view{ "struct " } : Key);
        WriteName(W, a);
    }

    template< typename T >
    consteval std::string_view KeyOf() noexcept
    {
        if constexpr (std::is_enum_v<T>)        return "enum ";
        else if constexpr (std::is_union_v<T>)  return "union ";
        else if constexpr (std::is_class_v<T>)  return "struct ";
        else                                    return "";
    }

    template< typename T, literal PREFIX, literal SUFFIX >
    consteval std::size_t Compose(char* p) noexcept
    {
        writer W{ p };
        W.put(PREFIX.view());
        if constexpr (std::is_class_v<T> || std::is_enum_v<T> || std::is_union_v<T>) WriteArg(W, RawName<T>(), KeyOf<T>());
        else                                                                         WriteArg(W, RawName<T>(), {});
        W.put(SUFFIX.view());
        return W.m_n;
    }

    // value.m is a "const char[N]" holding exactly the text MSVC's __FUNCSIG__ would hold.
    template< typename T, literal PREFIX, literal SUFFIX >
    struct funcsig
    {
        static constexpr std::size_t length_v = Compose<T, PREFIX, SUFFIX>(nullptr);
        static constexpr chars<length_v + 1> value = []() consteval { chars<length_v + 1> C{}; Compose<T, PREFIX, SUFFIX>(C.m); return C; }();
    };
}

#endif