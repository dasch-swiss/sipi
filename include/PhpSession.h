/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 *
 * This file implements access to the PHP session variables
 *
 * In out case in order to access the old PHP-based Salsah database, we need to access the
 * PHP session variables. This needed some reverse engineering... The name of the file storing
 * the session variable can be determined from the session cookie.
 */
#include <exception>
#include <fstream>

namespace Sipi {

/*!
 * \class PhpSession
 *
 * This class is Salsah specific and is not used in other environments! It implements reading
 * the session data from a PHP session using cookies. This class could probably easily be modified
 * to read any PHP session data.
 */
class PhpSession
{
private:
  std::ifstream *inf;//>! Open input stream for the PHP session file
  std::string lang;//>! Stores the session language
  int lang_id;
  int user_id;
  int active_project;

public:
  /*!
   * Empty constructor needed to create an "empty variable"
   */
  inline PhpSession(){};

  /*!
   * Constructor of PhpSession class
   *
   * \param inf_p Pointer to open ifstream of the PHP session file storing the session variables
   */
  PhpSession(std::ifstream *inf_p);

  /*!
   * Returns the language ID (Salsah specific)
   */
  inline int getLangId(void) { return lang_id; }

  /*!
   * Returns the user ID (Salsah specific)
   */
  inline int getUserId(void) { return user_id; }

  /*!
   * Returns the active project ID (Salsah specific)
   */
  inline int getActiveProject(void) { return active_project; }
};

}// namespace Sipi
