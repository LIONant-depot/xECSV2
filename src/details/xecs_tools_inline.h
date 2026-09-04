namespace xecs::tools
{
    //------------------------------------------------------------------------------

    template
    < typename T_QUERY
    > concept
    is_share_as_data_v
    =[]<typename...T>( std::tuple<T...>* ) constexpr noexcept
    {
        return (([]<template< typename...> class K, typename...J>(K<J...>*) constexpr noexcept
        {
            static_assert( std::is_same_v< K<J...>, xecs::query::none_of<J...> > ? !((std::is_same_v<J, xecs::component::share_as_data_exclusive_tag>) || ...) : true );
            return ((std::is_same_v<J,xecs::component::share_as_data_exclusive_tag>) || ...);
        }( reinterpret_cast<T*>(0))) || ... );

    }( xecs::types::null_tuple_v<T_QUERY> );
}
