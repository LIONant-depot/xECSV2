#ifndef XECS_TOOLS_META_H
#define XECS_TOOLS_META_H
#pragma once

#include <cstdio>

//--------------------------------------------------------------------------------------------
// This header used to be provided by xCore (xcore::types / xcore::function). It is now
// vendored directly inside xECS since these are generic, self-contained template-metaprogramming
// utilities with no dependency on xCore itself (no GUID/serialization/error-handling coupling).
//--------------------------------------------------------------------------------------------

namespace xecs::types
{
    //-------------------------------------------------------------------------------------------------------
    // Check if type if const (sees through references and pointers)
    //-------------------------------------------------------------------------------------------------------
    template< typename T >
    constexpr auto is_const_v = std::is_same_v< const std::remove_pointer_t<std::remove_reference_t<T>>, std::remove_pointer_t<std::remove_reference_t<T>> >;

    //--------------------------------------------------------------------------------------------
    // Removes all attributes and references operators giving the basic underlaying type
    //--------------------------------------------------------------------------------------------
    template< typename T >
    using decay_full_t = std::remove_const_t<std::remove_pointer_t<std::decay_t<T>>>;

    //------------------------------------------------------------------------------
    // Description:
    //      Static casting with safe ranges
    //------------------------------------------------------------------------------
    template< typename T_TO, typename T_FROM > constexpr
    T_TO static_cast_safe( const T_FROM a ) noexcept
    {
        assert(static_cast<std::int64_t>(a)  >= static_cast<std::int64_t>(std::numeric_limits<T_TO>::lowest()) &&
               static_cast<std::uint64_t>(a) <= static_cast<std::uint64_t>(std::numeric_limits<T_TO>::max()));

        return static_cast<T_TO>(a);
    }

    //------------------------------------------------------------------------------------------
    // Tuple err to index conversion
    //------------------------------------------------------------------------------------------
    namespace details
    {
        template <class T, class T_ARGS>
        struct tuple_t2i;

        template <class T, class... T_ARGS>
        struct tuple_t2i<T, std::tuple<T, T_ARGS...>>
        {
            static const std::size_t value = 0;
        };

        template <class T, class U, class... T_ARGS>
        struct tuple_t2i<T, std::tuple<U, T_ARGS...>>
        {
            static const std::size_t value = 1 + tuple_t2i<T, std::tuple<T_ARGS...>>::value;
        };
    }
    template< typename T_TYPE, typename T_TUPLE >
    constexpr static auto tuple_t2i_v = details::tuple_t2i< T_TYPE, T_TUPLE >::value;

    //------------------------------------------------------------------------------------------
    // Creates a nullptr value given a tuple
    //------------------------------------------------------------------------------------------
    template< typename T_TUPLE>
    constexpr auto null_tuple_v = static_cast<T_TUPLE*>(0);

    template< typename...T_ARGS>
    constexpr auto make_null_tuple_v = static_cast<std::tuple<T_ARGS...>*>(0);

    //------------------------------------------------------------------------------------------
    // Concatenate a list of tuples into a simple one
    //------------------------------------------------------------------------------------------
    template<typename ... T_TUPLES>
    using tuple_cat_t = decltype(std::tuple_cat(std::declval<T_TUPLES>()...));

    //--------------------------------------------------------------------------------------------
    // Tuple Decay Full, decays full each member of the tuple
    //--------------------------------------------------------------------------------------------
    namespace details
    {
        template<typename T >
        struct tuple_decay_full_helper;

        template<typename... T >
        struct tuple_decay_full_helper<std::tuple<T...>>
        {
            using type = std::tuple< xecs::types::decay_full_t<T> ... >;
        };
    }
    template< typename T_TUPLE >
    using tuple_decay_full_t = typename details::tuple_decay_full_helper<T_TUPLE>::type;

    //--------------------------------------------------------------------------------------------
    // From: https://codereview.stackexchange.com/questions/131194/selection-sorting-a-type-list-compile-time
    // Sorts a tuple base on a compare function such:
    //
    // template< typename T_A, typename T_B >
    // struct compare { constexpr static bool value = T_A::type_guid_v.m_Value > T_B::type_guid_v.m_Value; };
    //
    //--------------------------------------------------------------------------------------------
    namespace details
    {
        // swap types at index i and index j in the template argument tuple
        template <std::size_t i, std::size_t j, class Tuple>
        class tuple_element_swap
        {
            template <class IndexSequence>
            struct tuple_element_swap_impl;

            template <std::size_t... indices>
            struct tuple_element_swap_impl<std::index_sequence<indices...>>
            {
                using type = std::tuple
                <
                    std::tuple_element_t
                    <
                        indices != i && indices != j ? indices : indices == i ? j : i, Tuple
                    >...
                >;
            };

        public:
            using type = typename tuple_element_swap_impl
            <
                std::make_index_sequence<std::tuple_size<Tuple>::value>
            >::type;
        };

        // selection sort template argument tuple's variadic template's types
        template <template <class, class> class Comparator, class Tuple>
        class tuple_selection_sort
        {
            // selection sort's "loop"
            template <std::size_t i, std::size_t j, std::size_t tuple_size, class LoopTuple>
            struct tuple_selection_sort_impl
            {
                // this is done until we have compared every element in the type list
                using tuple_type = std::conditional_t
                <
                    Comparator
                    <
                          std::tuple_element_t<j, LoopTuple>
                        , std::tuple_element_t<i, LoopTuple>
                    >::value,
                    typename tuple_element_swap<i, j, LoopTuple>::type, // true: swap(i, j)
                    LoopTuple                                           // false: do nothing
                >;

                using type = typename tuple_selection_sort_impl // recurse until j == tuple_size
                <
                    i, j + 1, tuple_size, tuple_type // using the modified tuple
                >::type;
            };

            template <std::size_t i, std::size_t tuple_size, class LoopTuple>
            struct tuple_selection_sort_impl<i, tuple_size, tuple_size, LoopTuple>
            {
                // once j == tuple_size, we increment i and start j at i + 1 and recurse
                using type = typename tuple_selection_sort_impl
                <
                    i + 1, i + 2, tuple_size, LoopTuple
                >::type;
            };

            template <std::size_t j, std::size_t tuple_size, class LoopTuple>
            struct tuple_selection_sort_impl<tuple_size, j, tuple_size, LoopTuple>
            {
                // once i == tuple_size, we know that every element has been compared
                using type = LoopTuple;
            };

        public:
            using type = typename tuple_selection_sort_impl
            <
                0, 1, std::tuple_size<Tuple>::value, Tuple
            >::type;
        };
    }

    template< template <class, class> class T_COMPARE, class T_TUPLE >
    using tuple_sort_t = typename details::tuple_selection_sort< T_COMPARE, T_TUPLE >::type;

    //--------------------------------------------------------------------------------------------
    // Checks if a type is part of a pack
    //--------------------------------------------------------------------------------------------
    namespace details
    {
        template <typename...>
        struct count_of;

        template< typename F >
        struct count_of<F>
        {
            static constexpr auto value = 0ull;
        };

        template <typename F, typename S, typename... T>
        struct count_of<F, S, T...>
        {
            static constexpr auto value = std::is_same<F, S>::value + count_of<F, T...>::value;
        };
    }

    template< typename T_TYPE, typename...T_ARGS >
    static constexpr auto count_of_v  = details::count_of< T_TYPE, T_ARGS...>::value;

    //--------------------------------------------------------------------------------------------
    // Does tuple have duplicates
    //--------------------------------------------------------------------------------------------
    namespace details
    {
        template< typename T_TUPLE >
        struct tuple_has_duplicates;

        template< typename...T_ARGS >
        struct tuple_has_duplicates< std::tuple<T_ARGS...> >
        {
            constexpr static bool value = ((xecs::types::count_of_v<T_ARGS, T_ARGS...> > 1 ) || ... ) ;
        };
    }

    template< typename T_TUPLE >
    static constexpr bool tuple_has_duplicates_v = details::tuple_has_duplicates<T_TUPLE >::value;

    //------------------------------------------------------------------------------------------
    // Variant type to index conversion
    //------------------------------------------------------------------------------------------
    namespace details
    {
        template< class T, class T_VARIANT >
        struct variant_t2i;

        template< class T, class... T_ARGS >
        struct variant_t2i<T, std::variant<T, T_ARGS...>>
        {
            static const std::size_t value = 0;
        };

        template< class T, class U, class... T_ARGS >
        struct variant_t2i<T, std::variant<U, T_ARGS...>>
        {
            static const std::size_t value = 1 + variant_t2i<T, std::variant<T_ARGS...>>::value;
        };
    }
    template< typename T_TYPE, typename T_VARIANT >
    constexpr static auto variant_t2i_v = details::variant_t2i< T_TYPE, T_VARIANT >::value;

    //--------------------------------------------------------------------------------------------
    // Helpful when using variadic types
    //--------------------------------------------------------------------------------------------
    namespace details{ template< typename T > struct always_false : std::false_type {}; }
    template< typename T > constexpr static bool always_false_v =  details::always_false<T>::value;

    //--------------------------------------------------------------------------------------------
    // Determines if a type is derived from a particular template class
    //--------------------------------------------------------------------------------------------
    namespace details
    {
        template< template< typename... > typename T_BASE, typename    T_DERIVED > struct   is_specialized                            : std::false_type {};
        template< template< typename... > typename T_BASE, typename... T_ARGS    > struct   is_specialized<T_BASE, T_BASE<T_ARGS...>> : std::true_type  {};
    }
    template< template< typename... > typename T_BASE, typename    T_DERIVED > constexpr static bool is_specialized_v = details::is_specialized<T_BASE, T_DERIVED>::value;
}

namespace xecs::function
{
    //-------------------------------------------------------------------------------------------------------
    // Determines if a type is callable
    //-------------------------------------------------------------------------------------------------------
    namespace details
    {
        template<typename T>
        struct is_callable_impl
        {
            template<auto U>     struct Check;
            template<typename>   static std::false_type Test(...);
            template<typename C> static std::true_type  Test(Check<&C::operator()>*);
            constexpr static auto value = std::is_same_v< decltype(Test<T>(nullptr)), std::true_type>;
        };

        template<typename T>
        constexpr auto is_callable_v = std::conditional_t< std::is_class<T>::value, is_callable_impl<T>, std::false_type >::value;
    }
    template<typename T>
    constexpr auto is_callable_v = details::is_callable_v<std::decay_t<T>>;

    //------------------------------------------------------------------------------
    // Helper to build a function type
    //------------------------------------------------------------------------------
    template< bool T_NOEXCEPT, typename T_RET, typename... T_ARGS > struct make { using type = T_RET(T_ARGS...) noexcept(T_NOEXCEPT); };
    template< bool T_NOEXCEPT, typename T_RET >                     struct make< T_NOEXCEPT, T_RET, void > { using type = T_RET()            noexcept(T_NOEXCEPT); };
    template< bool T_NOEXCEPT, typename T_RET, typename... T_ARGS > using  make_t = typename make<T_NOEXCEPT, T_RET, T_ARGS...>::type;

    //------------------------------------------------------------------------------
    // Helper to extract a the function arguments
    //------------------------------------------------------------------------------
    template< std::size_t T_I, std::size_t T_MAX, typename... T_ARGS >  struct traits_args { using arg_t = typename std::tuple_element_t< T_I, std::tuple<T_ARGS...> >; };
    template<>                                                          struct traits_args<0, 0> { using arg_t = void; };
    template< std::size_t T_I, std::size_t T_MAX, typename... T_ARGS >  using  traits_args_t = typename traits_args<T_I, T_MAX, T_ARGS...>::arg_t;

    //------------------------------------------------------------------------------
    // Function traits
    //------------------------------------------------------------------------------
    template< class F > struct traits;

    template< bool T_NOEXCEPT_V, typename T_RETURN_TYPE, typename... T_ARGS >
    struct traits_decomposition
    {
        using                        return_type    = T_RETURN_TYPE;
        constexpr static std::size_t arg_count_v    = sizeof...(T_ARGS);
        using                        class_type     = void;
        using                        self           = xecs::function::traits< T_RETURN_TYPE(T_ARGS...) noexcept(T_NOEXCEPT_V) >;
        using                        func_type      = xecs::function::make_t< T_NOEXCEPT_V, T_RETURN_TYPE, T_ARGS... >;
        using                        args_tuple     = std::tuple<T_ARGS...>;

        template< std::size_t T_I >
        using arg_t = traits_args_t<T_I, arg_count_v, T_ARGS... >;

        template< std::size_t T_I >
        using safe_arg_t = typename std::conditional < T_I < arg_count_v, arg_t<T_I>, void >::type;
    };

    // functions
    template< typename T_RETURN_TYPE, typename... T_ARGS >          struct traits< T_RETURN_TYPE(T_ARGS...) noexcept > : xecs::function::traits_decomposition< true, T_RETURN_TYPE, T_ARGS... > {};
    template< typename T_RETURN_TYPE, typename... T_ARGS >          struct traits< T_RETURN_TYPE(T_ARGS...)          > : xecs::function::traits_decomposition< false, T_RETURN_TYPE, T_ARGS... > {};

    // function pointer
    template< typename T_RETURN, typename... T_ARGS >               struct traits< T_RETURN(*)(T_ARGS...) > : traits< T_RETURN(T_ARGS...) > {};
    template< typename T_RETURN, typename... T_ARGS >               struct traits< T_RETURN(*)(T_ARGS...) noexcept > : traits< T_RETURN(T_ARGS...) noexcept > {};

    // member function pointer
    template< class T_CLASS, typename T_RETURN, typename... T_ARGS> struct traits< T_RETURN(T_CLASS::*)(T_ARGS...) noexcept > : traits< T_RETURN(T_ARGS...) noexcept > { using class_type = T_CLASS; };
    template< class T_CLASS, typename T_RETURN, typename... T_ARGS> struct traits< T_RETURN(T_CLASS::*)(T_ARGS...) > : traits< T_RETURN(T_ARGS...) > { using class_type = T_CLASS; };

    // const member function pointer
    template< class T_CLASS, typename T_RETURN, typename... T_ARGS >struct traits< T_RETURN(T_CLASS::*)(T_ARGS...) const noexcept > : traits< T_RETURN(T_ARGS...) noexcept > { using class_type = T_CLASS; };
    template< class T_CLASS, typename T_RETURN, typename... T_ARGS >struct traits< T_RETURN(T_CLASS::*)(T_ARGS...) const > : traits< T_RETURN(T_ARGS...) > { using class_type = T_CLASS; };

    // functors
    template< class T_CLASS >                                       struct traits                   : traits<decltype(&T_CLASS::operator())> { using class_type = T_CLASS; };
    template< class T_CLASS >                                       struct traits<T_CLASS&>         : traits<T_CLASS> {};
    template< class T_CLASS >                                       struct traits<const T_CLASS&>   : traits<T_CLASS> {};
    template< class T_CLASS >                                       struct traits<T_CLASS&&>        : traits<T_CLASS> {};
    template< class T_CLASS >                                       struct traits<const T_CLASS&&>  : traits<T_CLASS> {};
    template< class T_CLASS >                                       struct traits<T_CLASS*>         : traits<T_CLASS> {};
    template< class T_CLASS >                                       struct traits<const T_CLASS*>   : traits<T_CLASS> {};

    //---------------------------------------------------------------------------------------
    // Compare two functions types
    //---------------------------------------------------------------------------------------
    namespace details
    {
        template< typename T_A, typename T_B, int T_ARG_I >
        struct traits_compare_args
        {
            static_assert(std::is_same
            <
                typename T_A::template arg_t< T_ARG_I >,
                typename T_B::template arg_t< T_ARG_I >
            >::value, "Argument Don't match");
            constexpr static bool value = traits_compare_args<T_A, T_B, T_ARG_I - 1 >::value;
        };

        template< typename T_A, typename T_B >
        struct traits_compare_args< T_A, T_B, -1 >
        {
            constexpr static bool value = true;
        };
    }
}

//--------------------------------------------------------------------------------------------
// Minimal stand-in for xCore's xcore::log::channel + XLOG_CHANNEL_WARNING. xECS only ever used
// this for a couple of plain warning messages (no structured logging elsewhere in this project
// to wire into instead), so this just tags stderr output with the channel name rather than
// pulling in or building a real logging subsystem.
//--------------------------------------------------------------------------------------------
namespace xecs::log
{
    struct channel
    {
        const char* m_pName;
    };
}

// ##__VA_ARGS__ (not __VA_OPT__) deliberately: this project doesn't build with the conformant
// preprocessor (/Zc:preprocessor) project-wide, and this MSVC/GCC extension - unlike __VA_OPT__ -
// works fine under the legacy preprocessor too, so this doesn't need that flag turned on globally.
#define XLOG_CHANNEL_WARNING(CHANNEL, FORMAT, ...) \
    std::fprintf(stderr, "[%s] WARNING: " FORMAT "\n", (CHANNEL).m_pName, ##__VA_ARGS__)

#endif
