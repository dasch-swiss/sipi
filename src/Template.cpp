/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */


#include <assert.h>
#include <stdlib.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stdio.h>
#include <unistd.h>

#include "SipiError.h"
#include "Template.h"

static const char __file__[] = __FILE__;

namespace Sipi {

void Template::value(const std::string &name, const string &sval) {
    values[name] = sval;
}

void Template::value(const std::string &name, const char *cval) {
    values[name] = string(cval);
}

void Template::value(const std::string &name, int ival) {
    values[name] = to_string(ival);
}

void Template::value(const std::string &name, float fval) {
    values[name] = to_string(fval);
}

std::string Template::get(void) {
    std::string result;
    size_t pos, old_pos = 0, epos = 0;
    while ((pos = templatestr.find("{{", old_pos)) != string::npos) {
        // we found somtheing to replace
        result += templatestr.substr(epos, pos - epos); // copy and add string up to token
        epos = templatestr.find("}}", pos + 2);
        if (epos == string::npos) throw SipiError(__file__, __LINE__, "Error in template!");
        pos += 2; // sckip the {{
        string name = templatestr.substr(pos, epos - pos);
        result += values[name];
        epos += 2;
        old_pos = pos;
    }
    result += templatestr.substr(epos);
    return result;
}

}
