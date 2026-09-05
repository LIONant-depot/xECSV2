namespace xecs::level
{
    // Level's real, resource-pipeline-integrated descriptor: just the list of member Scenes. Gets
    // the automatic array-of-resource-picker UI for free through xproperty's inspector (no custom
    // list widget needed) since xecs::scene::guid is itself a real def_guid.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource( std::string_view ) override {}
        void Validate       ( std::vector<std::string>& ) const noexcept override {}

        std::vector<xecs::scene::guid>   m_Scenes = {};

        XPROPERTY_VDEF
        ( "Level", descriptor
        , obj_member<"Scenes", &descriptor::m_Scenes>
        )
    };
    XPROPERTY_VREG(descriptor)

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor( void ) const noexcept override
        {
            return std::make_unique<descriptor>();
        }

        xresource::type_guid ResourceTypeGUID( void ) const noexcept override
        {
            return type_guid_v;
        }

        const char* ResourceTypeName( void ) const noexcept override
        {
            return "Level";
        }

        const xproperty::type::object& ResourceXPropertyObject( void ) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };
    inline static factory g_Factory{};
}
