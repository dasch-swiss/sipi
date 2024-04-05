/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform contributors.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stdio.h>
#include <unistd.h>
#include <map>
#include <unordered_map>
#include <vector>
#include <tuple>

#include "SipiError.h"
#include "Template.h"
#include "PhpSession.h"
#include "Salsah.h"

#include <mariadb/mysql.h>

namespace Sipi {

typedef enum {MYSQL_STRING, MYSQL_INTEGER, MYSQL_FLOAT} MysqlDatatype;

static int salsah_query_mysql(MYSQL *mysql, const std::string &sqlstr, std::vector<std::map<std::string, std::string>> &res) {
    int n = 0;
    if (mysql_query (mysql, sqlstr.c_str()) == 0) {
        MYSQL_RES *result = mysql_store_result(mysql);
        MYSQL_ROW row;
        MYSQL_FIELD *fields;
        unsigned int nfields = mysql_num_fields(result);
        fields = mysql_fetch_fields(result); //fields[i].name
        if (mysql_num_rows(result) > 0) {
            while ((row = mysql_fetch_row (result)) != nullptr) {
                std::map<std::string, std::string> m;
                for (unsigned int i = 0; i < nfields; i++) {
                    m[std::string(fields[i].name)] = std::string(row[i] ? row[i] : "NULL");
                }
                res.push_back(m);
                n++;
            }
        }
    }
    return n;
}
    //=========================================================================


Salsah::ResourceRights Salsah::salsah_get_resource_and_rights(int res_id, const std::string &quality, int person_id, int project_id) {
    MYSQL mysql;
    std::map<std::string, std::string> res;
    int n;

    std::string query1 = R"(SELECT `location`.`protocol` AS protocol,
    `location`.`filename` AS filename,
    `location`.`origname` AS origname,
    `location`.`resource_id` AS res_id,
    `location`.`lastaccess` AS lastaccess,
    `location`.`nx`,
    `location`.`ny`,
    CONVERT_TZ(`location`.`lastaccess`, 'Europe/Zurich', 'UTC') AS lastaccess_utc,
    `qualityparam`.`name` AS prefix,
    `mimetype`.`name` AS mtype,
    `qualityparam_format`.`renderer` AS renderer,
    `project`.`basepath` AS basepath
    FROM `location`
    INNER JOIN `qualityparam` ON `location`.`qualityparam_id` = `qualityparam`.`id`
    INNER JOIN `qualityparam_format` ON `qualityparam`.`qualityparam_format_id` = `qualityparam_format`.`id`
    INNER JOIN `mimetype` ON `qualityparam_format`.`mimetype_id` = `mimetype`.`id`
    INNER JOIN `resource` ON `location`.`resource_id` = `resource`.`id`
    INNER JOIN `project` ON `resource`.`project_id` = `project`.`id`
    WHERE (`qualityparam`.`name` = '{{QNAME}}') AND (`location`.`resource_id` = {{RESID}})
    )";

    std::string query2 = R"(SELECT * FROM `resource_rights` WHERE `resource_id` = {{RESID}})";

    std::string query3 = R"(SELECT * FROM `resource` WHERE (`id` = {{RESID}}))";

    std::string query4 = R"(SELECT * FROM `person_in_project` WHERE (`person_id` = {{PERSONID}}) AND (`project_id` = {{PROJECTID}}))";

    std::string query5 = R"(SELECT * FROM `person_in_group` WHERE (`person_id` = {{PERSONID}}))";

    mysql_init(&mysql);
    if (!mysql_real_connect (&mysql, "localhost", "salsah", "imago", "salsah", 0, nullptr, 0)) {
        throw SipiError(__file__, __LINE__, mysql_error (&mysql));
    }


    Template templ1(query1);
    templ1.value(string("QNAME"), quality);
    templ1.value(string("RESID"), res_id);
    std::string sql1 = templ1.get();
    std::vector<std::map<std::string, std::string>> resarr1;
    n = salsah_query_mysql(&mysql, sql1, resarr1);
    if (n != 1) {
        throw SipiError(__file__, __LINE__, "Resource not existing or not unqiue! (n=" + std::to_string(n) + ")");
    }
    std::map<std::string, std::string> &res1 = resarr1[0];
    filepath = res1["basepath"] + "/" + quality + "/" + res1["filename"];
    cerr << "FILEPATH IS \"" << filepath << "\"" << endl;
    try {
        nx = stoi(res1["nx"]);
        ny = stoi(res1["ny"]);
    }
    catch (const std::invalid_argument& ia) {
        nx = ny = 0;
    }

    Template templ2(query2);
    templ2.value(string("RESID"), res_id);
    std::string sql2 = templ2.get();
    std::vector<std::map<std::string, std::string>> resarr2;
    n = salsah_query_mysql(&mysql, sql2, resarr2);
    std::unordered_map<int,int> resrights;
    try {
        for (int i = 0; i < n; i++) {
            resrights[stoi(resarr2[i]["group_id"])] = stoi(resarr2[i]["access_rights"]);
        }
    }
    catch (const std::invalid_argument& ia) {
        return RESOURCE_ACCESS_NONE;
    }

    Template templ3(query3);
    templ3.value(string("RESID"), res_id);
    std::string sql3 = templ3.get();
    std::vector<std::map<std::string, std::string>> resarr3;
    n = salsah_query_mysql(&mysql, sql3, resarr3);
    if (n != 1) {
        throw SipiError(__file__, __LINE__, "Resource not existing or not unqiue! (n=" + std::to_string(n) + ")");
    }
    int res_project_id = stoi(resarr3[0]["project_id"]);
    int res_person_id = stoi(resarr3[0]["person_id"]);

    if ((user_id >= 0) && project_id >= 0) {
        Template templ4(query4);
        templ4.value(std::string("PERSONID"), user_id);
        templ4.value(std::string("PROJECTID"), project_id);
        std::string sql4 = templ4.get();
        std::vector<std::map<std::string, std::string>> resarr4;
        n = salsah_query_mysql(&mysql, sql4, resarr4);
        if ((n == 1) && (stoi(resarr4[0]["admin_rights"]) >= ADMIN_ROOT)) {
            return RESOURCE_ACCESS_RIGHTS;
        }
    }

    std::vector<int> mygroups;
    if ((user_id >= 0) && (user_id == res_person_id)) {
        mygroups.push_back(GROUP_OWNER);
    }
    else if ((user_id >= 0) && (project_id == res_project_id)) {
        mygroups.push_back(GROUP_MEMBER);
    }
    else if (user_id >= 0) {
        mygroups.push_back(GROUP_USER);
    }
    else {
        mygroups.push_back(GROUP_WORLD);
    }

    Template templ5(query5);
    templ5.value(std::string("PERSONID"), user_id);
    std::string sql5 = templ5.get();
    std::vector<std::map<std::string, std::string>> resarr5;
    n = salsah_query_mysql(&mysql, sql5, resarr5);

    mysql_close(&mysql);

    for (int i = 0; i < n; i++) {
        mygroups.push_back(stoi(resarr2[i]["group_id"]));
    }
    ResourceRights rights = RESOURCE_ACCESS_NONE;
    for (std::vector<int>::iterator it = mygroups.begin() ; it != mygroups.end(); ++it) {
        if (resrights.count(*it) > 0) {
            if (resrights[*it] > rights) rights = ResourceRights(resrights[*it]);
        }
    }
    return rights;
}
//=========================================================================

Salsah::Salsah(shttps::Connection *conobj, const std::string &res_id_str) {
    user_id = -1;
    active_project = -1;
    // int lang_id = 0;
    rights = RESOURCE_ACCESS_NONE;

    std::string cookie = conobj->header("Cookie");
    if (!cookie.empty()) {
        size_t pos = cookie.find('=');
        std::string name = cookie.substr(0, pos);
        std::string value = cookie.substr(pos + 1, std::string::npos);
        if (name == "SALSAH") {
            std::string sessfile_name = "/var/tmp/sess_" + value;
            std::ifstream sessfile(sessfile_name);
            if (sessfile.fail()) {
                throw SipiError(__file__, __LINE__, "Cannot find session file at\"" + sessfile_name + "\"!");
            }
            try {
                PhpSession session(&sessfile);
                user_id = session.getUserId();
                active_project = session.getActiveProject();
                // lang_id = session.getLangId();
            }
            catch (SipiError &err) {
                cerr << err << endl; // print error message to stderr and logfiles
            }
        }
    }


    size_t pos;
    if ((pos = res_id_str.find(':')) == std::string::npos) {
        throw SipiError(__file__, __LINE__, "Resource-ID contains no quality name. Syntax: \"res_id:quality\" !");
    }
    std::string numeric_res_id_str = res_id_str.substr(0, pos);
    std::string quality = res_id_str.substr(pos + 1,  std::string::npos);
    int res_id;
    try {
        res_id = stoi(numeric_res_id_str);
    }
    catch (const std::invalid_argument& ia) {
        throw SipiError(__file__, __LINE__, std::string("Resource-ID if image not an integer:  \"") + ia.what() + std::string("\" !"));
    }

    rights = salsah_get_resource_and_rights(res_id, quality, user_id, active_project);
}

}
