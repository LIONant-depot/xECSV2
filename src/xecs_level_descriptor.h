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
    // See xecs_scene_descriptor.h's own GetFactory for why this is a class-static-member holder
    // rather than a namespace-scope `inline static` (internal linkage, duplicates per TU) or a
    // function-local static (would change eager registration to lazy).
    namespace details { struct factory_holder { inline static factory s_Instance{}; }; }
    inline factory& GetFactory() noexcept { return details::factory_holder::s_Instance; }
}
