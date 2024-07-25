/* Copyright 2019-2024 Michael Sippel, Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @file redGrapes/resource/ioresource.hpp
 */

#pragma once

#include "redGrapes/TaskFreeCtx.hpp"
#include "redGrapes/resource/access/io.hpp"
#include "redGrapes/resource/resource.hpp"
#include "redGrapes/util/traits.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace redGrapes
{
    namespace ioresource
    {

        template<typename T>
        struct Guards : public SharedResourceObject<T, access::IOAccess>
        {
            auto read() const noexcept
            {
                return ResourceAccessPair<T const>{this->obj, this->res.make_access(access::IOAccess::read)};
            }

            auto write() const noexcept
            {
                return ResourceAccessPair<T>{this->obj, this->res.make_access(access::IOAccess::write)};
            }


        protected:
            using SharedResourceObject<T, access::IOAccess>::SharedResourceObject;
        };


    } // namespace ioresource

    template<typename T>
    struct IOResource : public ioresource::Guards<T>
    {
        IOResource(std::shared_ptr<T> const& o) : ioresource::Guards<T>(TaskFreeCtx::create_resource_uid(), o)
        {
        }

        template<typename... Args>
        requires(
            !(traits::is_specialization_of_v<std::decay_t<traits::first_type_t<Args...>>, IOResource>
              || std::is_same_v<std::decay_t<traits::first_type_t<Args...>>, std::shared_ptr<T>>) )
        IOResource(Args&&... args)
            : ioresource::Guards<T>(TaskFreeCtx::create_resource_uid(), std::forward<Args>(args)...)
        {
        }

        template<typename U>
        IOResource(IOResource<U> const& res, std::shared_ptr<T> const& obj) : ioresource::Guards<T>(res, obj)
        {
        }

        template<typename U, typename... Args>
        IOResource(IOResource<U> const& res, Args&&... args) : ioresource::Guards<T>(res, std::forward<Args>(args)...)
        {
        }

    }; // struct IOResource

} // namespace redGrapes
