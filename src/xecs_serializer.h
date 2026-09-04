namespace xecs::serializer
{
    struct stream : xtextfile::stream
    {
        template< std::size_t N, typename... T_ARGS >
        xforceinline  xerr    Field(const char(&pFieldName)[N], T_ARGS&&... Args)                             noexcept;

        template< std::size_t N>
        xforceinline  xerr    Field( const char(&pFieldName)[N], xecs::component::entity& Entity ) noexcept;

        virtual void Remap( xecs::component::entity& Entity ) noexcept {}
    };
}
