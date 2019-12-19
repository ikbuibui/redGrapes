/* Copyright 2019 Michael Sippel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <mutex>
#include <condition_variable>
#include <boost/graph/adjacency_list.hpp>
#include <redGrapes/graph/recursive_graph.hpp>
#include <redGrapes/graph/precedence_graph.hpp>
#include <redGrapes/graph/util.hpp>
#include <redGrapes/task/task.hpp>

#include <redGrapes/thread/thread_schedule.hpp>
#include <redGrapes/thread/thread_dispatcher.hpp>

namespace redGrapes
{

template <
    typename TaskID,
    typename TaskPtr,
    typename EventGraph =
        boost::adjacency_list<
            boost::listS,
            boost::listS,
            boost::bidirectionalS
        >
>
class SchedulingGraph
{
public:
    using EventID = typename boost::graph_traits< EventGraph >::vertex_descriptor;

    struct Event
    {
    private:
        std::mutex mutex;
        std::condition_variable cv;
        bool state;

    public:
        bool ready;
        TaskID task_id;
        std::atomic_int n_waiting;

        Event()
            : state( false )
            , ready( false )
            , n_waiting( 0 )
        {}

        auto lock()
        {
            return std::unique_lock< std::mutex >( mutex );
        }

        void notify()
        {
            if( ready )
            {
                {
                    auto l = lock();
                    state = true;
                }
                cv.notify_all();
            }
        }

        void wait()
        {
            auto l = lock();
            ++ n_waiting;
            cv.wait( l, [this]{ return state; } );
            -- n_waiting;
        }
    };

    std::mutex mutex;
    EventGraph m_graph;
    std::unordered_map< EventID, Event > events;
    std::unordered_map< TaskID , EventID > before_events;
    std::unordered_map< TaskID , EventID > after_events;

    bool empty()
    {
        std::lock_guard< std::mutex > lock( mutex );
        return boost::num_vertices( m_graph ) == 0;
    }

    EventID add_post_dependency( TaskID task_id )
    {
        std::lock_guard< std::mutex > lock( mutex );
        EventID id = make_event( task_id );

        boost::add_edge( id, after_events[ task_id ], m_graph );

        return id;
    }

    bool is_task_finished( TaskID task_id )
    {
        std::lock_guard< std::mutex > lock( mutex );
        return after_events.count(task_id) == 0;
    }

    void add_task( TaskPtr task_ptr )
    {
        auto & task = task_ptr.get();

        std::experimental::optional< TaskID > parent_id;
        if( task.parent )
            parent_id =  task.parent->locked_get().task_id;

        std::unique_lock< std::mutex > lock( mutex );
        EventID pre_event = make_event( task.task_id );
        EventID post_event = make_event( task.task_id );
        before_events[ task.task_id ] = pre_event;
        after_events[ task.task_id ] = post_event;

        task.hook_before([this, pre_event] { if( !finish_event( pre_event ) ) events[pre_event].wait(); });
        task.hook_after([this, post_event]{ finish_event( post_event ); });

        for(
            auto it = boost::in_edges( task_ptr.vertex, task_ptr.graph->graph() );
            it.first != it.second;
            ++ it.first
        )
        {
            auto & preceding_task = graph_get( boost::source( *(it.first), task_ptr.graph->graph() ), task_ptr.graph->graph() ).first;
            if( after_events.count(preceding_task.task_id) )
                boost::add_edge( after_events[ preceding_task.task_id ], pre_event, m_graph );
        }

        if( parent_id )
        {
            if( after_events.count( *parent_id ) )
                boost::add_edge( post_event, after_events[ *parent_id ], m_graph );
            else
                throw std::runtime_error("parent post-event doesn't exist!");
        }
    }

    auto update_vertex( TaskPtr const & task_ptr )
    {
        auto lock = task_ptr.graph->unique_lock();
        auto vertices = task_ptr.graph->update_vertex( task_ptr.vertex );
        auto task_id = task_ptr.get().task_id;

        {
            std::lock_guard< std::mutex > lock( mutex );
            for( auto v : vertices )
            {
                auto other_task_id = graph_get(v, task_ptr.graph->graph()).first.task_id;
                boost::remove_edge( after_events[task_id], before_events[other_task_id], m_graph );
            }

            for( auto v : vertices )
            {
                auto other_task_id = graph_get(v, task_ptr.graph->graph()).first.task_id;
                notify_event( before_events[ other_task_id ] );
            }
        }

        return vertices;
    }

    EventID make_event( TaskID task_id )
    {
        EventID event_id = boost::add_vertex( m_graph );
        events.emplace( std::piecewise_construct, std::forward_as_tuple(event_id), std::forward_as_tuple() );
        events[event_id].task_id = task_id;
        return event_id;
    }

    void remove_event( EventID id )
    {
        TaskID task_id = events[id].task_id;
        boost::clear_vertex( id, m_graph );
        boost::remove_vertex( id, m_graph );
        events.erase( id );

        if( before_events.count(task_id) && before_events[task_id] == id )
            before_events.erase( task_id );
        if( after_events.count(task_id) && after_events[task_id] == id )
            after_events.erase( task_id );
    }

    bool notify_event( EventID id )
    {
        if( events[id].ready && boost::in_degree( id, m_graph ) == 0 )
        {
            events[ id ].notify();

            // collect events to propagate to before to not invalidate the iterators in recursion
            std::vector< EventID > out;
            for( auto it = boost::out_edges( id, m_graph ); it.first != it.second; it.first++ )
                out.push_back( boost::target( *it.first, m_graph ) );

            while( events[id].n_waiting != 0 );
            remove_event( id );

            // propagate
            for( EventID e : out )
                notify_event( e );

            return true;
        }
        else
            return false;
    }

    bool finish_event( EventID id )
    {
        std::unique_lock< std::mutex > lock( mutex );
        events[ id ].ready = true;

        return notify_event( id );
    }
}; // class SchedulingGraph
    
} // namespace redGrapes