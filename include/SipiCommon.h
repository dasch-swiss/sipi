/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPICOMMON_H
#define SIPI_SIPICOMMON_H


#include <cstdlib>

namespace Sipi {

/*!
 * We had to create a memcpy which will not dump a core if the data
 * is not aligned (problem with ubuntu!)
 *
 * @param to Adress where to copy the data to
 * @param from Adress from where the data is copied
 * @param len Number of bytes to copy
 */
extern void memcpy(void *to, const void *from, size_t len);

}// namespace Sipi

#endif// SIPI_SIPICOMMON_H
