/* Copyright 2019-2024 Michael Sippel, Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * @file redGrapes/resource/fieldresource.hpp
 */

#pragma once

#include "redGrapes/TaskFreeCtx.hpp"
#include "redGrapes/resource/access/field.hpp"
#include "redGrapes/resource/resource.hpp"
#include "redGrapes/util/traits.hpp"

namespace redGrapes
{

    namespace trait
    {

        template<typename Container>
        struct Field
        {
        };

        template<typename T>
        struct Field<std::vector<T>>
        {
            using Item = T;
            static constexpr size_t dim = 1;

            static std::array<size_t, dim> extent(std::vector<T>& v)
            {
                return {v.size()};
            }

            static Item& get(std::vector<T>& v, std::array<size_t, dim> index)
            {
                return v[index[0]];
            }
        };

        template<typename T, size_t N>
        struct Field<std::array<T, N>>
        {
            using Item = T;
            static constexpr size_t dim = 1;

            static Item& get(std::array<T, N>& array, std::array<size_t, dim> index)
            {
                return array[index[0]];
            }
        };

        template<typename T, size_t Nx, size_t Ny>
        struct Field<std::array<std::array<T, Nx>, Ny>>
        {
            using Item = T;
            static constexpr size_t dim = 2;

            static Item& get(std::array<std::array<T, Nx>, Ny>& array, std::array<size_t, dim> index)
            {
                return array[index[1]][index[0]];
            }
        };

    }; // namespace trait

    namespace fieldresource
    {

        template<typename Container>
        struct AreaGuard : SharedResourceObject<Container, access::FieldAccess<trait::Field<Container>::dim>>
        {
            static constexpr size_t dim = trait::Field<Container>::dim;
            using Item = typename trait::Field<Container>::Item;
            using Index = std::array<size_t, dim>;

            bool contains(Index index) const noexcept
            {
                for(size_t d = 0; d < dim; d++)
                {
                    if(index[d] < m_area[d][0] || index[d] >= m_area[d][1])
                        return false;
                }
                return true;
            }

        protected:
            AreaGuard(ResourceId id, std::shared_ptr<Container> const& obj)
                : SharedResourceObject<Container, access::FieldAccess<dim>>(id, obj)
            {
            }

            template<typename... Args>
            AreaGuard(ResourceId id, Args&&... args)
                : SharedResourceObject<Container, access::FieldAccess<dim>>(id, std::forward<Args>(args)...)
            {
            }

            AreaGuard(
                Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res,
                std::shared_ptr<Container> const& obj)
                : SharedResourceObject<Container, access::FieldAccess<dim>>(res, obj)
            {
            }

            template<typename... Args>
            AreaGuard(Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res, Args&&... args)
                : SharedResourceObject<Container, access::FieldAccess<dim>>(res, std::forward<Args>(args)...)
            {
            }

            AreaGuard(AreaGuard const& other, Index begin, Index end)
                : SharedResourceObject<Container, access::FieldAccess<dim>>(other)
                , m_area(other.make_area(begin, end))
            {
            }

            Item& get(Index index) const
            {
                if(!contains(index))
                    throw std::out_of_range("invalid area access");

                return trait::Field<Container>::get(*this->obj, index);
            }

            access::ArrayAccess<access::AreaAccess, dim> make_area(Index begin, Index end) const
            {
                std::array<access::AreaAccess, dim> sub_area;
                for(int d = 0; d < dim; ++d)
                    sub_area[d] = access::AreaAccess({begin[d], end[d]});

                if(!m_area.is_superset_of(sub_area))
                    throw std::out_of_range("invalid sub area");

                return access::ArrayAccess<access::AreaAccess, dim>(sub_area);
            }

            access::ArrayAccess<access::AreaAccess, dim> m_area;
        };

        template<typename Container>
        struct ReadGuard : AreaGuard<Container>
        {
            static constexpr size_t dim = trait::Field<Container>::dim;
            using typename AreaGuard<Container>::Index;
            using typename AreaGuard<Container>::Item;

            ReadGuard read() const noexcept
            {
                return *this;
            }

            ReadGuard area(Index begin, Index end) const
            {
                return ReadGuard(*this, begin, end);
            }

            ReadGuard at(Index pos) const
            {
                Index end = pos;
                for(size_t d = 0; d < dim; ++d)
                    end[d]++;
                return ReadGuard(*this, pos, end);
            }

            Item const& operator[](Index index) const
            {
                return this->get(index);
            }

            Container const* operator->() const noexcept
            {
                return this->obj.get();
            }

            operator ResourceAccess() const noexcept
            {
                return this->make_access(access::FieldAccess<dim>(access::IOAccess::read, this->m_area));
            }

        protected:
            ReadGuard(ReadGuard const& other, Index begin, Index end) : AreaGuard<Container>(other, begin, end)
            {
            }

            ReadGuard(ResourceId id, std::shared_ptr<Container> const& obj) : AreaGuard<Container>(id, obj)
            {
            }

            template<typename... Args>
            ReadGuard(ResourceId id, Args&&... args) : AreaGuard<Container>(id, std::forward<Args>(args)...)
            {
            }

            ReadGuard(
                Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res,
                std::shared_ptr<Container> const& obj)
                : AreaGuard<Container>(res, obj)
            {
            }

            template<typename... Args>
            ReadGuard(Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res, Args&&... args)
                : AreaGuard<Container>(res, std::forward<Args>(args)...)
            {
            }
        };

        template<typename Container>
        struct WriteGuard : ReadGuard<Container>
        {
            static constexpr size_t dim = trait::Field<Container>::dim;
            using typename ReadGuard<Container>::Index;
            using typename ReadGuard<Container>::Item;

            WriteGuard write() const noexcept
            {
                return *this;
            }

            WriteGuard area(Index begin, Index end) const
            {
                return WriteGuard(*this, begin, end);
            }

            WriteGuard at(Index pos) const
            {
                Index end = pos;
                for(size_t d = 0; d < dim; ++d)
                    end[d]++;
                return WriteGuard(*this, pos, end);
            }

            Item& operator[](Index index) const
            {
                return this->get(index);
            }

            Container* operator->() const noexcept
            {
                return this->obj.get();
            }

            operator ResourceAccess() const noexcept
            {
                return this->make_access(access::FieldAccess<dim>(access::IOAccess::write, this->m_area));
            }

        protected:
            WriteGuard(WriteGuard const& other, Index begin, Index end) : ReadGuard<Container>(other, begin, end)
            {
            }

            WriteGuard(ResourceId id, std::shared_ptr<Container> const& obj) : ReadGuard<Container>(id, obj)
            {
            }

            template<typename... Args>
            WriteGuard(ResourceId id, Args&&... args) : ReadGuard<Container>(id, std::forward<Args>(args)...)
            {
            }

            WriteGuard(
                Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res,
                std::shared_ptr<Container> const& obj)
                : ReadGuard<Container>(res, obj)
            {
            }

            template<typename... Args>
            WriteGuard(Resource<access::FieldAccess<trait::Field<Container>::dim>> const& res, Args&&... args)
                : ReadGuard<Container>(res, std::forward<Args>(args)...)
            {
            }
        };

    } // namespace fieldresource

    template<typename Container>
    struct FieldResource : fieldresource::WriteGuard<Container>
    {
        static constexpr size_t dim = trait::Field<Container>::dim;

        FieldResource(Container* c)
            : fieldresource::WriteGuard<Container>(TaskFreeCtx::create_resource_uid(), std::shared_ptr<Container>(c))
        {
        }

        template<typename... Args>
        requires(
            !(traits::is_specialization_of_v<std::decay_t<traits::first_type_t<Args...>>, FieldResource>
              || std::is_same_v<std::decay_t<traits::first_type_t<Args...>>, Container*>) )

        FieldResource(Args&&... args)
            : fieldresource::WriteGuard<Container>(TaskFreeCtx::create_resource_uid(), std::forward<Args>(args)...)
        {
        }

        template<typename U>
        FieldResource(FieldResource<U> const& res, Container* c)
            : fieldresource::WriteGuard<Container>(res, std::shared_ptr<Container>(c))
        {
        }

        template<typename U, typename... Args>
        FieldResource(FieldResource<U> const& res, Args&&... args)
            : fieldresource::WriteGuard<Container>(res, std::forward<Args>(args)...)
        {
        }
    };

}; // namespace redGrapes
