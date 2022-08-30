/* Copyright 2021-2022 Michael Sippel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <atomic>
#include <vector>
#include <mutex>

#include <redGrapes/util/allocator.hpp>
#include <redGrapes/task/task.hpp>
#include <redGrapes/task/queue.hpp>

#ifndef TASK_ALLOCATOR_CHUNKSIZE
#define TASK_ALLOCATOR_CHUNKSIZE 0x800000
#endif

namespace redGrapes
{

/*!
 */
struct TaskSpace : std::enable_shared_from_this<TaskSpace>
{
    /* task storage */
    memory::Allocator< TASK_ALLOCATOR_CHUNKSIZE > task_storage;
    std::atomic< unsigned long > task_count;
    std::atomic< unsigned long > task_capacity;

    /* queue */
    std::mutex emplacement_mutex;
    task::Queue emplacement_queue;

    // ticket (id) of currently initialized task
    std::atomic< unsigned > serving_task_id;

    unsigned depth;
    Task * parent;

    virtual ~TaskSpace();
    
    // top space
    TaskSpace();

    // sub space
    TaskSpace( Task & parent );

    virtual bool is_serial( Task& a, Task& b );
    virtual bool is_superset( Task& a, Task& b );

    template < typename F >
    Task & emplace_task( F&& f, TaskProperties&& prop )
    {
        // allocate memory
        FunTask<F> * task = task_storage.m_alloc<FunTask<F>>();
        if( ! task )
            throw std::runtime_error("out of memory");

        // construct task in-place
        new (task) FunTask<F> ( std::move(f), std::move(prop) );

        task->space = shared_from_this();
        task->task = task;
        task->next = nullptr;

        ++ task_count;

        if( parent )
            assert( is_superset(*parent, *task) );

        emplacement_queue.push(task);

        top_scheduler->wake_one_worker();

        return *task;
    }
    
    /*! take tasks from the emplacement queue and initialize them,
     *  until a task is initialized whose execution could start immediately
     *
     * @return true if ready task found,
     *         false if no tasks available
     */
    bool init_until_ready();

    void try_remove( Task & task );

    bool empty() const;
};

} // namespace redGrapes
