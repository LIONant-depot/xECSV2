namespace xecs::component
{
    xerr ref_count::Serialize( xecs::serializer::stream& TextFile, bool ) noexcept
    { 
        return TextFile.Field("GlobalIndex", m_Value);
    }

    //----------------------------------------------------------------------------------------------------

    xerr parent::Serialize( xecs::serializer::stream& TextFile, bool ) noexcept
    {
        return TextFile.Field("Parent", m_Value );
    }

    //----------------------------------------------------------------------------------------------------

    void parent::ReportReferences(std::vector<xecs::component::entity*>& List) noexcept
    {
        List.push_back(&m_Value);
    }

    //----------------------------------------------------------------------------------------------------

    xerr children::FullSerialize( xecs::serializer::stream& TextFile, bool isRead, children* pData, int& Count ) noexcept
    {
        xerr Error;

        //
        // For each of the components write how many children they have
        //
        int nTotalChildren = 0;
        if( Error = TextFile.Record("Children"
        ,[&](std::size_t& Size, xerr& ) noexcept
        {
            if( isRead ) Count  = static_cast<int>(Size);
            else         Size   = static_cast<std::size_t>(Count);
        }
        , [&]( std::size_t Index, xerr& Err ) noexcept
        {
            auto& Children = reinterpret_cast<children*>(pData)[Index];

            int nChildren = isRead ? static_cast<int>(Children.m_List.size()) : 0;
            if( Err = TextFile.Field("nChildren", nChildren); Err ) return;
            if(isRead) Children.m_List.resize(nChildren);

            nTotalChildren += nChildren;

        }) ) return Error;

        //
        // Now that we know all the children from all the components list write all the children together
        //
        auto pChildren = reinterpret_cast<children*>(pData);
        int  iChild    = 0;
        if( nTotalChildren && (Error = TextFile.Record("AllChildren"
        , [&](std::size_t& Size, xerr& ) noexcept
        {
            if (isRead) xassert( nTotalChildren == static_cast<int>(Size) );
            else        Size = static_cast<std::size_t>(nTotalChildren);
        }
        , [&]( std::size_t, xerr& Err ) noexcept
        {
            while(iChild >= pChildren->m_List.size() )
            {
                pChildren++;
                iChild = 0;
            }

            Err = TextFile.Field("Entity", pChildren->m_List[iChild] );

        }))) return Error;

        return Error;
    }

    //----------------------------------------------------------------------------------------------------

    void children::ReportReferences(std::vector<xecs::component::entity*>& List) noexcept
    {
        for( auto& E : m_List ) List.push_back(&E);
    }

}