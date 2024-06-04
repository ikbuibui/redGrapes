/* Copyright 2019-2024 Michael Sippel, Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @file redGrapes/resource/resource_user.hpp
 */

#pragma once

#include "redGrapes/util/chunked_list.hpp"

#include <fmt/format.h>

#include <initializer_list>
#include <memory>

namespace redGrapes
{
#ifndef REDGRAPES_RUL_CHUNKSIZE
#    define REDGRAPES_RUL_CHUNKSIZE 128
#endif

    template<typename TTask>
    struct ResourceBase;

    template<typename TTask>
    struct ResourceAccess;

    template<typename TTask>
    struct ResourceUsageEntry
    {
        std::shared_ptr<ResourceBase<TTask>> resource;
        typename ChunkedList<TTask*, REDGRAPES_RUL_CHUNKSIZE>::MutBackwardIterator task_entry;

        bool operator==(ResourceUsageEntry<TTask> const& other) const;
    };

    template<typename TTask>
    struct ResourceUser
    {
        ResourceUser(WorkerId worker_id);
        ResourceUser(ResourceUser const& other) = delete;

        ResourceUser(ResourceUser<TTask> const& other, WorkerId worker_id)
            : access_list(memory::Allocator(worker_id), other.access_list)
            , unique_resources(memory::Allocator(worker_id), other.unique_resources)
            , scope_level(other.scope_level)
        {
        }

        ResourceUser(std::initializer_list<ResourceAccess<TTask>> list, WorkerId worker_id);

        void add_resource_access(ResourceAccess<TTask> ra);
        void rm_resource_access(ResourceAccess<TTask> ra);
        void build_unique_resource_list();
        bool has_sync_access(std::shared_ptr<ResourceBase<TTask>> const& res);
        bool is_superset_of(ResourceUser const& a) const;

        friend bool is_serial(ResourceUser const& a, ResourceUser const& b)
        {
            TRACE_EVENT("ResourceUser", "is_serial");
            for(auto ra = a.access_list.crbegin(); ra != a.access_list.crend(); ++ra)
                for(auto rb = b.access_list.crbegin(); rb != b.access_list.crend(); ++rb)
                {
                    TRACE_EVENT("ResourceUser", "RA::is_serial");
                    if(ResourceAccess<TTask>::is_serial(*ra, *rb))
                        return true;
                }
            return false;
        }

        ChunkedList<ResourceAccess<TTask>, 8> access_list;
        ChunkedList<ResourceUsageEntry<TTask>, 8> unique_resources;
        uint8_t scope_level;
    }; // struct ResourceUser

} // namespace redGrapes

template<typename TTask>
struct fmt::formatter<redGrapes::ResourceUser<TTask>>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(redGrapes::ResourceUser<TTask> const& r, FormatContext& ctx)
    {
        auto out = ctx.out();
        out = fmt::format_to(out, "[");

        for(auto it = r.access_list.rbegin(); it != r.access_list.rend();)
        {
            out = fmt::format_to(out, "{}", *it);
            if(++it != r.access_list.rend())
                out = fmt::format_to(out, ",");
        }

        out = fmt::format_to(out, "]");
        return out;
    }
};

#include "redGrapes/resource/resource_user.tpp"
