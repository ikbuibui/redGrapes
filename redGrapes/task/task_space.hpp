/* Copyright 2021-2024 Michael Sippel, Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "redGrapes/TaskFreeCtx.hpp"
#include "redGrapes/memory/block.hpp"
#include "redGrapes/util/trace.hpp"

#include <atomic>
#include <memory>

namespace redGrapes
{

    /*! TaskSpace handles sub-taskspaces of child tasks
     */
    template<typename TTask>
    struct TaskSpace : std::enable_shared_from_this<TaskSpace<TTask>>
    {
        std::atomic<unsigned long> task_count;

        unsigned depth;
        TTask* parent;

        // top space
        TaskSpace() : depth(0), parent(nullptr)
        {
            task_count = 0;
        }

        // sub space

        TaskSpace(TTask* parent) : depth(parent->space->depth + 1), parent(parent)
        {
            task_count = 0;
        }

        // add a new task to the task-space
        void submit(TTask* task)
        {
            TRACE_EVENT("TaskSpace", "submit()");
            task->space = this->shared_from_this();
            task->task = task;

            ++task_count;

            if(parent)
                assert(parent->is_superset_of(*task));

            for(auto r = task->unique_resources.rbegin(); r != task->unique_resources.rend(); ++r)
            {
                r->task_entry = r->resource->users.push(task);
            }
        }

        // remove task from task-space
        void free_task(TTask* task)
        {
            TRACE_EVENT("TaskSpace", "free_task()");
            unsigned count = task_count.fetch_sub(1) - 1;

            unsigned worker_id = task->worker_id;
            // auto task_scheduler_p = task->scheduler_p;
            task->~TTask(); // TODO check if this is really required

            // FIXME: len of the Block is not correct since FunTask object is bigger than sizeof(Task)
            // TODO check if arenaID is correct for the global alloc pool
            TaskFreeCtx::worker_alloc_pool->get_alloc(worker_id).deallocate(
                memory::Block{(uintptr_t) task, sizeof(TTask)});

            // TODO: implement this using post-event of root-task?
            //  - event already has in_edge count
            //  -> never have current_task = nullptr
            // SPDLOG_INFO("kill task... {} remaining", count);
            // if(count == 0)
            //     task_scheduler_p->wake_all(); // TODO think if this should call wake_all on all schedulers

            // to wake after barrier the check tells us that the root space is empty
            // if(TaskCtx<TTask>::root_space->empty()) cant be written because TaskCtx include root space
            if(count == 0 && depth == 0)
            {
                // TODO should i wake up workers also? that was the old behaviour
                TaskFreeCtx::cv.notify();
            }
        }

        bool empty() const
        {
            unsigned tc = task_count.load();
            return tc == 0;
        }
    };

} // namespace redGrapes
