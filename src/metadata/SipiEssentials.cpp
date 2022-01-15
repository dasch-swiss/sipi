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
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

#include "shttps/Global.h"
#include "shttps/Error.h"
#include "SipiEssentials.h"

static const char __file__[] = __FILE__;

static int calcDecodeLength(const std::string &b64input) {
    int padding = 0;

    // Check for trailing '=''s as padding
    if(b64input[b64input.length() - 1] == '=' && b64input[b64input.length() - 2] == '=')
        padding = 2;
    else if (b64input[b64input.length() - 1] == '=')
        padding = 1;

    return (int) b64input.length()*0.75 - padding;
}

std::string base64Encode(const std::vector<unsigned char> &message) {
    BIO *bio;
    BIO *b64;
    FILE* stream;

    size_t encodedSize = 4*ceil((double) message.size() / 3);

    char *buffer = (char *) malloc(encodedSize + 1);
    if(buffer == NULL) {
        throw shttps::Error(__file__, __LINE__, "Failed to allocate memory", errno);
    }

    stream = fmemopen(buffer, encodedSize + 1, "w");
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_fp(stream, BIO_NOCLOSE);
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, message.data(), message.size());
    (void) BIO_flush(bio);
    BIO_free_all(bio);
    fclose(stream);

    std::string res(buffer);
    free(buffer);
    return res;
}

std::vector<unsigned char> base64Decode(const std::string &b64message) {
    BIO *bio;
    BIO *b64;
    size_t decodedLength = calcDecodeLength(b64message);

    unsigned char *buffer = (unsigned char *) malloc(decodedLength + 1);
    if(buffer == NULL) {
        shttps::Error(__file__, __LINE__, "Failed to allocate memory", errno);
    }
    FILE* stream = fmemopen((char *) b64message.c_str(), b64message.size(), "r");

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_fp(stream, BIO_NOCLOSE);
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    decodedLength = BIO_read(bio, buffer, b64message.size());
    buffer[decodedLength] = '\0';

    BIO_free_all(bio);
    fclose(stream);

    std::vector<unsigned char> data;
    data.reserve(decodedLength);
    for (int i = 0; i < decodedLength; i++) data.push_back(buffer[i]);

    free(buffer);
    return data;
}

namespace Sipi {

    SipiEssentials::SipiEssentials(
            const std::string &origname_p,
            const std::string &mimetype_p,
            shttps::HashType hash_type_p,
            const std::string &data_chksum_p,
            const std::vector<unsigned char> &icc_profile_p) :
    _origname(origname_p),
    _mimetype(mimetype_p),
    _hash_type(hash_type_p),
    _data_chksum(data_chksum_p) {
        _is_set = true;
        _use_icc = false;
        if (!_icc_profile.empty()) {
            _icc_profile = base64Encode(icc_profile_p);
            _use_icc = true;
        }
    }

    std::string SipiEssentials::hash_type_string(void) const {
        std::string hash_type_str;
        switch (_hash_type) {
            case shttps::HashType::none:    hash_type_str = "none";   break;
            case shttps::HashType::md5:     hash_type_str = "md5";   break;
            case shttps::HashType::sha1:    hash_type_str = "sha1";   break;
            case shttps::HashType::sha256:   hash_type_str = "sha256";   break;
            case shttps::HashType::sha384:  hash_type_str = "sha384";   break;
            case shttps::HashType::sha512:  hash_type_str = "sha512";   break;
        }
        return hash_type_str;
    }

    void SipiEssentials::hash_type(const std::string &hash_type_p) {
        if (hash_type_p == "none")        _hash_type = shttps::HashType::none;
        else if (hash_type_p == "md5")    _hash_type = shttps::HashType::md5;
        else if (hash_type_p == "sha1")   _hash_type = shttps::HashType::sha1;
        else if (hash_type_p == "sha256") _hash_type = shttps::HashType::sha256;
        else if (hash_type_p == "sha384") _hash_type = shttps::HashType::sha384;
        else if (hash_type_p == "sha512") _hash_type = shttps::HashType::sha512;
        else _hash_type = shttps::HashType::none;
    }

    std::vector<unsigned char> SipiEssentials::icc_profile(void) {
        return base64Decode(_icc_profile);
    }

    unsigned char *SipiEssentials::icc_profile(unsigned int &len) {
        std::vector<unsigned char> tmp = base64Decode(_icc_profile);
        len = tmp.size();

        unsigned char *buf = new unsigned char[len];
        memcpy(buf, tmp.data(), len);
        return buf;
    }

    void SipiEssentials::icc_profile(const std::vector<unsigned char> &icc_profile_p) {
        _icc_profile = base64Encode(icc_profile_p);
    }

    // for string delimiter
    std::vector<std::string> split (std::string s, std::string delimiter) {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;

        while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
            token = s.substr (pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back (token);
        }

        res.push_back (s.substr (pos_start));
        return res;
    }

    void SipiEssentials::parse(const std::string &str) {
        std::vector<std::string> result = split(str, "|");
        _origname = result[0];
        _mimetype = result[1];
        std::string _hash_type_str = result[2];
        if (_hash_type_str == "none") _hash_type = shttps::HashType::none;
        else if (_hash_type_str == "md5") _hash_type = shttps::HashType::md5;
        else if (_hash_type_str == "sha1") _hash_type = shttps::HashType::sha1;
        else if (_hash_type_str == "sha256") _hash_type = shttps::HashType::sha256;
        else if (_hash_type_str == "sha384") _hash_type = shttps::HashType::sha384;
        else if (_hash_type_str == "sha512") _hash_type = shttps::HashType::sha512;
        else _hash_type = shttps::HashType::none;
        _data_chksum = result[3];

        if (result.size() > 5) {
            std::string tmp_use_icc = result[4];
            _use_icc = (tmp_use_icc == "USE_ICC");
            if (_use_icc) {
                _icc_profile = result[5];
            }
            else {
                _icc_profile = std::string();
            }
        }
        _is_set = true;
    }
}
