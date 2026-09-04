namespace xecs::tools
{
    //------------------------------------------------------------------------------

    struct bits final
    {
        constexpr __inline
        void                    setBit                  ( int Bit 
                                                        ) noexcept;
        inline
        bits&                   setupAnd                ( const bits& A
                                                        , const bits& B 
                                                        ) noexcept;
        constexpr __inline
        bool                    getBit                  ( int Bit
                                                        ) const noexcept;
        constexpr
        bool                    Superset                ( const bits& B 
                                                        ) const noexcept;
        constexpr
        bool                    Subset                  ( const bits& B 
                                                        ) const noexcept;
        constexpr
        bool                    Equals                  ( const bits& B 
                                                        ) const noexcept;
        constexpr
        void                    clearBit                ( int Bit
                                                        ) noexcept;
        template
        < typename... T_COMPONENTS
        > requires
        ( assert_valid_tuple_components_v< std::tuple<T_COMPONENTS...> >
        ) constexpr __inline
        void                    AddFromComponents       ( void 
                                                        ) noexcept;
        template
        < typename... T_COMPONENTS
        > requires
        ( assert_valid_tuple_components_v<std::tuple<T_COMPONENTS...>>
        ) constexpr __inline
        void                    AddFromComponents       ( std::tuple<T_COMPONENTS...>*
                                                        ) noexcept;
        template
        < typename... T_COMPONENTS
        > requires
        ( assert_valid_tuple_components_v< std::tuple<T_COMPONENTS...> >
        ) constexpr __inline
        void                    ClearFromComponents     ( void 
                                                        ) noexcept;
        template
        < typename... T_COMPONENTS
        > requires
        ( assert_valid_tuple_components_v< std::tuple<T_COMPONENTS...> >
        ) constexpr __inline
        void                    ClearFromComponents     ( std::tuple<T_COMPONENTS...>*
                                                        ) noexcept;
        inline
        std::uint64_t           GenerateUniqueID        ( void
                                                        ) const noexcept;
        inline
        int                     CountComponents         ( void 
                                                        ) const noexcept;
        inline
        int                     ToInfoArray             ( xecs::component::entity::info_array& InfoArray
                                                        ) const noexcept;
        inline
        int                     getIndexOfComponent     ( int BitIndex
                                                        ) const noexcept;
        template< typename T_CALLBACK > // Call back Should take ( int Index, const xecs::component::type::info& )
        int                     Foreach                 ( T_CALLBACK&& Function
                                                        ) const noexcept;

        std::array<std::uint64_t, ((xecs::settings::max_component_types_v-1)/64)+1> m_Bits{};
    };

    //-------------------------------------------------------------------------------------------------
    constexpr
    bool HaveAllComponents(const bits& Bits, std::span<const xecs::component::type::info* const > Span ) noexcept
    {
        for( auto& e : Span )
        {
            if( Bits.getBit( e->m_BitID ) == false ) return false;
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    template< typename... T_COMPONENTS >
    constexpr
    bool HaveAllComponents( const bits& Bits, std::tuple<T_COMPONENTS...>* ) noexcept
    {
        return ((Bits.getBit(xecs::component::type::info_v<T_COMPONENTS>.m_BitID ) && ... ));
    }
}