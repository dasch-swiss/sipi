/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIROTATION_H
#define SIPI_SIPIROTATION_H

#include <string>

namespace Sipi {

//-------------------------------------------------------------------------
// local class to handle IIIF Rotation parameters
//
class SipiRotation
{
private:
  bool mirror = false;
  float rotation = 0.F;

public:
  SipiRotation();

  SipiRotation(std::string str);

  inline bool get_rotation(float &rot)
  {
    rot = rotation;
    return mirror;
  };

  friend std::ostream &operator<<(std::ostream &lhs, const SipiRotation &rhs);
};

}// namespace Sipi
#endif// SIPI_SIPIROTATION_H
