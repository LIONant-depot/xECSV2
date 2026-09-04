namespace xecs::query
{
    //---------------------------------------------------------------------------

    bool instance::Compare(const tools::bits& ArchetypeBits) const noexcept
    {
        std::uint64_t c = 0;
        std::uint64_t a = 0;

        for (int i = 0, end = static_cast<int>(ArchetypeBits.m_Bits.size()); i != end; ++i)
        {
            if (m_NoneOf.m_Bits[i] & ArchetypeBits.m_Bits[i])
                return false;

            if ((m_Must.m_Bits[i] & ArchetypeBits.m_Bits[i]) != m_Must.m_Bits[i])
                return false;

            c |= (m_OneOf.m_Bits[i] & ArchetypeBits.m_Bits[i]);
            a |= m_OneOf.m_Bits[i];
        }

        // a | c
        // ------
        // 0   0 | 1 true 
        // 0   1 | x error
        // 1   0 | 0 false
        // 1   1 | 1 true 
        assert(!(!a && c));
        return !a || c;
    }

    //---------------------------------------------------------------------------

    bool instance::Compare( const tools::bits& ArchetypeBits
                          , const tools::bits& ExclutiveTagBits ) const noexcept
    {
        std::uint64_t c = 0;
        std::uint64_t a = 0;

        for (int i = 0, end = static_cast<int>(ArchetypeBits.m_Bits.size()); i != end; ++i)
        {
            if (m_NoneOf.m_Bits[i] & ArchetypeBits.m_Bits[i])
                return false;

            if ((m_Must.m_Bits[i] & ArchetypeBits.m_Bits[i]) != m_Must.m_Bits[i])
                return false;

            if ( ((m_OneOf.m_Bits[i] | m_Must.m_Bits[i]) & ExclutiveTagBits.m_Bits[i]) != ExclutiveTagBits.m_Bits[i] )
                return false;

            c |= (m_OneOf.m_Bits[i] & ArchetypeBits.m_Bits[i]);
            a |= m_OneOf.m_Bits[i];
        }

        // a | c
        // ------
        // 0   0 | 1 true 
        // 0   1 | x error
        // 1   0 | 0 false
        // 1   1 | 1 true 
        assert(!(!a && c));
        return !a || c;
    }

    //---------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > 
    void instance::AddQueryFromFunction( void ) noexcept
    {
        using func_traits = xecs::function::traits<T_FUNCTION>;
        static_assert( func_traits::arg_count_v > 0 );

        [&] < typename...T_COMPONENTS >(std::tuple<T_COMPONENTS...>*) constexpr noexcept
        {
            ( [&]<typename T_C>(std::tuple<T_C>*)
            {
                if constexpr (std::is_pointer_v<T_C>)
                {
                    m_OneOf.AddFromComponents<T_C>();
                }
                else if constexpr (std::is_reference_v<T_C>)
                {
                    m_Must.AddFromComponents<T_C>();
                }
                else
                {
                    static_assert(xecs::types::always_false_v<T_C>);
                }
            }(xecs::types::make_null_tuple_v<T_COMPONENTS>)
            , ...
            );

        }( xecs::types::null_tuple_v<typename func_traits::args_tuple> );
    }

    //---------------------------------------------------------------------------

    template
    < typename T_FUNCTION
    > requires
    ( xecs::function::is_callable_v<T_FUNCTION>
    )
    void instance::AddQueryFromFunction(T_FUNCTION&&) noexcept
    {
        AddQueryFromFunction<T_FUNCTION>();
    }

    //---------------------------------------------------------------------------

    template
    < typename... T_QUERIES
    > requires
    (((xecs::types::is_specialized_v< must, T_QUERIES>
        || xecs::types::is_specialized_v< one_of, T_QUERIES>
        || xecs::types::is_specialized_v< none_of, T_QUERIES>
        ) && ...)
    )
    void instance::AddQueryFromTuple(std::tuple<T_QUERIES...>* ) noexcept
    {
        ( [&]<template<typename...> class T_QTYPE, typename... T_COMPONENTS>(T_QTYPE<T_COMPONENTS...>*) constexpr noexcept
        {
            using t = T_QTYPE<T_COMPONENTS...>;
            if constexpr (std::is_same_v< t, xecs::query::must<T_COMPONENTS...>>)
            {
                m_Must.AddFromComponents<T_COMPONENTS...>();
            }
            else if constexpr (std::is_same_v< t, xecs::query::one_of<T_COMPONENTS...>>)
            {
                m_OneOf.AddFromComponents<T_COMPONENTS...>();
            }
            else if constexpr (std::is_same_v< t, xecs::query::none_of<T_COMPONENTS...>>)
            {
                m_NoneOf.AddFromComponents<T_COMPONENTS...>();
            }
            else
            {
                static_assert(xecs::types::always_false_v<t>);
            }
        }( xecs::types::null_tuple_v<T_QUERIES> )
        , ...
        );
    }

    //---------------------------------------------------------------------------
    template
    < typename T_TUPLE_QUERY
    > requires ( xecs::types::is_specialized_v< std::tuple, T_TUPLE_QUERY> )
    void instance::AddQueryFromTuple( void ) noexcept
    {
        AddQueryFromTuple( xecs::types::null_tuple_v<T_TUPLE_QUERY> );
    }

    //---------------------------------------------------------------------------
    inline
    std::uint64_t instance::GenerateUniqueID( void ) const noexcept
    {
        std::hash<std::uint64_t> Hasher;
        auto                     Hashes = std::span{ reinterpret_cast<const std::uint64_t*>(this), sizeof(*this) / sizeof(std::uint64_t) };

        std::uint64_t Hash = Hasher(Hashes[0]);
        for (int i = 1; i < Hashes.size(); ++i)
        {
            Hash ^= Hasher(Hashes[i]) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
        }

        return Hash;
    }

}
    
