#ifndef XECS_SETTINGS_H
#define XECS_SETTINGS_H
#pragma once

namespace xecs::settings
{
    constexpr auto max_component_types_v                = 128;
    constexpr auto max_data_components_per_entity_v     = 32;
    constexpr auto max_share_components_per_entity_v    = 8;
    constexpr auto max_tags_components_per_entity_v     = 32;
    constexpr auto max_components_per_entity_v          = max_data_components_per_entity_v + max_share_components_per_entity_v;
    constexpr auto max_entity_count_per_pool_v          = 50000;
    constexpr auto max_entities_v                       = 500000;
    constexpr auto virtual_page_size_v                  = 4096;
    constexpr auto max_scenes_v                         = 16;

    //------------------------------------------------------------------
    // Validations
    //------------------------------------------------------------------
    static_assert( max_components_per_entity_v == (max_data_components_per_entity_v + max_share_components_per_entity_v) );
    static_assert( max_component_types_v > (max_tags_components_per_entity_v + max_components_per_entity_v) );
    static_assert( max_entities_v > max_entity_count_per_pool_v );
}
#endif