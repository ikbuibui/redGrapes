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
        struct ReadGuard : public SharedResourceObject<T, access::IOAccess>
        {
            operator ResourceAccess() const noexcept
            {
                return this->make_access(access::IOAccess::read);
            }

            ReadGuard read() const noexcept
            {
                return *this;
            }

            T const& operator*() const noexcept
            {
                return *this->obj;
            }

            T const* operator->() const noexcept
            {
                return this->obj.get();
            }

            T const* get() const noexcept
            {
                return this->obj.get();
            }

        protected:
            ReadGuard(ResourceId id, std::shared_ptr<T> const& obj)
                : SharedResourceObject<T, access::IOAccess>(id, obj)
            {
            }

            template<typename... Args>
            ReadGuard(ResourceId id, Args&&... args)
                : SharedResourceObject<T, access::IOAccess>(id, std::forward<Args>(args)...)
            {
            }

            ReadGuard(Resource<access::IOAccess> const& res, std::shared_ptr<T> const& obj)
                : SharedResourceObject<T, access::IOAccess>(res, obj)
            {
            }

            template<typename... Args>
            ReadGuard(Resource<access::IOAccess> const& res, Args&&... args)
                : SharedResourceObject<T, access::IOAccess>(res, std::forward<Args>(args)...)
            {
            }
        };

        template<typename T>
        struct WriteGuard : public ReadGuard<T>
        {
            operator ResourceAccess() const noexcept
            {
                return this->make_access(access::IOAccess::write);
            }

            WriteGuard write() const noexcept
            {
                return *this;
            }

            T& operator*() const noexcept
            {
                return *this->obj;
            }

            T* operator->() const noexcept
            {
                return this->obj.get();
            }

            T* get() const noexcept
            {
                return this->obj.get();
            }

        protected:
            WriteGuard(ResourceId id, std::shared_ptr<T> const& obj) : ReadGuard<T>(id, obj)
            {
            }

            template<typename... Args>
            WriteGuard(ResourceId id, Args&&... args) : ReadGuard<T>(id, std::forward<Args>(args)...)
            {
            }

            WriteGuard(Resource<access::IOAccess> const& res, std::shared_ptr<T> const& obj) : ReadGuard<T>(res, obj)
            {
            }

            template<typename... Args>
            WriteGuard(Resource<access::IOAccess> const& res, Args&&... args)
                : ReadGuard<T>(res, std::forward<Args>(args)...)
            {
            }
        };

    } // namespace ioresource

    template<typename T>
    struct IOResource : public ioresource::WriteGuard<T>
    {
        IOResource(std::shared_ptr<T> const& o) : ioresource::WriteGuard<T>(TaskFreeCtx::create_resource_uid(), o)
        {
        }

        template<typename... Args>
        requires(
            !(traits::is_specialization_of_v<std::decay_t<traits::first_type_t<Args...>>, IOResource>
              || std::is_same_v<std::decay_t<traits::first_type_t<Args...>>, std::shared_ptr<T>>) )
        IOResource(Args&&... args)
            : ioresource::WriteGuard<T>(TaskFreeCtx::create_resource_uid(), std::forward<Args>(args)...)
        {
        }

        template<typename U>
        IOResource(IOResource<U> const& res, std::shared_ptr<T> const& obj) : ioresource::WriteGuard<T>(res, obj)
        {
        }

        template<typename U, typename... Args>
        IOResource(IOResource<U> const& res, Args&&... args)
            : ioresource::WriteGuard<T>(res, std::forward<Args>(args)...)
        {
        }


    }; // struct IOResource

} // namespace redGrapes
