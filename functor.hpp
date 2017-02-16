#pragma once

#include <vector>
#include <string>
#include <boost/preprocessor/stringize.hpp>

#include "queue.hpp"
#include "resource.hpp"

class Functor
{
    public:
        struct CheckFunctor
        {
            static inline bool check(Functor const& a, Functor const& b)
            {
                return a.depends_on(b);
            }
        };

        struct Label
        {
            static inline std::string getLabel(Functor const& f)
            {
                std::string label;
                label.append(f.name);
                label.append("\n");
                for(ResourceAccess const& a : f.resource_list)
                {
                    label.append(std::to_string(a.resourceID));
                    if(a.write)
                        label.append("w");
                    else
                        label.append("r");
                }

                return label;
            }
        };

        Functor(Queue<Functor, CheckFunctor, Label>& queue_, std::string const& name_, std::vector<ResourceAccess> const& ral)
            : queue(queue_), name(name_), resource_list(ral)
        {
        }

        std::string name;

        void operator() (void)
        {
            this->queue.push(*this);
        }

        bool depends_on(ResourceAccess const& a) const
        {
            for(ResourceAccess const& b : this->resource_list)
            {
                if(check_dependency(a, b))
                    return true;
            }
            return false;
        }

        bool depends_on(Functor const& f) const
        {
            for(ResourceAccess const& a : f.resource_list)
            {
                if(this->depends_on(a))
                    return true;
            }
            return false;
        }

    private:
        std::vector<ResourceAccess> resource_list;
        Queue<Functor, CheckFunctor, Label>& queue;
};

#define FUNCTOR(name, ...) \
    Functor name (queue, BOOST_PP_STRINGIZE(name), { __VA_ARGS__ });


