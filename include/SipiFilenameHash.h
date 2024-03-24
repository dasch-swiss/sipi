/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPIFILENAMEHASH_H
#define SIPI_SIPIFILENAMEHASH_H

#include <string>
#include <vector>

/*!
 * Create a hash value of a filename consisting of a vector of chars 'A'-'Z'
 *
 * Usually there should not be too many files in one directory. 1) it may – depending
 * on the file system used – degrade performance severely, and 2) it makes operations
 * like rsync, file listings etc. cumbersome if there are thousands of files in
 * one directroy. A common way is to create a tree of subdirectories. In SIPI we use
 * subdirectories named 'A'-'Z' and a configurable number of levels of subdirectories.
 * In order to calculate the path efficiently we use avery prinitive, simple custom
 * hash function whichs "returns" a vector of the characters 'A'-'Z'. We overload
 * the [] operator in order to access the characters.
 *
 * The class may throw a SipiError!
 */
class SipiFilenameHash
{
private:
  const int seed = 137;//!< an arbitrary seed value (no special meaning...)
  const int numchars = 26;
  const unsigned int modval = 308915776;// (26^6)
  const int hash_len = 6;
  std::string path;
  std::string name;
  std::vector<char> *hash;//!< Pointer to a vector of chars
  static int __levels;

public:
  /*!
   * Constructor for hash vector
   *
   * @param name Filename of file for which the "hash" should be calculated
   * @param hash_len Number of directory levels. Must be in the range (inklusive) of [1..6]!
   */
  SipiFilenameHash(const std::string &path_p);

  /*!
   * Copy constructor (makes deep copy)
   *
   * @param other Reference to SipiFilenameHash instance
   */
  SipiFilenameHash(const SipiFilenameHash &other);

  /*!
   * Assignment copy operator (deep copy)
   *
   * @param other Reference to SipiFilenameHash instance
   * @return
   */
  SipiFilenameHash &operator=(const SipiFilenameHash &other);

  /*!
   * Destructor which frees all allocated memory (pointer to vector)
   */
  virtual ~SipiFilenameHash();

  inline static void setLevels(int l) { __levels = l; }

  inline static int getLevels(void) { return __levels; }


  /*!
   * Index operator to access hash character
   *
   * @param index Number between [0..5] inclusive
   *
   * @return Character in the range 'A' to 'Z'
   */
  char &operator[](int index);

  std::string filepath(void);

  /*!
   * Returns the number of levels used in the image repository
   *
   * @param imgdir Top of image repository
   * @return Returns the number of levels used
   */
  static int check_levels(const std::string &imgdir);

  /*!
   * Changes the number of levels and migrates all files
   *
   * @param imgdir  Top of image repository
   * @param levels Nunber of levels the image respository should have
   */
  static void migrateToLevels(const std::string &imgdir, int levels);
};


#endif// SIPI_SIPIFILENAMEHASH_H
