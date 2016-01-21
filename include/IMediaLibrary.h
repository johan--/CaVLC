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

#ifndef IMEDIALIBRARY_H
#define IMEDIALIBRARY_H

#include <vector>
#include <string>

#include "Types.h"

namespace medialibrary
{
    static constexpr auto UnknownArtistID = 1u;
    static constexpr auto VariousArtistID = 2u;

    struct MediaSearchAggregate
    {
        std::vector<MediaPtr> episodes;
        std::vector<MediaPtr> movies;
        std::vector<MediaPtr> others;
        std::vector<MediaPtr> tracks;
    };

    struct SearchAggregate
    {
        std::vector<AlbumPtr> albums;
        std::vector<ArtistPtr> artists;
        std::vector<GenrePtr> genres;
        MediaSearchAggregate media;
        std::vector<PlaylistPtr> playlists;
    };
}

class IMediaLibraryCb
{
public:
    virtual ~IMediaLibraryCb() = default;
    /**
     * @brief onFileAdded Will be called when a media gets added.
     * Depending if the media is being restored or was just discovered,
     * the media type might be a best effort guess. If the media was freshly
     * discovered, it is extremely likely that no metadata will be
     * available yet.
     */
    virtual void onMediaAdded( MediaPtr media ) = 0;
    /**
     * @brief onFileUpdated Will be called when a file metadata gets updated.
     */
    virtual void onFileUpdated( MediaPtr media ) = 0;

    virtual void onDiscoveryStarted( const std::string& entryPoint ) = 0;
    virtual void onDiscoveryCompleted( const std::string& entryPoint ) = 0;
    virtual void onReloadStarted( const std::string& entryPoint ) = 0;
    virtual void onReloadCompleted( const std::string& entryPoint ) = 0;
    /**
     * @brief onParsingStatsUpdated Called when the parser statistics are updated
     *
     * There is no waranty about how often this will be called.
     * @param percent The progress percentage [0,100]
     *
     */
    virtual void onParsingStatsUpdated( uint32_t percent) = 0;
};

class IMediaLibrary
{
    public:
        virtual ~IMediaLibrary() = default;
        ///
        /// \brief  initialize Initializes the media library.
        ///         This will use the provided discoverer to search for new media asynchronously.
        ///
        /// \param dbPath       Path to the database
        /// \return true in case of success, false otherwise
        ///
        virtual bool initialize( const std::string& dbPath, const std::string& thumbnailPath, IMediaLibraryCb* metadataCb ) = 0;
        virtual void setVerbosity( LogLevel v ) = 0;

        virtual LabelPtr createLabel( const std::string& label ) = 0;
        virtual bool deleteLabel( LabelPtr label ) = 0;
        virtual std::vector<MediaPtr> audioFiles() = 0;
        virtual std::vector<MediaPtr> videoFiles() = 0;
        virtual AlbumPtr album( unsigned int id ) = 0;
        virtual std::vector<AlbumPtr> albums() = 0;
        virtual ShowPtr show( const std::string& name ) = 0;
        virtual MoviePtr movie( const std::string& title ) = 0;
        virtual ArtistPtr artist( unsigned int id ) = 0;
        virtual std::vector<ArtistPtr> artists() const = 0;
        virtual std::vector<GenrePtr> genres() const = 0;

        /***
         *  Playlists
         */
        virtual PlaylistPtr createPlaylist( const std::string& name ) = 0;
        virtual std::vector<PlaylistPtr> playlists() = 0;
        virtual bool deletePlaylist( unsigned int playlistId ) = 0;

        /**
         * History
         */
        virtual bool addToHistory( MediaPtr media ) = 0;
        virtual bool addToHistory( const std::string& mrl ) = 0;
        virtual std::vector<HistoryPtr> history() const = 0;

        /**
         * Search
         */
        virtual medialibrary::MediaSearchAggregate searchMedia( const std::string& pattern ) const = 0;
        virtual std::vector<PlaylistPtr> searchPlaylists( const std::string& name ) const = 0;
        virtual std::vector<AlbumPtr> searchAlbums( const std::string& pattern ) const = 0;
        virtual std::vector<GenrePtr> searchGenre( const std::string& genre ) const = 0;
        virtual std::vector<ArtistPtr> searchArtists( const std::string& name ) const = 0;
        virtual medialibrary::SearchAggregate search( const std::string& pattern ) const = 0;

        /**
         * @brief discover Launch a discovery on the provided entry point.
         * The actuall discovery will run asynchronously, meaning this method will immediatly return.
         * Depending on which discoverer modules where provided, this might or might not work
         * @param entryPoint What to discover.
         */
        virtual void discover( const std::string& entryPoint ) = 0;
        /**
         * @brief banFolder will blacklist a folder for discovery
         */
        virtual bool banFolder( const std::string& path ) = 0;
        virtual bool unbanFolder( const std::string& path ) = 0;
        virtual const std::string& thumbnailPath() const = 0;
        virtual void setLogger( ILogger* logger ) = 0;
        /**
         * @brief pauseBackgroundOperations Will stop potentially CPU intensive background
         * operations, until resumeBackgroundOperations() is called.
         * If an operation is currently running, it will finish before pausing.
         */
        virtual void pauseBackgroundOperations() = 0;
        /**
         * @brief resumeBackgroundOperations Resumes background tasks, previously
         * interrupted by pauseBackgroundOperations().
         */
        virtual void resumeBackgroundOperations() = 0;
        virtual void reload() = 0;
        virtual void reload( const std::string& entryPoint ) = 0;
};

extern "C"
{
    IMediaLibrary* NewMediaLibrary();
}

#endif // IMEDIALIBRARY_H
