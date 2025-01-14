/* Copyright 2024 Tapish Narwal
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <type_traits>
#include <typeinfo>
#ifndef _MSC_VER
#    include <cxxabi.h>
#endif
#include <cstdlib>
#include <memory>
#include <string>

namespace redGrapes::util
{
    /**
     * Demangled type names from
     * https://stackoverflow.com/questions/81870/is-it-possible-to-print-a-variables-type-in-standard-c
     */
    template<class T>
    std::string type_name()
    {
        typedef typename std::remove_reference<T>::type TR;
        std::unique_ptr<char, void (*)(void*)> own(
#ifndef _MSC_VER
            abi::__cxa_demangle(typeid(TR).name(), nullptr, nullptr, nullptr),
#else
            nullptr,
#endif
            std::free);
        std::string r = own != nullptr ? own.get() : typeid(TR).name();
        if(std::is_const<TR>::value)
            r += " const";
        if(std::is_volatile<TR>::value)
            r += " volatile";
        if(std::is_lvalue_reference<T>::value)
            r += "&";
        else if(std::is_rvalue_reference<T>::value)
            r += "&&";
        return r;
    }
} // namespace redGrapes::util
