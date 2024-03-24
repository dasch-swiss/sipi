/*
* Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __salsah_h
#define __salsah_h

#include <cstring>
#include <map>


//#include "mongoose.h"
#include "Connection.h"

/*!
 * \namespace Sipi Is used for all Sipi things.
 */
namespace Sipi {

    /*!
     * \class Salsah
     *
     * This is a "private" classed to encapsulate access to the old PHP-bases Salsah backend in order to use the
     * IIIF compatible image server there too.
     */
    class Salsah {
    public:
        typedef enum {
            ADMIN_PROPERTIES = 1,     // may add/modify new properties within vocabularies belonging to the project
            ADMIN_RESOURCE_TYPES = 2, // may add/modify new resource_types within vocabularies belonging to the project
            ADMIN_RIGHTS = 4,         // ???
            ADMIN_PERSONS = 8,        // may add/modify persons within the project
            ADMIN_ADD_RESOURCE = 256, // may add/upload a new resource belonging to project or to system (project_id = 0)
            ADMIN_ROOT = 65536        // = 2^16
        } AdminRights;
        typedef enum {
            RESOURCE_ACCESS_NONE = 0,               // Resource is not visible
            RESOURCE_ACCESS_VIEW_RESTRICTED = 1,    // Resource is viewable with restricted rights (e.g. watermark)
            RESOURCE_ACCESS_VIEW = 2,               // Resource is viewable, potentially with properties
            RESOURCE_ACCESS_ANNOTATE = 3,           // User may add annotation properties/values
            RESOURCE_ACCESS_EXTEND = 4,             // User may add a new value to properties which allow it according to the
            RESOURCE_ACCESS_OVERRIDE = 5,           // User may break the rules and add non-standard properties
            RESOURCE_ACCESS_MODIFY = 6,             // User may modify the resource, it's location and all it's associated
            RESOURCE_ACCESS_DELETE = 7,             // User may delete the resource and it's associated properties
            RESOURCE_ACCESS_RIGHTS = 8              // User may change the access rights
        } ResourceRights;
        typedef enum {
            GROUP_WORLD = 1, GROUP_USER = 2, GROUP_MEMBER = 3, GROUP_OWNER = 4
        } DefaultGroups;

    private:
        std::string filepath;
        int nx;
        int ny;
        int user_id;
        int active_project;
        int lang_id;
        ResourceRights rights;

        Salsah::ResourceRights
        salsah_get_resource_and_rights(int res_id, const std::string &quality, int user_id, int project_id);

    public:
        inline Salsah() {
            nx = ny = user_id = active_project = lang_id = -1;
            rights = RESOURCE_ACCESS_NONE;
        }

        Salsah(shttps::Connection *conobj, const std::string &res_id_str);

        inline Salsah::ResourceRights getRights() { return rights; }

        inline std::string getFilepath() { return filepath; }

        inline int getNx() { return nx; }

        inline int getNy() { return ny; }
    };

}

#endif
