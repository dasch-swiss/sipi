//
// Created by Lukas Rosenthaler on 2019-05-23.
//
#include <cstdlib>
#include <cstring>

#include "shttps/Connection.h"
#include "../SipiError.hpp"
#include "SipiIdentifier.h"

static const char __file__[] = __FILE__;

namespace Sipi {
    SipiIdentifier::SipiIdentifier(const std::string &str) {
        size_t pos;
        if ((pos = str.find("@")) != std::string::npos) {
            identifier = shttps::urldecode(str.substr(0, pos));
            try {
                page = stoi(str.substr(pos + 1));
            }
            catch(const std::invalid_argument& ia) {
                page = 0;
            }
            catch(const std::out_of_range& oor) {
                page = 0;
            }
        }
        else {
            identifier = shttps::urldecode(str);
            page = 0;
        }
    }
}
