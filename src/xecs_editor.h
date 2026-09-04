namespace xecs::editor
{
    struct tag
    {
        constexpr static auto typedef_v = xecs::component::type::exclusive_tag
        {
            .m_pName = "EditorTag"
        };
    };

    //-------------------------------------------------------------------------------

    struct prefab_instance
    {
  //      inline static xcore::err FullSerialize(xecs::serializer::stream&, bool, std::byte*, int&) noexcept;

        constexpr static auto typedef_v = xecs::component::type::data
        {
            .m_pName            = "EditorPrafabInstance"
  //      ,   .m_pFullSerializeFn = FullSerialize
        };

        struct component
        {
            enum class type : std::uint8_t
            { OVERRIDES         // This component was part of the original archetype and I am overriding some properties
            , NEW               // New component so all properties are assumed to be overwritten 
            , REMOVE            // I removed this component from the original archetype
            };

            struct override
            {
                std::string             m_PropertyName;
                xtextfile::crc32        m_PropertyType;
            };

            xecs::component::type::guid m_ComponentTypeGuid;
            std::vector<override>       m_PropertyOverrides;
            type                        m_Type;
        };

        xecs::prefab::guid              m_PrefabInstance;
        std::vector<component>          m_lComponents;
    };

    //-------------------------------------------------------------------------------

    struct link
    {
        xecs::component::entity         m_lEntity;
    };
}
