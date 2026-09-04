namespace xecs::serializer
{
    template< std::size_t N, typename... T_ARGS >
    xerr stream::Field( const char(&pFieldName)[N], T_ARGS&&... Args ) noexcept
    {
        static_assert( ((std::is_same_v< xecs::types::decay_full_t<T_ARGS>, xecs::component::entity > == false) && ... ) );
        return xtextfile::stream::Field(pFieldName, std::forward<T_ARGS&&>(Args)... );
    }

    //-----------------------------------------------------------------------------------

    template< std::size_t N>
    xerr stream::Field( const char(&pFieldName)[N], xecs::component::entity& Entity ) noexcept
    {
        if (auto Err = xtextfile::stream::Field(pFieldName, Entity.m_Value); Err) return Err;
        if( isReading() ) Remap(Entity);
        return {};
    }
}