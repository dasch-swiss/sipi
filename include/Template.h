/*
* Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __template_h
#define __template_h

#include <string>
#include <unordered_map>

/*!
 * \namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

    class Template {
    private:
        std::string templatestr;
        std::unordered_map <std::string, std::string> values;

    public:
        Template(std::string &template_p) { templatestr = template_p; };

        void value(const std::string &name, const std::string &sval);

        void value(const std::string &name, const char *cval);

        void value(const std::string &name, int ival);

        void value(const std::string &name, float fval);

        std::string get(void);
    };

}

#endif
