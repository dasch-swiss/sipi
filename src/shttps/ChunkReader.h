/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements reading of chunks
 *
 */
#ifndef __shttp_chunk_reader_h
#define __shttp_chunk_reader_h

#include <iostream>
#include <vector>

#include "Error.h"

namespace shttps {

/*!
 * The class ChunkReader handles reading from HTTP-connections that used the
 * chunked transfer method. This method is used whenever the size of the data
 * to be transferred is known when sending the data starts. A chunk consists of
 * a header line containing the number of bytes in the chunk as hexadezimal value
 * followed by the data and an empty line. The end of the transfer is indicated
 * by an empty chunk.
 */
class ChunkReader
{
private:
  std::istream *ins;
  size_t chunk_size;
  size_t chunk_pos;
  size_t post_maxsize;

  size_t read_chunk(std::istream &ins, char **buf, size_t offs = 0);

public:
  /*!
   * Constructor for class used for reading chunks from a HTTP connection that is chunked
   *
   * \param[in] ins_p Input stream (e.g. socket stream of HTTP connection)
   * \param[in] maxisze_p Maximal total size the chunk reader is allowed to read.
   *            If the chunk or total data is bigger, an shttps::Error is thrown!
   *            IF post_maxsize_p is 0 (default), there is no limit.
   */
  ChunkReader(std::istream *ins_p, size_t post_maxsize_p = 0);

  /*!
   * Read all chunks and return the data in the buffer
   *
   * This method reads all data from all chunks and returns it in the
   * pointer given. The data is allocated using malloc and realloc.
   * Important: The caller
   * is responsible for freeing the memory using free()!
   *
   * \param buf Address of a pointer to char. The method allocates the memory
   * using malloc and realloc and returns the pointer to the data. The caller is
   * responsible of freeing the memry using free()!
   *
   * \returns The number of bytes that have been read (length of buf)
   */
  size_t readAll(char **buf);

  /*!
   * Get the next (text-)line from the chunk stream, even if the line spans the chunk
   * boundary of two successive chunks
   *
   * \param[out] String with the textline
   *
   * \return Number of bytes read (that is the length of the line...)
   */
  size_t getline(std::string &t);

  /*!
   * Get the next byte in a chunked stream
   *
   * \returns The next byte or EOF, if the end of the HTTP data is reached
   */
  int getc(void);
};

}// namespace shttps

#endif
