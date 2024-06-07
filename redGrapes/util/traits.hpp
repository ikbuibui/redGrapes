#pragma once
#include <type_traits>

namespace redGrapes::traits
{
    // Primary template for is_specialization_of
    template<typename, template<typename...> class>
    struct is_specialization_of : std::false_type
    {
    };

    // Specialization for types that are specializations of the template
    template<typename... Args, template<typename...> class Template>
    struct is_specialization_of<Template<Args...>, Template> : std::true_type
    {
    };

    template<typename T = std::false_type, typename...>
    struct first_type
    {
        using type = T;
    };

    // Convenience alias template
    template<typename... Ts>
    using first_type_t = typename first_type<Ts...>::type;

} // namespace redGrapes::traits
