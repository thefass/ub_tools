/** \file   FullTextCache.h
 *  \brief  Anything relating to our full-text cache.
 *  \author Dr. Johannes Ruscheinski (johannes.ruscheinski@uni-tuebingen.de)
 *
 *  \copyright 2017 Universitätsbibliothek Tübingen.  All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef FullTextCache_H
#define FullTextCache_H


#include <string>
#include "DbConnection.h"


namespace FullTextCache {


/** \brief Test whether an entry in the cache has expired or not.
 *  \return True if we find "id" in the database and the entry is older than now-CACHE_EXPIRE_TIME_DELTA or if "id"
 *          is not found in the database, else false.
 *  \note Deletes expired entries and associated data in the key/value database found at "full_text_db_path".
 */
bool CacheExpired(DbConnection * const db_connection, const std::string &full_text_db_path, const std::string &key);


// \note If "data" is empty only an entry will be made in the SQL database but not in the key/value store.
void InsertIntoCache(DbConnection * const db_connection, const std::string &full_text_db_path,
                     const std::string &key, const std::string &data);


} // namespace FullTextCache


#endif // ifndef FullTextCache_H