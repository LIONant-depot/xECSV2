namespace xecs::prefab
{
    // Prefab's real, resource-pipeline-integrated descriptor. A prefab is stored as a scene
    // (documentation/Editors/prefabs_plan.md, phase 1): Descriptors/Prefab/<b0>/<b1>/<guid>.desc/ holds this
    // Descriptor.txt (which entities, which one is the root, their names), entity_db/ (one file per member,
    // the scene's entity format) and ComponentDeps.txt - all written by mgr::Save, read by mgr::EnsureLoaded.
    // A folder whose descriptor has no Root is the old format (one Entity.txt with every member), still read.
    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource( std::string_view ) override {}
        void Validate       ( std::vector<std::string>& ) const noexcept override {}

        xecs::scene::permanent_id                   m_Root           = xecs::scene::invalid_permanent_id_v;   // every other member descends from it
        std::vector<xecs::scene::permanent_id>      m_ActiveEntities = {};                                     // the members, sorted (the scene's meaning: load reads only these)
        std::vector<xecs::scene::entity_name>       m_EntityNames    = {};                                     // the names the members were given, sorted by id

        XPROPERTY_VDEF
        ( "Prefab", descriptor
        , obj_member<"Root",           &descriptor::m_Root,           member_flags<flags::SHOW_READONLY>>
        , obj_member<"ActiveEntities", &descriptor::m_ActiveEntities, member_flags<flags::SHOW_READONLY>>
        , obj_member<"EntityNames",    &descriptor::m_EntityNames,    member_flags<flags::SHOW_READONLY>>
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
            return "Prefab";
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
