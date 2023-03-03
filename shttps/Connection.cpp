/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 *//*!
 * \file Connection.cpp
 */
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>
#include <new>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>      // Needed for memset

#include <netinet/in.h>
#include <arpa/inet.h> //inet_addrmktemp
#include <unistd.h>    //write
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#include "Global.h"
#include "Error.h"
#include "Connection.h"
#include "ChunkReader.h"
#include "makeunique.h"
#include "Server.h" // TEMPORARY !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

static const char __file__[] = __FILE__;

using namespace std;

namespace shttps {

    const size_t max_headerline_len = 65535;

    // trim from start (in place)
    static inline void ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !std::isspace(ch);
        }));
    }

    // trim from end (in place)
    static inline void rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }

    // trim from both ends (in place)
    static inline void trim(std::string &s) {
        ltrim(s);
        rtrim(s);
    }

    // trim from start (copying)
    static inline std::string ltrim_copy(std::string s) {
        ltrim(s);
        return s;
    }

    // trim from end (copying)
    static inline std::string rtrim_copy(std::string s) {
        rtrim(s);
        return s;
    }

    // trim from both ends (copying)
    static inline std::string trim_copy(std::string s) {
        trim(s);
        return s;
    }

    pair<string, string> strsplit(const string &str, const char c) {
        size_t pos;
        if ((pos = str.find(c)) != string::npos) {
            string s1 = str.substr(0, pos);
            string s2 = str.substr(pos + 1);
            return make_pair(s1, s2);
        } else {
            string emptystr;
            return make_pair(str, emptystr);
        }
    }
    //=========================================================================

    unordered_map<string, string> parse_header_options(const string &options, bool form_encoded, char sep) {
        vector<string> params;
        size_t pos = 0;
        size_t old_pos = 0;
        while ((pos = options.find(sep, pos)) != string::npos) {
            pos++;
            if (pos == 1) { // if first char is a token skip it!
                old_pos = pos;
                continue;
            }
            params.push_back(options.substr(old_pos, pos - old_pos - 1));
            old_pos = pos;
        }
        if (old_pos != options.length()) {
            params.push_back(options.substr(old_pos, string::npos));
        }
        unordered_map<string, string> q;
        string name;
        string value;
        for (auto it = params.begin(); it != params.end(); it++) {
            if ((pos = it->find('=')) != string::npos) {
                name = urldecode(it->substr(0, pos), form_encoded);
                value = urldecode(it->substr(pos + 1, string::npos), form_encoded);
            } else {
                name = urldecode(*it, form_encoded);
            }
            trim(name);
            trim(value);
            asciitolower(name);
            q[name] = value;
        }
        return q;
    }
    //=========================================================================


    size_t safeGetline(std::istream &is, std::string &t, size_t max_n) {
        t.clear();

        size_t n = 0;
        for (;;) {
            int c;
            c = is.get();
            switch (c) {
                case '\n':
                    n++;
                    return n;
                case '\r':
                    n++;
                    if (is.peek() == '\n') {
                        is.get();
                        n++;
                    }
                    return n;
                case EOF:
                    return n;
                default:
                    n++;
                    t += (char) c;
            }
            if ((max_n > 0) && (n >= max_n)) {
                throw Error(__file__, __LINE__, "Input line too long!");
            }
        }
    }
    //=========================================================================


    std::string urldecode(const std::string &src, bool form_encoded) {

#define HEXTOI(x) (isdigit(x) ? x - '0' : x - 'W')

        stringstream outss;
        size_t start = 0;
        size_t pos;

        while ((pos = src.find('%', start)) != string::npos) {
            if ((pos + 2) < src.length()) {
                if (isxdigit(src[pos + 1]) && isxdigit(src[pos + 2])) {
                    string tmpstr = src.substr(start, pos - start);

                    if (form_encoded) {
                        for (int i = 0; i < tmpstr.length(); i++) {
                            if (tmpstr[i] == '+') tmpstr[i] = ' ';
                        }
                    }

                    outss << tmpstr;

                    //
                    // we have a valid hex number
                    //
                    char a = (char) tolower(src[pos + 1]);
                    char b = (char) tolower(src[pos + 2]);
                    char c = ((HEXTOI(a) << 4) | HEXTOI(b));
                    outss << c;
                    start = pos + 3;
                } else {
                    pos += 3;
                    string tmpstr = src.substr(start, pos - start);
                    if (form_encoded) {
                        for (int i = 0; i < tmpstr.length(); i++) {
                            if (tmpstr[i] == '+') tmpstr[i] = ' ';
                        }
                    }
                    outss << tmpstr;
                    start = pos;
                }
            }
        }

        outss << src.substr(start, src.length() - start);
        return outss.str();
    }
    //=========================================================================

    string urlencode(const string &value) {
        ostringstream escaped;
        escaped.fill('0');
        escaped << hex;

        for (string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
            string::value_type c = (*i);

            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
                continue;
            }

            // Any other characters are percent-encoded
            escaped << uppercase;
            escaped << '%' << setw(2) << int((unsigned char) c);
            escaped << nouppercase;
        }

        return escaped.str();
    }
    //=========================================================================

    static unordered_map<string, string> parse_query_string(const string &query, bool form_encoded = false) {
        vector<string> params;
        size_t pos = 0;
        size_t old_pos = 0;

        while ((pos = query.find('&', pos)) != string::npos) {
            pos++;

            if (pos == 1) { // if first char is a token skip it!
                old_pos = pos;
                continue;
            }

            params.push_back(query.substr(old_pos, pos - old_pos - 1));

            old_pos = pos;
        }

        if (old_pos != query.length()) {
            params.push_back(query.substr(old_pos, string::npos));
        }

        unordered_map<string, string> q;

        for (auto it = params.begin(); it != params.end(); it++) {
            string name;
            string value;

            if ((pos = it->find('=')) != string::npos) {
                name = urldecode(it->substr(0, pos), form_encoded);
                value = urldecode(it->substr(pos + 1, string::npos), form_encoded);
            } else {
                name = urldecode(*it, form_encoded);
            }

            //asciitolower(name);
            q[name] = value;
        }

        return q;
    }
    //=========================================================================

    void Connection::process_header() {
        //
        // process header files
        //
        bool eoh = false; //end of header reached
        string line;

        while (!eoh && !ins->eof() && !ins->fail()) {
            (void) safeGetline(*ins, line, max_headerline_len);

            if (line.empty() || ins->fail()) {
                eoh = true;
            } else {
                size_t pos = line.find(':');
                string name = line.substr(0, pos);
                name = trim_copy(name);
                asciitolower(name);
                string value = line.substr(pos + 1);
                value = header_in[name] = trim_copy(value);

                if (name == "connection") {
                    unordered_map<string, string> opts = parse_header_options(value, true);
                    _keep_alive = true;
                    //_keep_alive = opts.count("keep-alive") == 1;
                    _keep_alive = opts.count("close") != 1;
                    if (opts.count("upgrade") == 1) {
                        // upgrade connection, e.g. to websockets...
                    }
                } else if (name == "cookie") {
                    _cookies = parse_header_options(value, true);
                } else if (name == "keep-alive") {
                    unordered_map<string, string> opts = parse_header_options(value, true, ',');
                    if (opts.count("timeout") == 1) {
                        _keep_alive_timeout = stoi(opts["timeout"]);
                    }
                } else if (name == "content-length") {
                    content_length = static_cast<size_t>(stoll(value));
                } else if (name == "transfer-encoding") {
                    if (value == "chunked") {
                        _chunked_transfer_in = true;
                    }
                } else if (name == "host") {
                    _host = value;
                }
            }
        }
    }
    //=============================================================================

    vector<string> Connection::process_header_value(const string &valstr) {
        vector<string> result;
        size_t start = 0;
        size_t pos = 0;
        while ((pos = valstr.find(';', start)) != string::npos) {
            string tmpstr = valstr.substr(start, pos - start);
            result.push_back(trim_copy(tmpstr));
            start = pos + 1;
        }
        string tmpstr = valstr.substr(start);
        result.push_back(trim_copy(tmpstr));

        return result;
    }
    //=============================================================================


    Connection::Connection(void) {
        _server = nullptr;
        _secure = false;
        ins = nullptr;
        os = nullptr;
        cachefile = nullptr;
        outbuf_size = 0;
        outbuf_inc = 0;
        outbuf = nullptr;
        header_sent = false;
        _keep_alive = false;  // should be true as this is the default for HTTP/1.1, but ab makes a porblem
        _keep_alive_timeout = -1;
        _chunked_transfer_in = false;
        _chunked_transfer_out = false;
        _content = nullptr;
        content_length = 0;
        _finished = false;
        _reset_connection = false;
    }
    //=========================================================================


    Connection::Connection(Server *server_p, std::istream *ins_p, std::ostream *os_p, const string &tmpdir_p,
                           size_t buf_size, size_t buf_inc) : ins(ins_p), os(os_p), _tmpdir(tmpdir_p),
                                                              outbuf_size(buf_size), outbuf_inc(buf_inc) {
        _server = server_p;
        _secure = false;
        cachefile = nullptr;
        header_sent = false;
        _keep_alive = false; // should be true as this is the default for HTTP/1.1, but ab makes a porblem
        _keep_alive_timeout = -1;
        _chunked_transfer_in = false;
        _chunked_transfer_out = false;
        _content = nullptr;
        content_length = 0;
        _finished = false;
        _reset_connection = false;

        status(OK); // thats the default...

        if (outbuf_size > 0) {
            if ((outbuf = (char *) malloc(outbuf_size)) == nullptr) {
                throw Error(__file__, __LINE__, "malloc failed!", errno);
            }

            outbuf_nbytes = 0;
        } else {
            outbuf = nullptr;
        }

        string line;
        if ((safeGetline(*ins, line, max_headerline_len) == 0) || line.empty() || ins->fail() || ins->eof()) {
            //
            // we got either a timeout or a socket close (for shutdown of server)
            //
            throw INPUT_READ_FAIL;
        }
        //
        // Parse first line of request
        //
        string method_in;
        string fulluri;
        stringstream lineparse(line);
        lineparse >> method_in >> fulluri >> http_version;

        if (!lineparse.fail()) {
            size_t pos;

            if ((pos = fulluri.find('?')) != string::npos) {
                _uri = fulluri.substr(0, pos);
                string querystr = fulluri.substr(pos + 1, string::npos);
                get_params = parse_query_string(querystr);
                request_params = get_params;
            } else {
                _uri = fulluri;
            }

            process_header();

            if (ins->fail() || ins->eof()) {
                throw INPUT_READ_FAIL;
            }

            //
            // check if we have a CORS request and add the appropriate headers
            //
            string origin;
            if (header_in.count("origin") == 1) {
                origin = header_in["origin"];
                header_out["Access-Control-Allow-Origin"] = origin;
                header_out["Access-Control-Allow-Credentials"] = "true";
            }

            if (method_in == "OPTIONS") {
                //
                // test for CORS preflight request and process it here...
                //

                string method;
                string xreq;
                if (header_in.count("access-control-request-method") == 1) {
                    method = header_in["access-control-request-method"];
                }
                if (header_in.count("access-control-request-headers") >= 1) {
                    xreq = header_in["access-control-request-headers"];
                }

                if (!((origin.empty()) || (method.empty()) || (xreq.empty()))) {
                    _method = OPTIONS;

                    header_out["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE";
                    //header_out["Access-Control-Expose-Headers"] =  "FooBar"; // aditional headers allowed...
                    header_out["Access-Control-Allow-Headers"] = xreq;
                    header_out["Content-Type"] = "text/plain";

                    flush();

                    _reset_connection = true; // handled in Server.cpp

                } else {
                    _method = OPTIONS;
                }
            } else if (method_in == "GET") {
                _method = GET;
            } else if (method_in == "HEAD") {
                _method = HEAD;
            } else if ((method_in == "POST") || (method_in == "PUT")) {
                if (method_in == "POST") {
                    _method = POST;
                } else {
                    _method = PUT;
                }

                vector<string> content_type_opts = process_header_value(header_in["content-type"]);
                if (content_type_opts[0] == "application/x-www-form-urlencoded") {
                    char *bodybuf = nullptr;

                    if (_chunked_transfer_in) {
                        char *chunk_buf;
                        ChunkReader ckrd(ins, _server->max_post_size());
                        content_length = ckrd.readAll(&chunk_buf);

                        if ((bodybuf = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                            free(chunk_buf);
                            throw Error(__file__, __LINE__, "malloc failed!", errno);
                        }

                        memcpy(bodybuf, chunk_buf, content_length);
                        free(chunk_buf);
                    } else {
                        if (content_length > 0) {
                            if ((_server->max_post_size() > 0) && (content_length > _server->max_post_size())) {
                                throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                            }

                            if ((bodybuf = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                                throw Error(__file__, __LINE__, "malloc failed!", errno);
                            }

                            ins->read(bodybuf, content_length);

                            if (ins->fail() || ins->eof()) {
                                free(bodybuf);
                                bodybuf = nullptr;
                                throw INPUT_READ_FAIL;
                            }
                        }
                    }
                    if (bodybuf != nullptr) {
                        bodybuf[content_length] = '\0';
                        string body = bodybuf;
                        post_params = parse_query_string(body, true);
                        request_params.insert(post_params.begin(), post_params.end());
                        free(bodybuf);
                    }
                    content_length = 0;
                } else if (content_type_opts[0] == "multipart/form-data") {
                    if ((!_chunked_transfer_in) && (_server->max_post_size() > 0) &&
                        (content_length > _server->max_post_size())) {
                        throw Error(__file__, __LINE__, "Upload bigger than max_post_size");
                    }

                    string boundary;
                    for (int i = 1; i < content_type_opts.size(); ++i) {
                        pair<string, string> p = strsplit(content_type_opts[i], '=');
                        if (p.first == "boundary") {
                            boundary = string("--") + p.second;
                        }
                    }

                    if (boundary.empty()) {
                        throw Error(__file__, __LINE__, "boundary header missing in multipart/form-data!");
                    }

                    string lastboundary = boundary + "--";
                    size_t n = 0;

                    //
                    // at this location we have determined the boundary string
                    //
                    ChunkReader ckrd(ins, _server->max_post_size()); // if we need it, we have it...

                    n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line, _server->max_post_size());

                    if (ins->fail() || ins->eof()) {
                        throw INPUT_READ_FAIL;
                    }

                    if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                        throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                    }

                    while (line != lastboundary) {
                        if (line == boundary) { // we have a boundary, thus we start a new field
                            string fieldname;
                            string fieldvalue;
                            string filename;
                            string tmpname;
                            string mimetype;
                            string encoding;
                            n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line,
                                                                                         _server->max_post_size() - n);
                            if (ins->fail() || ins->eof()) {
                                throw INPUT_READ_FAIL;
                            }

                            if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                            }

                            while (!line.empty()) {
                                size_t colon_pos = line.find(':');
                                string name = line.substr(0, colon_pos);
                                name = trim_copy(name);
                                asciitolower(name);
                                string value = line.substr(colon_pos + 1);
                                value = trim_copy(value);

                                if (name == "content-disposition") {
                                    unordered_map<string, string> opts = parse_header_options(value, true);
                                    unordered_map<string, string>::iterator it;

                                    //for (it = opts.begin(); it != opts.end(); it++) {
                                        //std::cerr << "OPTS: " << it->first << "=" << it->second << std::endl;
                                        //How do I access each element without knowing any of its string-int values?
                                    //}

                                    if (opts.count("form-data") == 0) {
                                        // something is wrong!
                                        //cerr << thisSourceFile << " #" << __LINE__ << endl;
                                        //cerr << "LINE=" << line << endl;
                                    }

                                    if (opts.count("name") == 0) {
                                        // something wrong
                                        //cerr << thisSourceFile << " #" << __LINE__ << endl;
                                        //cerr << "LINE=" << line << endl;
                                        //cerr << "VALUE=" << value << endl;
                                    }

                                    fieldname = opts["name"];
                                    if (fieldname[0] == '"' && fieldname[fieldname.size() - 1] == '"') {
                                        // filename is inside quotes, remove them
                                        fieldname = fieldname.substr(1, fieldname.size() - 2);
                                    }

                                    if (opts.count("filename") == 1) {
                                        // we have an upload of a file ...
                                        filename = opts["filename"];

                                        if (filename[0] == '"' && filename[filename.size() - 1] == '"') {
                                            // filename is inside quotes, remove them
                                            filename = filename.substr(1, filename.size() - 2);
                                        }
                                    }
                                } else if (name == "content-type") {
                                    mimetype = value;
                                } else if (name == "content-transfer-encoding") {
                                    encoding = value;
                                }

                                n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line,
                                                                                             _server->max_post_size() -
                                                                                             n);
                                if (ins->fail() || ins->eof()) {
                                    throw INPUT_READ_FAIL;
                                }
                                if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                    throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                                }
                            } // while

                            if (filename.empty()) {
                                // we read a normal value
                                n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line,
                                                                                             _server->max_post_size() -
                                                                                             n);
                                if (ins->fail() || ins->eof()) {
                                    throw INPUT_READ_FAIL;
                                }

                                if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                    throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                                }

                                while ((line != boundary) && (line != lastboundary)) {
                                    fieldvalue += line;
                                    n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line,
                                                                                                 _server->max_post_size() -
                                                                                                 n);
                                    if (ins->fail() || ins->eof()) {
                                        throw INPUT_READ_FAIL;
                                    }

                                    if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                        throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                                    }
                                }

                                post_params[fieldname] = fieldvalue;
                                continue;
                            } else {
                                int inbyte;
                                size_t cnt = 0;
                                size_t fsize = 0;

                                //
                                // create a unique temporary filename
                                //

                                if (_tmpdir.empty()) {
                                    throw Error(__file__, __LINE__, "_tmpdir is empty");
                                }

                                tmpname = _tmpdir + "/sipi_XXXXXXXX";

                                auto writable = make_unique<char[]>(tmpname.size() + 1);
                                std::copy(tmpname.begin(), tmpname.end(), writable.get());
                                (writable.get())[tmpname.size()] = '\0'; // don't forget the terminating 0
                                int fd = mkstemp(writable.get());

                                if (fd == -1) {
                                    throw Error(__file__, __LINE__, "Could not create temporary filename!");
                                }

                                tmpname = string(writable.get());
                                close(fd); // here we close the file created by mkstemp
                                ofstream outf(tmpname, ofstream::out | ofstream::trunc | ofstream::binary);

                                if (outf.fail()) {
                                    throw Error(__file__, __LINE__, "Could not open temporary file!");
                                }

                                //
                                // the boundary string starts on a new line which is separate by "\r\n"
                                //
                                string nlboundary = "\r\n" + boundary;

                                while ((inbyte = _chunked_transfer_in ? ckrd.getc() : ins->get()) != EOF) {
                                    if (ins->fail() || ins->eof()) {
                                        throw INPUT_READ_FAIL;
                                    }

                                    ++n;

                                    if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                        throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                                    }

                                    if ((cnt < nlboundary.length()) && (inbyte == nlboundary[cnt])) {
                                        ++cnt;
                                        if (cnt == nlboundary.length()) {
                                            // OK, we have read the whole file...
                                            break; // break enclosing while loop
                                        }
                                    } else if (cnt > 0) { // not yet the boundary
                                        for (int i = 0; i < cnt; i++) {
                                            outf.put(nlboundary[i]);
                                            if (!outf.good()) {
                                                throw Error(__file__, __LINE__, "Could not write to output file!");
                                            }
                                            ++fsize;
                                        }

                                        cnt = 0;
                                        if ((cnt < nlboundary.length()) && (inbyte == nlboundary[cnt])) {
                                            ++cnt;
                                        } else {
                                            outf.put((char) inbyte);
                                        }

                                        if (!outf.good()) {
                                            throw Error(__file__, __LINE__, "Could not write to output file!");
                                        }

                                        ++fsize;
                                    } else {
                                        outf.put((char) inbyte);

                                        if (!outf.good()) {
                                            throw Error(__file__, __LINE__, "Could not write to output file!");
                                        }

                                        ++fsize;
                                    }
                                }

                                outf.close();
                                UploadedFile uf = {fieldname, filename, tmpname, mimetype, fsize};
                                _uploads.push_back(uf);

                                //
                                // if it's an inside boundary, we read the terminating "\r\n", if it's
                                // the last boundary, we read the "--" at the end (the last boundary is
                                // *not* followed by a "\r\n"!)
                                //
                                inbyte = _chunked_transfer_in ? ckrd.getc() : ins->get(); // get '-' or '\r'

                                if (ins->fail() || ins->eof()) {
                                    throw INPUT_READ_FAIL;
                                }

                                ++n;

                                if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                                    throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                                }

                                if (inbyte == '-') { // we have a last boundary!
                                    inbyte = _chunked_transfer_in ? ckrd.getc() : ins->get(); // second '-'

                                    if (ins->fail() || ins->eof()) {
                                        throw INPUT_READ_FAIL;
                                    }

                                    line = lastboundary;
                                } else {
                                    inbyte = _chunked_transfer_in ? ckrd.getc() : ins->get(); // \n'

                                    if (ins->fail() || ins->eof()) {
                                        throw INPUT_READ_FAIL;
                                    }

                                    line = boundary;
                                }
                            }

                            continue; // break loop;
                        }

                        n += _chunked_transfer_in ? ckrd.getline(line) : safeGetline(*ins, line,
                                                                                     _server->max_post_size() - n);
                        if (ins->fail() || ins->eof()) {
                            throw INPUT_READ_FAIL;
                        }

                        if ((_server->max_post_size() > 0) && (n > _server->max_post_size())) {
                            throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                        }
                    }

                    content_length = 0;
                } else if ((content_type_opts[0] == "text/plain") || (content_type_opts[0] == "application/json") ||
                           (content_type_opts[0] == "application/ld+json") ||
                           (content_type_opts[0] == "application/xml")) {
                    _content_type = content_type_opts[0];

                    if (_chunked_transfer_in) {
                        char *tmp;
                        ChunkReader ckrd(ins, _server->max_post_size());
                        content_length = ckrd.readAll(&tmp);
                        if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                            throw Error(__file__, __LINE__, "malloc failed!", errno);
                        }
                        memcpy(_content, tmp, content_length);
                        free(tmp);
                        _content[content_length] = '\0';
                    } else if (content_length > 0) {
                        if ((_server->max_post_size() > 0) && (content_length > _server->max_post_size())) {
                            throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                        }
                        if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                            throw Error(__file__, __LINE__, "malloc failed!", errno);
                        }
                        ins->read(_content, content_length);
                        if (ins->fail() || ins->eof()) {
                            free(_content);
                            _content = nullptr;
                            throw INPUT_READ_FAIL;
                        }
                        _content[content_length] = '\0';
                    }
                } else {
                    throw Error(__file__, __LINE__, "Content type not supported!");
                }
            } else if (method_in == "DELETE") {
                vector<string> content_type_opts = process_header_value(header_in["content-type"]);
                _content_type = content_type_opts[0];
                if (_chunked_transfer_in) {
                    char *tmp;
                    ChunkReader ckrd(ins, _server->max_post_size());
                    content_length = ckrd.readAll(&tmp);
                    if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                        throw Error(__file__, __LINE__, "malloc failed!", errno);
                    }
                    memcpy(_content, tmp, content_length);
                    free(tmp);
                    _content[content_length] = '\0';
                } else if (content_length > 0) {
                    if ((_server->max_post_size() > 0) && (content_length > _server->max_post_size())) {
                        throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                    }
                    if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                        throw Error(__file__, __LINE__, "malloc failed!", errno);
                    }
                    ins->read(_content, content_length);

                    if (ins->fail() || ins->eof()) {
                        free(_content);
                        _content = nullptr;
                        throw INPUT_READ_FAIL;
                    }
                    _content[content_length] = '\0';
                }
                _method = DELETE;
            } else if (method_in == "TRACE") {
                vector<string> content_type_opts = process_header_value(header_in["content-type"]);
                _content_type = content_type_opts[0];
                if (_chunked_transfer_in) {
                    char *tmp;
                    ChunkReader ckrd(ins, _server->max_post_size());
                    content_length = ckrd.readAll(&tmp);
                    if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                        throw Error(__file__, __LINE__, "malloc failed!", errno);
                    }
                    memcpy(_content, tmp, content_length);
                    free(tmp);
                    _content[content_length] = '\0';
                } else if (content_length > 0) {
                    if ((_server->max_post_size() > 0) && (content_length > _server->max_post_size())) {
                        throw Error(__file__, __LINE__, "Content bigger than max_post_size");
                    }
                    if ((_content = (char *) malloc((content_length + 1) * sizeof(char))) == nullptr) {
                        throw Error(__file__, __LINE__, "malloc failed!", errno);
                    }
                    ins->read(_content, content_length);

                    if (ins->fail() || ins->eof()) {
                        free(_content);
                        _content = nullptr;
                        throw INPUT_READ_FAIL;
                    }
                }
                _method = TRACE;
            } else if (method_in == "CONNECT") {
                _method = CONNECT;
            } else {
                _method = OTHER;
            }
        } else {
            throw Error(__file__, __LINE__, "Invalid HTTP header!");
        }
    }
    //=============================================================================

    Connection::Connection(const Connection &conn) {
        throw Error(__file__, __LINE__, "Copy constructor not allowed!");
    }
    //=============================================================================

    Connection &Connection::operator=(const Connection &other) {
        throw Error(__file__, __LINE__, "Assignment operator not allowed!");
    }
    //=============================================================================

    Connection::~Connection() {
        try {
            finalize();
        } catch (InputFailure iofail) {
            // do nothing....
        }

        if (_content != nullptr) {
            free(_content);
        }

        if (outbuf != nullptr) {
            free(outbuf);
        }

        if (cachefile != nullptr) {
            delete cachefile;
            cachefile = nullptr;
        }
    }
    //=============================================================================

    int Connection::setupKeepAlive(int default_timeout) {
        if (_keep_alive) {
            header_out["Connection"] = "keep-alive";

            if (_keep_alive_timeout <= 0) {
                _keep_alive_timeout = default_timeout;
            }

            if (_keep_alive_timeout > 0) {
                header_out["Keep-Alive"] =
                        string("timeout=") + to_string(_keep_alive_timeout) + string(", max=") + to_string(100);
            }
        } else {
            header_out["Connection"] = "close";
            _keep_alive_timeout = 0;
        }
        return _keep_alive_timeout;
    }
    //=============================================================================


    void Connection::status(StatusCodes status_code_p, const string status_string_p) {
        status_code = status_code_p;
        if (status_string_p.empty()) {
            switch (status_code_p) {
                case CONTINUE:
                    status_string = "Continue";
                    break;
                case SWITCHING_PROTOCOLS:
                    status_string = "Switching protocols";
                    break;
                case PROCESSING:
                    status_string = "Processing";
                    break;
                case OK:
                    status_string = "OK";
                    break;
                case CREATED:
                    status_string = "Created";
                    break;
                case ACCEPTED:
                    status_string = "Accepted";
                    break;
                case NONAUTHORITATIVE_INFORMATION:
                    status_string = "Nonauthoritative information";
                    break;
                case NO_CONTENT:
                    status_string = "No content";
                    break;
                case RESET_CONTENT:
                    status_string = "Reset content";
                    break;
                case PARTIAL_CONTENT:
                    status_string = "Partial content";
                    break;
                case MULTI_STATUS:
                    status_string = "Multi status";
                    break;
                case ALREADY_REPORTED:
                    status_string = "";
                    break;
                case IM_USED:
                    status_string = "Already reported";
                    break;
                case MULTIPLE_CHOCIES:
                    status_string = "Multiple chocies";
                    break;
                case MOVED_PERMANENTLY:
                    status_string = "Moved permanently";
                    break;
                case FOUND:
                    status_string = "Found";
                    break;
                case SEE_OTHER:
                    status_string = "See other";
                    break;
                case NOT_MODIFIED:
                    status_string = "Not modified";
                    break;
                case USE_PROXY:
                    status_string = "Use proxy";
                    break;
                case SWITCH_PROXY:
                    status_string = "Switch proxy";
                    break;
                case TEMPORARY_REDIRECT:
                    status_string = "Temporary redirect";
                    break;
                case PERMANENT_REDIRECT:
                    status_string = "Permanent redirect";
                    break;
                case BAD_REQUEST:
                    status_string = "Bad request";
                    break;
                case UNAUTHORIZED:
                    status_string = "Unauthorized";
                    break;
                case PAYMENT_REQUIRED:
                    status_string = "Payment required";
                    break;
                case FORBIDDEN:
                    status_string = "Forbidden";
                    break;
                case NOT_FOUND:
                    status_string = "Not found";
                    break;
                case METHOD_NOT_ALLOWED:
                    status_string = "Method not allowed";
                    break;
                case NOT_ACCEPTABLE:
                    status_string = "Not acceptable";
                    break;
                case PROXY_AUTHENTIFICATION_REQUIRED:
                    status_string = "Proxy authentification required";
                    break;
                case REQUEST_TIMEOUT:
                    status_string = "Request timeout";
                    break;
                case CONFLICT:
                    status_string = "Conflict";
                    break;
                case GONE:
                    status_string = "Gone";
                    break;
                case LENGTH_REQUIRED:
                    status_string = "Length required";
                    break;
                case PRECONDITION_FAILED:
                    status_string = "Precondition failed";
                    break;
                case PAYLOAD_TOO_LARGE:
                    status_string = "Payload too large";
                    break;
                case REQUEST_URI_TOO_LONG:
                    status_string = "Request uri too long";
                    break;
                case UNSUPPORTED_MEDIA_TYPE:
                    status_string = "Unsupported_media_type";
                    break;
                case REQUEST_RANGE_NOT_SATISFIABLE:
                    status_string = "Request range not satisfiable";
                    break;
                case EXPECTATION_FAILED:
                    status_string = "Expectation failed";
                    break;
                case I_AM_A_TEAPOT:
                    status_string = "I am a teapot";
                    break;
                case AUTHENTIFICATION_TIMEOUT:
                    status_string = "Authentification timeout";
                    break;
                case METHOD_FAILURE:
                    status_string = "Method failure";
                    break;
                case TOO_MANY_REQUESTS:
                    status_string = "Too many requests";
                    break;
                case UNAVAILABLE_FOR_LEGAL_REASONS:
                    status_string = "Unavailable for legal reasons";
                    break;
                case INTERNAL_SERVER_ERROR:
                    status_string = "Internal server error";
                    break;
                case NOT_IMPLEMENTED:
                    status_string = "Not implemented";
                    break;
                case BAD_GATEWAY:
                    status_string = "Bad gateway";
                    break;
                case SERVICE_UNAVAILABLE:
                    status_string = "Service unavailable";
                    break;
                case GATEWAY_TIMEOUT:
                    status_string = "Gateway timeout";
                    break;
                case HTTP_VERSION_NOT_SUPPORTED:
                    status_string = "Http version not supported";
                    break;
                case UNKOWN_ERROR:
                    status_string = "Unkown error";
                    break;
            }
        }
        else {
            status_string = status_string_p;
        }
    }
    //=============================================================================

    string Connection::getParams(const std::string &name) {
        string result;

        if (get_params.count(name) == 1) {
            result = get_params[name];
        }

        return result;
    }
    //=============================================================================

    vector<string> Connection::getParams(void) {
        vector<string> names;

        for (auto const &iterator : get_params) {
            names.push_back(iterator.first);
        }

        return names;
    }
    //=============================================================================

    string Connection::postParams(const std::string &name) {
        string result;

        if (post_params.count(name) == 1) {
            result = post_params[name];
        }

        return result;
    }
    //=============================================================================

    vector<string> Connection::postParams(void) {
        vector<string> names;

        for (auto const &iterator : post_params) {
            names.push_back(iterator.first);
        }

        return names;
    }
    //=============================================================================

    string Connection::requestParams(const std::string &name) {
        string result;

        if (request_params.count(name) == 1) {
            result = request_params[name];
        }
        return result;
    }
    //=============================================================================

    vector<string> Connection::requestParams(void) {
        vector<string> names;

        for (auto const &iterator : request_params) {
            names.push_back(iterator.first);
        }

        return names;
    }
    //=============================================================================

    void Connection::setBuffer(size_t buf_size, size_t buf_inc) {
        if (header_sent) {
            throw Error(__file__, __LINE__, "Header already sent - cannot changed to buffered mode!");
        }

        if (outbuf == nullptr) {
            if ((outbuf = (char *) malloc(outbuf_size)) == nullptr) {
                throw Error(__file__, __LINE__, "malloc failed!", errno);
            }

            outbuf_nbytes = 0;
        }
    }
    //=============================================================================


    void Connection::setChunkedTransfer() {
        if (header_sent) {
            throw Error(__file__, __LINE__, "Header already sent!");
        }

        header_out["Transfer-Encoding"] = "chunked";
        _chunked_transfer_out = true;
    }
    //=============================================================================

    void Connection::openCacheFile(const std::string &cfname) {
        cachefile = new ofstream(cfname, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);

        if (cachefile->fail()) {
            throw Error(__file__, __LINE__, "Could not open cache file!");
        }
    }
    //=============================================================================

    void Connection::closeCacheFile(void) {
        if (cachefile != nullptr) {
            cachefile->close();
            delete cachefile;
            cachefile = nullptr;
        }
    }
    //=============================================================================

    vector<string> Connection::header(void) {
        vector<string> names;

        for (auto const &iterator : header_in) {
            names.push_back(iterator.first);
        }

        return names;
    }
    //=============================================================================


    void Connection::header(string name, string value) {
        if (header_sent) {
            throw Error(__file__, __LINE__, "Header already sent!");
        }

        header_out[name] = value;
    }
    //=============================================================================


    void Connection::cookies(const Cookie &cookie_p) {
        string str = cookie_p.name() + "=" + cookie_p.value();
        if (!cookie_p.path().empty()) str += "; Path=" + cookie_p.path();
        if (!cookie_p.domain().empty()) str += "; Domain=" + cookie_p.domain();
        if (!cookie_p.expires().empty()) str += "; Expires=" + cookie_p.expires();
        if (cookie_p.secure()) str += "; Secure";
        if (cookie_p.httpOnly()) str += "; HttpOnly";
        header("Set-Cookie", str);
    }
    //=============================================================================


    void Connection::corsHeader(const char *origin) {
        if (origin == nullptr) return;
        header_out["Access-Control-Allow-Origin"] = origin;
        header_out["Access-Control-Allow-Credentials"] = "true";
        //header_out["Access-Control-Expose-Headers"] = "FooBar";
    }
    //=========================================================================


    void Connection::corsHeader(const std::string &origin) {
        if (origin.empty()) return;
        header_out["Access-Control-Allow-Origin"] = origin;
        header_out["Access-Control-Allow-Credentials"] = "true";
        //header_out["Access-Control-Expose-Headers"] = "FooBar";
    }
    //=========================================================================


    void Connection::sendData(const void *buffer, size_t n) {
        if (!header_sent) {
            send_header(); // sends content length if not buffer nor chunked
        }

        os->write((char *) buffer, n);
        if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
        if (cachefile != nullptr) cachefile->write((char *) buffer, n);
    }
    //=============================================================================


    void Connection::send(const void *buffer, size_t n) {
        if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");

        if (outbuf != nullptr) {
            //
            // we have a buffer -> we add the data to the buffer
            //
            add_to_outbuf((char *) buffer, n);
        } else {
            //
            // we have no buffer, so an immediate action is required
            //
            if (!header_sent) { // we have not yet sent a header -> do it
                if (_chunked_transfer_out) {
                    //
                    // chunked transfer -> send header and chunk
                    //
                    send_header(); // sends content length if not buffer nor chunked
                    *os << std::hex << n << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    os->write((char *) buffer, n);
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    if (cachefile != nullptr) cachefile->write((char *) buffer, n);
                    *os << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    os->flush();
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                } else {
                    //
                    // normal (unchunked) transfer -> send header with length of data and then send the data
                    //
                    send_header(n); // sends content length if not buffer nor chunked
                    os->write((char *) buffer, n);
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    if (cachefile != nullptr) cachefile->write((char *) buffer, n);
                    os->flush();
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    _finished = true; // no more data can be sent
                }
            } else { // the header has already been sent
                if (_chunked_transfer_out) {
                    //
                    // chunked transfer -> send the chunk
                    //
                    *os << std::hex << n << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    os->write((char *) buffer, n);
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    if (cachefile != nullptr) cachefile->write((char *) buffer, n);
                    *os << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    os->flush();
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                } else {
                    //
                    // houston, we have a problem. The header is already sent...
                    //
                    throw Error(__file__, __LINE__, "Header already sent – cannot add data anymore!");
                }
            }
        }
    }
    //=============================================================================


    void Connection::sendAndFlush(const void *buffer, size_t n) {
        if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");

        if (outbuf != nullptr) {
            //
            // we have a buffer -> we add the data to the buffer
            //
            add_to_outbuf((char *) buffer, n);
        }

        if (_chunked_transfer_out) {
            //
            // we have chunked transfer, we don't want the content length header
            //
            if (!header_sent) {
                send_header(); // sends content length if nor buffer nor chunked
            }

            if (outbuf != nullptr) {
                //
                // we use the buffer -> send buffer as chunk
                //
                *os << std::hex << outbuf_nbytes << "\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                os->write((char *) outbuf, outbuf_nbytes);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) outbuf, outbuf_nbytes);
                outbuf_nbytes = 0;
            } else {
                //
                // we have no buffer, send the data provided as parameters
                //
                *os << std::hex << n << "\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                os->write((char *) buffer, n);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) buffer, n);
            }

            *os << "\r\n";
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            os->flush();
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
        } else {
            //
            // we don't use chunks, so we *need* the Content-Length header!
            //
            if ((outbuf != nullptr) && (outbuf_nbytes > 0)) {
                //
                // we use the buffer, the header *must* contain the Content-Length header!
                // then we send the data in the buffer
                //
                if (header_sent) {
                    throw Error(__file__, __LINE__, "Header already sent – cannot add Content-Length header!");
                } else {
                    send_header(); // sends content length if not buffer nor chunked
                }
                os->write((char *) buffer, n);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) buffer, n);
                outbuf_nbytes = 0;
            } else {
                //
                // we don't use a buffer (or the buffer is empty) -> send the data immediatly
                //
                if (header_sent) {
                    throw Error(__file__, __LINE__, "Header already sent – cannot add Content-Length header!");
                } else {
                    send_header(n); // sends content length if not buffer nor chunked
                }
                os->write((char *) buffer, n);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) buffer, n);
            }

            os->flush();
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            _finished = true; // no more data can be sent!
        }
    }
    //=============================================================================


    void Connection::sendFile(const string &path, const size_t bufsize, size_t from, size_t to) {
        if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");

        //
        // test if we have access to the file
        //
        if (access(path.c_str(), R_OK) != 0) { // test, if file exists
            throw Error(__file__, __LINE__, "File not readable:" + path);
        }

        struct stat fstatbuf{};

        if (stat(path.c_str(), &fstatbuf) != 0) {
            throw Error(__file__, __LINE__, "Cannot fstat file: " + path);
        }

        size_t fsize = fstatbuf.st_size;
        size_t orig_fsize = fsize;

        FILE *infile = fopen(path.c_str(), "rb");

        if (infile == nullptr) {
            throw Error(__file__, __LINE__, "File not readable: " + path);
        }

        if (from > 0) {
            if (from < fsize) {
                if (fseek(infile, from, SEEK_SET) < 0) {
                    fclose(infile);
                    throw Error(__file__, __LINE__, "Cannot seek to position!");
                }
                fsize -= from;
            } else {
                fclose(infile);
                throw Error(__file__, __LINE__, "Seek position beyond end of file!");
            }
        }

        if (to > 0) {
            if (to > orig_fsize) {
                fclose(infile);
                throw Error(__file__, __LINE__, "Trying to read beyond end of file!");
            }
            fsize -= (orig_fsize - to - 1);
        }

        if (outbuf != nullptr) {
            char buf[bufsize];
            size_t n = 0;
            size_t nn = 0;
            while ((nn < fsize) && ((n = fread(buf, sizeof(char), fsize - nn > bufsize ? bufsize : fsize - nn, infile)) > 0)) {
                add_to_outbuf(buf, n);
                nn += n;
            }
        } else {
            if (!header_sent) {
                if (_chunked_transfer_out) {
                    send_header();
                } else {
                    send_header(fsize);
                }
            }

            char buf[bufsize];
            size_t n;
            size_t nn = 0;

            while ((nn < fsize) && ((n = fread(buf, sizeof(char), fsize - nn > bufsize ? bufsize : fsize - nn, infile)) > 0)) {
                // send data here...
                if (_chunked_transfer_out) {
                    *os << std::hex << n << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    os->write(buf, n);
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                    *os << "\r\n";
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                } else {
                    os->write(buf, n);
                    if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                }

                os->flush();
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                nn += n;
            }
            if (!feof(infile)) {
                fclose(infile);
                return;
                // send Error here
            }
            _finished = true; // no more data can be sent!
        }

        fclose(infile);
    }
    //=============================================================================


    void Connection::flush(void) {
        if (!header_sent) {
            if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");
            send_header(); // sends content length if not buffer nor chunked
        }

        if ((outbuf != nullptr) && (outbuf_nbytes > 0)) {
            if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");
            if (_chunked_transfer_out) {
                *os << std::hex << outbuf_nbytes << "\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                os->write((char *) outbuf, outbuf_nbytes);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) outbuf, outbuf_nbytes);
                *os << "\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                os->flush();
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            } else {
                os->write((char *) outbuf, outbuf_nbytes);
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                if (cachefile != nullptr) cachefile->write((char *) outbuf, outbuf_nbytes);
                os->flush();
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
                _finished = true;
            }

            outbuf_nbytes = 0;
        } else {
            os->flush();
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;

            if (!_chunked_transfer_out) {
                _finished = true;
            }
        }
    }
    //=============================================================================


    Connection &Connection::operator<<(const std::string &str) {
        send((void *) str.c_str(), str.length());
        return *this;
    }
    //=============================================================================


    Connection &Connection::operator<<(Connection::Commands cmd) {
        flush();
        return *this;
    }
    //=============================================================================


    Connection &Connection::operator<<(const Error &err) {
        stringstream outss;
        outss << err;
        string tmpstr = outss.str();
        send((void *) tmpstr.c_str(), tmpstr.length());
        return *this;
    }
    //=============================================================================


    void Connection::add_to_outbuf(char *buf, size_t n) {
        if (_finished) throw Error(__file__, __LINE__, "Sending data already terminated!");

        if (outbuf_nbytes + n > outbuf_size) {
            size_t incsize = outbuf_size + ((n + outbuf_inc - 1) / outbuf_inc) * outbuf_inc;
            char *tmpbuf;
            if ((tmpbuf = (char *) realloc(outbuf, incsize)) == nullptr) {
                throw Error(__file__, __LINE__, "realloc failed!", errno);
            }
            outbuf = tmpbuf;
            outbuf_size = incsize;
        }

        memcpy(outbuf + outbuf_nbytes, buf, n);
        outbuf_nbytes += n;
    }
    //=============================================================================

    void Connection::send_header(size_t n) {
        if (header_sent) {
            throw Error(__file__, __LINE__, "Header already sent!");
        }

        *os << "HTTP/1.1 " << to_string(status_code) << " " << status_string << "\r\n";
        if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;

        for (auto const &iterator : header_out) {
            *os << iterator.first << ": " << iterator.second << "\r\n";
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
        }

        if (_chunked_transfer_out) { // no content length, please!!!
            *os << "\r\n"; //we have to add only one more "\r\n" in this case
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
        } else {
            if ((outbuf != nullptr) && (outbuf_nbytes > 0)) {
                *os << "Content-Length: " << outbuf_nbytes << "\r\n\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            } else if (n > 0) {
                *os << "Content-Length: " << n << "\r\n\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            } else {
                *os << "Content-Length: " << n << "\r\n\r\n";
                //*os << "\r\n";
                if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            }
        }

        os->flush();
        if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;

        header_sent = true;
    }
    //=============================================================================


    void Connection::finalize() {
        if (_chunked_transfer_out && !_finished) {
            *os << "0\r\n\r\n";
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
            os->flush(); // last (empty) chunk
            if (os->eof() || os->fail()) throw OUTPUT_WRITE_FAIL;
        }
        *os << "\r\n";
        os->flush();
        _finished = true;
    }
    //=============================================================================


    bool Connection::cleanupUploads(void) {
        bool filedelok = true;

        for (auto &i : _uploads) {
            if (remove(i.tmpname.c_str()) != 0) {
                filedelok = false;
                // send somewhere an error message...
            }
        }

        return filedelok;
    }
    //=============================================================================

}
