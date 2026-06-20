/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIIDENTIFIER_H
#define SIPI_SIPIIDENTIFIER_H

#include <string>

namespace Sipi {
class SipiIdentifier
{
private:
  std::string identifier;
  int page;

public:
  inline SipiIdentifier() { page = 0; }

  SipiIdentifier(const std::string &str);

  const std::string &getIdentifier() const { return identifier; }

  int getPage() const { return page; }
};
}// namespace Sipi
#endif// SIPI_SIPIIDENTIFIER_H
