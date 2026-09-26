#ifndef XECS_SHARED_COMPONENT_TEMPLATE_H
#define XECS_SHARED_COMPONENT_TEMPLATE_H
#pragma once

// SharedComponentTemplate - export-only snapshot of one interned share-component value
// (component type identity + serialized property values). Rehydrating creates/adds a share
// component via the normal ECS intern path; there is never a persistent link from entity back
// to the template. Descriptor-only resource (no compiler), same treatment as Prefab/Scene/Level.
namespace xecs::shared_component_template
{
    inline constexpr auto type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("SharedComponentTemplate"));
    using guid = xresource::full_guid;

    // One reflected property row - same {Path, TypeGuid, ValueStr} shape
    // xscene::commands::SnapshotComponentProperties already uses for undo snapshots.
    struct property_value
    {
        std::string     m_Path;
        std::uint32_t   m_TypeGuid = 0;
        std::string     m_Value;

        XPROPERTY_DEF
        ( "SharedComponentTemplateProperty", property_value
        , obj_member<"Path",     &property_value::m_Path>
        , obj_member<"TypeGuid", &property_value::m_TypeGuid>
        , obj_member<"Value",    &property_value::m_Value>
        )
    };
    XPROPERTY_REG(property_value)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        void SetupFromSource( std::string_view ) override {}
        void Validate       ( std::vector<std::string>& ) const noexcept override {}

        std::uint64_t                 m_ComponentTypeGuid = 0;
        std::vector<property_value>   m_Properties        = {};

        XPROPERTY_VDEF
        ( "SharedComponentTemplate", descriptor
        , obj_member<"ComponentTypeGuid", &descriptor::m_ComponentTypeGuid>
        , obj_member<"Properties",        &descriptor::m_Properties>
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
            return "SharedComponentTemplate";
        }

        const xproperty::type::object& ResourceXPropertyObject( void ) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };
    namespace details { struct factory_holder { inline static factory s_Instance{}; }; }
    inline factory& GetFactory() noexcept { return details::factory_holder::s_Instance; }
}

#endif
