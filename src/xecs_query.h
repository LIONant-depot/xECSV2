namespace xecs::query
{
    template< typename... T_COMPONENTS >
    struct must final {};

    template< typename... T_COMPONENTS >
    struct one_of final {};

    template< typename... T_COMPONENTS >
    struct none_of final {};

    struct instance final
    {
        tools::bits     m_Must;
        tools::bits     m_OneOf;
        tools::bits     m_NoneOf;

        inline 
        bool                    Compare                 ( const tools::bits& ArchetypeBits
                                                        ) const noexcept;
        inline 
        bool                    Compare                 ( const tools::bits& ArchetypeBits
                                                        , const tools::bits& ExclutiveTagBits
                                                        ) const noexcept;
        template
        < typename T_FUNCTION
        > 
        void                    AddQueryFromFunction    ( void
                                                        ) noexcept;
        template
        < typename T_FUNCTION
        > requires
        ( xecs::function::is_callable_v<T_FUNCTION>
        )
        void                    AddQueryFromFunction    ( T_FUNCTION&& 
                                                        ) noexcept;
        template
        < typename... T_QUERIES
        > requires
        (((xecs::types::is_specialized_v< must, T_QUERIES>
            || xecs::types::is_specialized_v< one_of, T_QUERIES>
            || xecs::types::is_specialized_v< none_of, T_QUERIES>
            ) && ...)
        )
        void                    AddQueryFromTuple       ( std::tuple<T_QUERIES...>*
                                                        ) noexcept;
        template
        < typename T_TUPLE_QUERY
        > requires ( xecs::types::is_specialized_v< std::tuple, T_TUPLE_QUERY> )
        void                    AddQueryFromTuple       ( void
                                                        ) noexcept;
        inline
        std::uint64_t           GenerateUniqueID        ( void
                                                        ) const noexcept;
    };
}
