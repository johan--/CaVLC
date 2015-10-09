/*****************************************************************************
 * Media Library
 *****************************************************************************
 * Copyright (C) 2015 Hugo Beauzée-Luyssen, Videolabs
 *
 * Authors: Hugo Beauzée-Luyssen<hugo@beauzee.fr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef ISHOWEPISODE_H
#define ISHOWEPISODE_H

class IShow;

#include "IMediaLibrary.h"

class IShowEpisode
{
    public:
        virtual ~IShowEpisode(){}

        virtual unsigned int id() const = 0;
        virtual const std::string& artworkUrl() const = 0;
        virtual bool setArtworkUrl( const std::string& artworkUrl ) = 0;
        virtual unsigned int episodeNumber() const = 0;
        virtual time_t lastSyncDate() const = 0;
        virtual const std::string& name() const = 0;
        virtual unsigned int seasonNumber() const = 0;
        virtual bool setSeasonNumber(unsigned int seasonNumber) = 0;
        virtual const std::string& shortSummary() const = 0;
        virtual bool setShortSummary( const std::string& summary ) = 0;
        virtual const std::string& tvdbId() const = 0;
        virtual bool setTvdbId( const std::string& tvdbId ) = 0;
        virtual std::shared_ptr<IShow> show() = 0;
        virtual std::vector<FilePtr> files() = 0;
        /**
         * @brief destroy Deletes the album track and the file(s) associated
         */
        virtual bool destroy() = 0;
};

#endif // ISHOWEPISODE_H
