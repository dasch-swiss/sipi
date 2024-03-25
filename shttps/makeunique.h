/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// An implementation of the C++14 make_unique function, copied from the standards document
// <http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3656.htm>.

#ifndef SIPI_MAKEUNIQUE_H
#define SIPI_MAKEUNIQUE_H

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace shttps {
template<class T> struct _Unique_if
{
  typedef std::unique_ptr<T> _Single_object;
};

template<class T> struct _Unique_if<T[]>
{
  typedef std::unique_ptr<T[]> _Unknown_bound;
};

template<class T, size_t N> struct _Unique_if<T[N]>
{
  typedef void _Known_bound;
};

template<class T, class... Args> typename _Unique_if<T>::_Single_object make_unique(Args &&...args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<class T> typename _Unique_if<T>::_Unknown_bound make_unique(size_t n)
{
  typedef typename std::remove_extent<T>::type U;
  return std::unique_ptr<T>(new U[n]());
}

template<class T, class... Args> typename _Unique_if<T>::_Known_bound make_unique(Args &&...) = delete;
}// namespace shttps

#endif// SIPI_MAKEUNIQUE_H
