
/**
 * @file rmngr/scheduler/scheduler.hpp
 */

#pragma once

#include <condition_variable>
#include <boost/mpl/inherit.hpp>
#include <boost/mpl/inherit_linearly.hpp>
#include <boost/mpl/for_each.hpp>

#include <rmngr/functor.hpp>
#include <rmngr/functor_queue.hpp>
#include <rmngr/thread_dispatcher.hpp>
#include <rmngr/scheduler/scheduling_graph.hpp>

// defaults
#include <boost/graph/adjacency_list.hpp>
#include <rmngr/graph/precedence_graph.hpp>
#include <rmngr/resource/resource_user.hpp>

namespace rmngr
{

struct SchedulerInterface
{
    struct SchedulableInterface
        : virtual public DelayedFunctorInterface
    {
        virtual ~SchedulableInterface() {}

        virtual void start(void) = 0;
        virtual void finish(void) = 0;
    };

    class WorkerInterface
    {
    protected:
        virtual void work(void) = 0;

    public:
        void operator() (void)
        {
            this->work();
        }
    };

    virtual void update(void) = 0;
    virtual bool empty(void) = 0;
    virtual size_t num_threads(void) const = 0;

    std::unique_lock< std::mutex >
    lock(void)
    {
        return std::unique_lock<std::mutex>(this->mutex);
    };

    void set_worker( WorkerInterface & worker_ )
    {
        this->worker = worker_;
    }

    template < typename Policy >
    struct ProtoProperty
    {
        typename Policy::ProtoProperty prop;
        operator typename Policy::ProtoProperty& ()
        { return this->prop; }
    };

    template < typename Policy >
    struct RuntimeProperty
    {
        typename Policy::RuntimeProperty prop;
        operator typename Policy::RuntimeProperty& ()
        { return this->prop; }
    };

    template < typename Policy >
    static typename Policy::ProtoProperty &
    proto_property( ProtoProperty<Policy> & s )
    { return s.prop; }

    template < typename Policy >
    static typename Policy::RuntimeProperty &
    runtime_property( RuntimeProperty<Policy> & s )
    { return s.prop; }

protected:
    std::mutex mutex;
    observer_ptr<WorkerInterface> worker;
};

template <typename T>
using DefaultGraph =
boost::adjacency_list<
    boost::setS,
    boost::vecS,
    boost::bidirectionalS,
    T
>;

template <typename Graph>
using DefaultRefinement =
QueuedPrecedenceGraph<
    Graph,
    ResourceUser
>;

struct DefaultSchedulingPolicy
{
    struct ProtoProperty {};
    struct RuntimeProperty {};

    void init( SchedulerInterface & ) {}
    void finish() {};

    template <typename Graph>
    void update( Graph & graph, SchedulerInterface & scheduler ) {}
};

/**
 * Compose the Scheduler from multiple Scheduling-Policies.
 *
 * @tparam SchedulingPolicies Model of boost::mpl Forward-Sequence
 * @tparam Refinement Refinement type for Precedence-Graph
 * @tparam Graph Graph<T> is a complete boost::graph type
 */
template <
    typename SchedulingPolicies = boost::mpl::vector<>,
    template <typename Graph> class Refinement = DefaultRefinement,
    template <typename T> class Graph = DefaultGraph
>
class Scheduler
  : public SchedulerInterface
{
private:
    using ProtoProperties =
        typename boost::mpl::inherit_linearly<
            SchedulingPolicies,
            boost::mpl::inherit< boost::mpl::_1, ProtoProperty<boost::mpl::_2> >
        >::type;

    using RuntimeProperties =
        typename boost::mpl::inherit_linearly<
            SchedulingPolicies,
            boost::mpl::inherit< boost::mpl::_1, RuntimeProperty<boost::mpl::_2> >
        >::type;

public:
    Scheduler( size_t nthreads = 1 )
      : graph( main_refinement ),
        currently_scheduled( nthreads+1 )
    {
        Initializer initializer{ *this };
        boost::mpl::for_each<
            SchedulingPolicies,
            boost::type<boost::mpl::_>
        >( initializer );
    }

    ~Scheduler()
    {
        Finisher finisher{ *this };
        boost::mpl::for_each<
            SchedulingPolicies,
            boost::type<boost::mpl::_>
        >( finisher );
    }

    size_t num_threads(void) const
    {
        return this->currently_scheduled.size()-1;
    }

    /**
     * Base class storing all scheduling info and the functor
     */
    struct Schedulable
        : public virtual SchedulerInterface::SchedulableInterface
        , public ProtoProperties
        , public RuntimeProperties
    {
        observer_ptr<Schedulable> last;

        Schedulable( Scheduler & scheduler_ )
            : scheduler(scheduler_) {}

        virtual ~Schedulable()
        {
            this->scheduler.currently_scheduled[ thread::id ] = this->last;
        }

        void start(void)
        {
            last = this->scheduler.currently_scheduled[ thread::id ];
            this->scheduler.currently_scheduled[ thread::id ] = this;
        }

        void finish(void)
        {
            if( this->scheduler.graph.finish(this) )
                delete this;
        }

        template < typename Policy >
        typename Policy::ProtoProperty & proto_property( void )
        { return scheduler.proto_property< Policy >( *this ); }

        template < typename Policy >
        typename Policy::RuntimeProperty & runtime_property( void )
        { return scheduler.runtime_property< Policy >( *this ); }

    private:
        Scheduler & scheduler;
    };

    template <typename DelayedFunctor>
    struct SchedulableFunctor
        : public DelayedFunctor
        , public Schedulable
    {
        SchedulableFunctor(
            DelayedFunctor && f,
            ProtoProperties const & props,
            Scheduler & scheduler
        )
            : DelayedFunctor( std::forward<DelayedFunctor>( f ) )
            , Schedulable( scheduler )
        {
            ProtoProperties& p = *this;
            p = props;
        }
    }; // struct SchedulableFunctor

    template <typename Functor>
    class ProtoSchedulableFunctor
        : public ProtoProperties
    {
    public:
        ProtoSchedulableFunctor(
            Functor const & f,
            Scheduler & scheduler_
        )
            : functor( f )
            , scheduler(scheduler_)
        {}

        template <
            typename DelayedFunctor,
            typename... Args
        >
        SchedulableFunctor<DelayedFunctor> *
        clone(
            DelayedFunctor && f,
            Args&&... args
        ) const
        {
            return new SchedulableFunctor<DelayedFunctor>(
                std::forward<DelayedFunctor>( f ),
                *this,
                this->scheduler
            );
        }

        template <typename... Args>
        typename std::result_of<Functor( Args... )>::type
        operator()( Args &&... args )
        {
            return this->functor( std::forward<Args>( args )... );
        }

    private:
        Functor functor;
        Scheduler & scheduler;
    }; // class ProtoSchedulableFunctor

    template <typename Functor>
    ProtoSchedulableFunctor<Functor>
    make_proto( Functor const & f )
    {
        return ProtoSchedulableFunctor<Functor>( f, *this );
    }

    template <
        typename Functor,
        typename PropertyFun
    >
    class PreparingProtoSchedulableFunctor
        : public ProtoSchedulableFunctor<Functor>
    {
    public:
        PreparingProtoSchedulableFunctor(
            Functor const & f,
            Scheduler & scheduler,
            PropertyFun const & prepare_properties_
        )
          : ProtoSchedulableFunctor<Functor>(f, scheduler)
          , prepare_properties(prepare_properties_)
        {}

        template <
            typename DelayedFunctor,
            typename... Args
        >
        SchedulableFunctor<DelayedFunctor> *
        clone(
            DelayedFunctor && f,
            Args&&... args
        ) const
        {
            SchedulableFunctor<DelayedFunctor> * schedulable =
                this->ProtoSchedulableFunctor<Functor>::clone(
                    std::forward<DelayedFunctor>(f),
                    std::forward<Args>(args)...
                );

            this->prepare_properties(
                schedulable,
                std::forward<Args>(args)...
            );

            return schedulable;
        }

    private:
        PropertyFun prepare_properties;
    };

    template <
        typename Functor,
        typename PropertyFun
    >
    PreparingProtoSchedulableFunctor<Functor, PropertyFun>
    make_proto(
        Functor const & f,
        PropertyFun const & prepare_properties
    )
    {
        return
        PreparingProtoSchedulableFunctor<
            Functor,
            PropertyFun
        >(
            f,
            *this,
            prepare_properties
        );
    }

private:
    std::condition_variable cv;
    std::mutex cv_mutex;
    std::atomic_flag currently_updating = ATOMIC_FLAG_INIT;

public:
    void update(void)
    {
        if( this->graph.is_deprecated() )
        {
            if( this->currently_updating.test_and_set() )
            {
                std::unique_lock<std::mutex> lock( this->cv_mutex );
                this->cv.wait( lock, [this]{ return !this->graph.is_deprecated(); } );
            }
            else
            {
                std::lock_guard< std::mutex > lock( this->mutex );

                this->graph.update();

                Updater updater{ *this };
                boost::mpl::for_each<
                    SchedulingPolicies,
                    boost::type<boost::mpl::_>
                >( updater );

                currently_updating.clear();
                this->cv.notify_all();
            }
        }
    }

    template <typename Policy>
    Policy & policy( void )
    {
        return this->policies;
    }

    bool empty(void)
    {
        auto lock = this->lock();
        return this->graph.empty();
    }

    FunctorQueue< Refinement< Graph< observer_ptr<Schedulable> > >, WorkerInterface >
    get_main_queue( void )
    {
        return make_functor_queue( this->main_refinement, *this->worker, this->mutex );
    }

    observer_ptr<Schedulable>
    get_current_schedulable( void )
    {
        return this->currently_scheduled[thread::id];
    }

    template <
        typename SRefinement = Refinement< Graph<observer_ptr<Schedulable>> >
    >
    observer_ptr<SRefinement>
    get_current_refinement( void )
    {
        std::lock_guard<std::mutex> lock( this->mutex );
        if( this->get_current_schedulable() )
        {
            auto r = this->main_refinement.template refinement<SRefinement>(
                       this->get_current_schedulable()
                   );

            if(r)
              return r;
        }
        return this->main_refinement;
    }

    template <
        typename SRefinement = Refinement< Graph<observer_ptr<Schedulable>> >
    >
    FunctorQueue< SRefinement, WorkerInterface >
    get_current_queue( void )
    {
        return make_functor_queue(
                   *this->get_current_refinement< SRefinement >(),
                   *this->worker,
                   this->mutex
               );
    }

    struct CurrentQueuePusher
    {
        Scheduler & scheduler;

        template <
            typename ProtoFunctor,
            typename DelayedFunctor,
            typename... Args
        >
        void operator() (
            ProtoFunctor const& proto,
            DelayedFunctor&& delayed,
            Args&&... args
        )
        {
            auto queue = scheduler.get_current_refinement();
            std::lock_guard< std::mutex > lock( scheduler.mutex );
            queue->push(
                   proto.clone(
                       std::forward<DelayedFunctor>(delayed),
                       std::forward<Args>(args)...
            ));
        }
    };

    template < typename Functor >
    using CurrentQueueFunctor = DelayingFunctor< CurrentQueuePusher, Functor, WorkerInterface >;

    template <typename ProtoFunctor>
    auto make_functor( ProtoFunctor const & proto )
    {
        return make_delaying( CurrentQueuePusher{ *this }, proto, *this->worker );
    }

    template <typename Functor, typename PropertyFun>
    auto make_functor( Functor const& f, PropertyFun const & prop )
    {
        return make_functor( this->make_proto( f, prop ) );
    }

    template<
        typename Policy,
        typename... Args
    >
    void update_property( Args&&... args )
    {
        this->lock();
        auto s = this->get_current_schedulable();
        this->policy< Policy >().update_property(*s, *s, std::forward<Args>(args)...);

        auto ref = dynamic_cast< Refinement<Graph<observer_ptr<Schedulable>>>* >(
                       & this->main_refinement.find_refinement_containing( s )
                   );

        // fixme: precedence policy
        ref->template update_vertex< ResourceUser >( s );

        this->update();
    }

private:
    Refinement< Graph<observer_ptr<Schedulable>> > main_refinement;
    SchedulingGraph< Graph<observer_ptr<Schedulable>> > graph;
    std::vector< observer_ptr<Schedulable> > currently_scheduled;

    typename boost::mpl::inherit_linearly<
        SchedulingPolicies,
        boost::mpl::inherit< boost::mpl::_1, boost::mpl::_2 >
    >::type policies;

    struct Initializer
    {
        Scheduler & scheduler;

        template <typename Policy>
        void operator() ( boost::type<Policy> )
        {
            scheduler.policy<Policy>().init( scheduler );
        }
    };

    struct Updater
    {
        Scheduler & scheduler;

        template <typename Policy>
        void operator() ( boost::type<Policy> )
        {
            scheduler.policy<Policy>().update( scheduler.graph, scheduler );
        }
    };

    struct Finisher
    {
        Scheduler & scheduler;

        template <typename Policy>
        void operator() ( boost::type<Policy> )
        {
            scheduler.policy<Policy>().finish();
        }
    };

}; // class Scheduler

} // namespace rmngr
