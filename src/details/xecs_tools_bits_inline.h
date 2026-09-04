namespace xecs::tools
{
    //------------------------------------------------------------------------------------
    constexpr
    void bits::setBit(int Bit) noexcept
    {
        xassert(Bit >= 0 && Bit <= 63);
        int x = Bit / 64;
        int y = Bit % 64;
        m_Bits[x] |= (1ull << y);
    }

    //------------------------------------------------------------------------------------
    constexpr
    void bits::clearBit(int Bit) noexcept
    {
        xassert(Bit>=0 && Bit <= 63);
        int x = Bit / 64;
        int y = Bit % 64;
        m_Bits[x] &= ~(1ull << y);
    }

    //------------------------------------------------------------------------------------
    constexpr
    bool bits::getBit(int Bit) const noexcept
    {
        xassert(Bit >= 0 && Bit <= 63);
        int x = Bit / 64;
        int y = Bit % 64;
        return m_Bits[x] & (1ull << y);
    }

    //------------------------------------------------------------------------------------
    constexpr
    bool bits::Superset(const bits& B) const noexcept
    {
        for (int i = 0, size = static_cast<int>(m_Bits.size()); i < size; ++i)
        {
            if ((m_Bits[i] & B.m_Bits[i]) != B.m_Bits[i])
                return false;
        }

        return true;
    }

    //------------------------------------------------------------------------------------
    constexpr
    bool bits::Subset(const bits& B) const noexcept
    {
        return B.Superset(*this);
    }

    //------------------------------------------------------------------------------------
    constexpr
    bool bits::Equals(const bits& B) const noexcept
    {
        for (int i = 0, size = static_cast<int>(m_Bits.size()); i < size; ++i)
        {
            if (m_Bits[i] != B.m_Bits[i])
                return false;
        }

        return true;
    }

    //------------------------------------------------------------------------------------
    template< typename... T_COMPONENTS >
    requires( assert_valid_tuple_components_v < std::tuple<T_COMPONENTS...> > )
    constexpr
    void bits::AddFromComponents( void ) noexcept
    {
        ((setBit(xecs::component::type::info_v<T_COMPONENTS>.m_BitID)), ...);
    }

    //------------------------------------------------------------------------------------
    template< typename... T_COMPONENTS >
    requires( assert_valid_tuple_components_v < std::tuple<T_COMPONENTS...> > )
    constexpr
    void bits::AddFromComponents( std::tuple<T_COMPONENTS...>* ) noexcept
    {
        ((setBit(xecs::component::type::info_v<T_COMPONENTS>.m_BitID)), ...);
    }

    //------------------------------------------------------------------------------------

    template< typename... T_COMPONENTS >
    requires( assert_valid_tuple_components_v < std::tuple<T_COMPONENTS...> > )
    constexpr
    void bits::ClearFromComponents( void ) noexcept
    {
        ((ckearBit(xecs::component::type::info_v<T_COMPONENTS>.m_BitID)), ...);
    }

    //------------------------------------------------------------------------------------

    template< typename... T_COMPONENTS >
    requires( assert_valid_tuple_components_v < std::tuple<T_COMPONENTS...> > )
    constexpr
    void bits::ClearFromComponents( std::tuple<T_COMPONENTS...>* ) noexcept
    {
        ((ckearBit(xecs::component::type::info_v<T_COMPONENTS>.m_BitID)), ...);
    }

    //------------------------------------------------------------------------------------

    std::uint64_t bits::GenerateUniqueID( void ) const noexcept
    {
        std::hash<std::uint64_t> Hasher;
        const auto               Hashes = std::span{ reinterpret_cast<const std::uint64_t*>(this), sizeof(*this) / sizeof(std::uint64_t) };

        std::uint64_t Hash = Hasher(Hashes[0]);
        for (int i = 1; i < Hashes.size(); ++i)
        {
            Hash ^= Hasher(Hashes[i]) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
        }

        return Hash;
    }

    //------------------------------------------------------------------------------------
    inline
    bits& bits::setupAnd( const bits& A, const bits& B ) noexcept
    {
        for( auto i = 0, size = static_cast<const int>(m_Bits.size()); i < size; ++i)
        {
            m_Bits[i] = A.m_Bits[i] & B.m_Bits[i];
        }
        return *this;
    }

    //------------------------------------------------------------------------------------
    inline
    int bits::CountComponents( void ) const noexcept
    {
        int Count = 0;
        for (int i = static_cast<int>(m_Bits.size()) - 1; i >= 0; --i)
        {
            Count += std::popcount(m_Bits[i]);
        }
        return Count;

        /*
        // https://graphics.stanford.edu/~seander/bithacks.html
        int Count = 0;
        for ( int i = static_cast<int>(m_Bits.size()) - 1; i >= 0; --i )
        {
            auto v = m_Bits[i] - ((m_Bits[i] >> 1) & 0x5555555555555555ull);
            v = (v & 0x3333333333333333ull) + ((v >> 2) & 0x3333333333333333ull);
            Count += ((v + (v >> 4) & 0xF0F0F0F0F0F0F0F) * 0x101010101010101) >> 56;
        }
        return Count;
        */
    }

    //------------------------------------------------------------------------------------
    inline
    int bits::ToInfoArray( xecs::component::entity::info_array& InfoArray ) const noexcept
    {
        const auto Count = Foreach( [&]( int Index, const xecs::component::type::info& Info ) constexpr noexcept
        {
            InfoArray[Index] = &Info;
        });

        assert(Count == CountComponents());

        //
        // Sanity Check
        //
#if _DEBUG

        // Make sure that all the components are sorted
        for( int i = 1, end = Count; i < end; ++i )
        {
            assert( xecs::component::type::details::CompareTypeInfos(InfoArray[i-1], InfoArray[i]) );
        }
#endif

        return Count;
    }

    //------------------------------------------------------------------------------------
    // Find which index a component is assuming we are in an array of components defined by the 1s
    //------------------------------------------------------------------------------------
    inline
    int bits::getIndexOfComponent( int BitIndex ) const noexcept
    {
        assert(getBit(BitIndex));

        const int   y     = BitIndex % 64;
        int         x     = BitIndex / 64;
        int         Count = std::popcount( m_Bits[x] & ((1ull << y) - 1) );

        while(x) Count += std::popcount(m_Bits[--x]);
        return Count;
    }

    //------------------------------------------------------------------------------------
    template< typename T_CALLBACK >
    int bits::Foreach( T_CALLBACK&& Function ) const noexcept
    {
        int nComponents = 0;
        int GlobalBit   = 0;
        for( int i=0; i< m_Bits.size(); ++i )
        {
            std::uint64_t V = m_Bits[i];
            if (V)
            {
                int nBit = 0;
                do
                {
                    const int c = std::countr_zero(V);
                    nBit += c;
                    Function( nComponents++, *xecs::component::mgr::s_BitsToInfo[GlobalBit + nBit] );
                    V >>= (1 + c);
                    nBit++;
                } while (V);
            }

            GlobalBit += 64;
        }

        return nComponents;
    }
}