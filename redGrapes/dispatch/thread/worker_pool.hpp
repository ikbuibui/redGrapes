/* Copyright 2022-2024 Michael Sippel, Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "redGrapes/TaskFreeCtx.hpp"
#include "redGrapes/util/bitfield.hpp"

#include <memory>

namespace redGrapes
{
    struct HwlocContext;

    namespace dispatch
    {
        namespace thread
        {
            enum WorkerState
            {
                BUSY = 0,
                AVAILABLE = 1
            };

            template<typename Worker>
            struct WorkerThread;

            template<typename Worker>
            struct WorkerPool
            {
                using task_type = Worker::task_type;
                using TTask = task_type;

                WorkerPool(size_t n_workers);

                void emplace_workers(WorkerId base_id);

                /* get the number of workers in this pool
                 */
                inline size_t size()
                {
                    return workers.size();
                }

                /* signals all workers to start executing tasks
                 */
                void start();

                /* signals all workers that no new tasks will be added
                 */
                void stop();

                inline WorkerThread<Worker>& get_worker_thread(WorkerId local_worker_id)
                {
                    assert(local_worker_id < size());
                    return *workers[local_worker_id];
                }

                inline WorkerState get_worker_state(WorkerId local_worker_id)
                {
                    return worker_state.get(local_worker_id) ? WorkerState::AVAILABLE : WorkerState::BUSY;
                }

                // return true if state changes
                inline bool set_worker_state_global(WorkerId worker_id, WorkerState state)
                {
                    return worker_state.set(worker_id - m_base_id, state) != state;
                }

                // return true if state changes
                inline bool set_worker_state(WorkerId local_worker_id, WorkerState state)
                {
                    return worker_state.set(local_worker_id, state) != state;
                }

                template<typename T, typename F>
                inline std::optional<T> probe_worker_by_state(
                    F&& f,
                    bool expected_worker_state,
                    WorkerId start_worker_idx,
                    bool exclude_start = true)
                {
                    return worker_state.template probe_by_value<T, F>(
                        std::move(f),
                        expected_worker_state,
                        start_worker_idx,
                        exclude_start);
                }

                /*!
                 * tries to find an available worker, but potentially
                 * returns a busy worker if no free worker is available
                 *
                 * @return local_worker_id
                 */
                int find_free_worker();

                /* tries to find a task with uninialized dependency edges in the
                 * task-graph in the emplacement queues of other workers
                 * and removes it from there
                 */
                TTask* steal_new_task(Worker& worker)
                {
                    std::optional<TTask*> task = probe_worker_by_state<TTask*>(
                        [&worker, this](WorkerId idx) -> std::optional<TTask*>
                        {
                            // we have a candidate of a busy worker,
                            // now check its queue
                            if(TTask* t = get_worker_thread(idx).worker.emplacement_queue.pop())
                                return t;

                            // otherwise check own queue again
                            else if(TTask* t = worker.emplacement_queue.pop())
                                return t;

                            // else continue search
                            else
                                return std::nullopt;
                        },

                        // find a busy worker
                        dispatch::thread::WorkerState::BUSY,

                        // start next to current worker
                        worker.id - m_base_id);

                    return task ? *task : nullptr;
                }

                /* tries to find a ready task in any queue of other workers
                 * and removes it from the queue
                 */
                TTask* steal_ready_task(Worker& worker)
                {
                    std::optional<TTask*> task = probe_worker_by_state<TTask*>(
                        [&worker, this](WorkerId idx) -> std::optional<TTask*>
                        {
                            // we have a candidate of a busy worker,
                            // now check its queue
                            if(TTask* t = get_worker_thread(idx).worker.ready_queue.pop())
                                return t;

                            // otherwise check own queue again
                            else if(TTask* t = worker.ready_queue.pop())
                                return t;

                            // else continue search
                            else
                                return std::nullopt;
                        },

                        // find a busy worker
                        dispatch::thread::WorkerState::BUSY,

                        // start next to current worker
                        worker.id - m_base_id);

                    return task ? *task : nullptr;
                }

                // give worker a ready task if available
                // @return task if a new task was found, nullptr otherwise
                TTask* steal_task(Worker& worker)
                {
                    SPDLOG_DEBUG("steal task for worker (global id) {}", worker.id);

                    if(TTask* task = steal_ready_task(worker))
                    {
                        set_worker_state_global(worker.id, dispatch::thread::WorkerState::BUSY);
                        return task;
                    }

                    if(TTask* task = steal_new_task(worker))
                    {
                        task->pre_event.up();
                        task->init_graph();

                        if(task->get_pre_event().notify(true))
                        {
                            set_worker_state_global(worker.id, dispatch::thread::WorkerState::BUSY);
                            return task;
                        }
                    }

                    return nullptr;
                }


            private:
                std::vector<std::shared_ptr<dispatch::thread::WorkerThread<Worker>>> workers;
                AtomicBitfield worker_state;
                WorkerId num_workers;
                WorkerId m_base_id;
            };


        } // namespace thread
    } // namespace dispatch
} // namespace redGrapes

#include "redGrapes/dispatch/thread/worker_pool.tpp"
