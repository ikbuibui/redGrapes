/* Copyright 2023 Michael Sippel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <memory>
#include <optional>

#include <hwloc.h>
#include <redGrapes/util/spinlock.hpp>
#include <spdlog/spdlog.h>

namespace redGrapes
{
namespace memory
{

/* maintains a lockfree singly-linked list of arbitrarily sized data
 * chunks
 *
 * allowed operations:
 *   - append new chunks at head
 *   - erase any chunk which is not current head
 *   - reversed iteration (starting at head)
 *
 * each chunk is managed through a `std::shared_ptr` which points to a
 * contiguous block containing list-metadata, the chunk-control-object
 * (`ChunkData`) and freely usable data.
 */
template <
    typename ChunkData,
    template <typename> class Allocator
>
struct ChunkList
{
//private:
    struct Chunk
    {
        bool volatile deleted;
        std::shared_ptr< Chunk > prev;
        ChunkData * chunk_data;

        template < typename... Args >
        Chunk( uintptr_t chunk_data, Args&&... args ) : deleted(false), prev(nullptr), chunk_data((ChunkData*)chunk_data)
        {
            new ( get() ) ChunkData ( std::forward<Args>(args)... );
        }

        ~Chunk()
        {
            get()->~ChunkData();
        }

        /* flag this chunk as deleted and call ChunkData destructor
         */
        void erase()
        {
            deleted = true;
        }

        /* adjusts `prev` so that it points to a non-deleted chunk again
         * this should free the shared_ptr of the original prev
         * in case no iterators point to it
         */
        void skip_deleted_prev()
        {
            auto p = std::atomic_load( &prev );
            while( p && p->deleted )
            {
                std::atomic_store( &prev, std::atomic_load(&p->prev) );
                p = std::atomic_load( &prev );
            }
        }

        ChunkData * get() const
        {
            return chunk_data;
        }
    };

    Allocator< uint8_t > alloc;
    std::shared_ptr< Chunk > head;
    size_t const chunk_size;

    /* keeps a single, predefined pointer
     * and frees it on deallocate.
     * used to spoof the allocated size to be bigger than requested.
     */
    template <typename T>
    struct StaticAlloc
    {
        typedef T value_type;

        Allocator< T > alloc;
        T * ptr;

        StaticAlloc( Allocator<uint8_t> alloc, size_t n_bytes )
            : alloc(alloc)
            , ptr( (T*)alloc.allocate( n_bytes ) )
        {}

        template<typename U>
        constexpr StaticAlloc( StaticAlloc<U> const & other ) noexcept
            : alloc(other.alloc)
            , ptr((T*)other.ptr)
        {}

        T * allocate( size_t n ) noexcept
        {
            return ptr;
        }

        void deallocate( T * _p, std::size_t _n ) noexcept
        {
            alloc.deallocate( ptr, 1 );
        }
    };

public:
    ChunkList( Allocator< uint8_t > && alloc, size_t chunk_size )
        : alloc( alloc )
        , head( nullptr )
        , chunk_size( chunk_size )
    {
        if( chunk_size <= get_controlblock_size() )
             spdlog::error("chunksize = {}, control block ={}", chunk_size, get_controlblock_size());

        assert( chunk_size > get_controlblock_size() );
    }

    static constexpr size_t get_controlblock_size()
    {

        /* TODO: use sizeof( ...shared_ptr_inplace_something... )
         */
        size_t const shared_ptr_size = 128;

        return sizeof(Chunk) + shared_ptr_size;
    }

    constexpr size_t get_chunk_capacity()
    {
        return chunk_size - get_controlblock_size();
    }

    /* initializes a new chunk
     */
    void add_chunk()
    {
        TRACE_EVENT("Allocator", "ChunkList add_chunk()");

        /* we are relying on std::allocate_shared
         * to do one *single* allocation which contains:
         * - shared_ptr control block
         * - chunk control block
         * - chunk data
         * whereby chunk data is not included by sizeof(Chunk),
         * but reserved by StaticAlloc.
         * This works because shared_ptr control block lies at lower address.
         */
        StaticAlloc<void> chunk_alloc( this->alloc, chunk_size );
        uintptr_t base = (uintptr_t)chunk_alloc.ptr;
        append_chunk(
            std::allocate_shared< Chunk >(
                chunk_alloc,
                base + get_controlblock_size(),
                base + get_controlblock_size() + sizeof(ChunkData),
                base + chunk_size
            )
        );
    }

    /* atomically appends a floating chunk to this list
     */
    void append_chunk( std::shared_ptr< Chunk > new_head )
    {
        TRACE_EVENT("Allocator", "append_chunk()");
        bool append_successful = false;
        while( ! append_successful )
        {
            std::shared_ptr< Chunk > old_head = std::atomic_load( &head );
            std::atomic_store( &new_head->prev, old_head );
            append_successful = std::atomic_compare_exchange_strong<Chunk>( &head, &old_head, new_head );
        }
    }

    template < bool is_const = false >
    struct BackwardIterator
    {
        std::shared_ptr< Chunk > c;

        void erase()
        {
            c->erase();
        }

        bool operator!=(BackwardIterator const & other) const
        {
            return c != other.c;
        }

        operator bool() const
        {
            return (bool)c;
        }

        typename std::conditional<
            is_const,
            ChunkData const *,
            ChunkData *
        >::type
        operator->() const
        {
            return c->get();
        }
        
        typename std::conditional<
            is_const,
            ChunkData const &,
            ChunkData &
        >::type
        operator*() const
        {
            return *c->get();
        }

        void optimize()
        {
            if(c)
                c->skip_deleted_prev();
        }

        BackwardIterator& operator++()
        {
            if( c )
            {
                c->skip_deleted_prev();
                c = c->prev;
            }

            return *this;
        }
    };

    using ConstBackwardIterator = BackwardIterator< true >;
    using MutBackwardIterator = BackwardIterator< false >;

    /* get iterator starting at current head, iterating backwards from
     * most recently added to least recently added
     */
    MutBackwardIterator rbegin() const
    {
        return MutBackwardIterator{ std::atomic_load(&head) };
    }

    MutBackwardIterator rend() const
    {
        return MutBackwardIterator{ std::shared_ptr<Chunk>() };
    }

    ConstBackwardIterator crbegin() const
    {
        return ConstBackwardIterator{ std::atomic_load(&head) };
    }

    ConstBackwardIterator crend() const
    {
        return ConstBackwardIterator{ std::shared_ptr<Chunk>() };
    }
    
    /* Flags chunk at `pos` as erased. Actual removal is delayed until
     * iterator stumbles over it.
     *
     * Since we only append to the end and `chunk` is not `head`,
     * there wont occur any inserts after this chunk.
     */
    void erase( MutBackwardIterator pos )
    {
        pos.erase();
    }

};

} // namespace memory

} // namespace redGrapes
