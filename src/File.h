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

#ifndef FILE_H
#define FILE_H


#include <sqlite3.h>

#include "IFile.h"
#include "database/Cache.h"

class Album;
class ShowEpisode;
class AlbumTrack;

class File;

namespace policy
{
struct FileTable
{
    static const std::string Name;
    static const std::string CacheColumn;
    static unsigned int File::*const PrimaryKey;
};
struct FileCache
{
    typedef std::string KeyType;
    static const std::string& key(const std::shared_ptr<File> self);
    static std::string key( sqlite3_stmt* stmt );
};
}

class File : public IFile, public Cache<File, IFile, policy::FileTable, policy::FileCache>
{
    private:
        typedef Cache<File, IFile, policy::FileTable, policy::FileCache> _Cache;
    public:

        // Those should be private, however the standard states that the expression
        // ::new (pv) T(std::forward(args)...)
        // shall be well-formed, and private constructor would prevent that.
        // There might be a way with a user-defined allocator, but we'll see that later...
        File(DBConnection dbConnection , sqlite3_stmt* stmt);
        File(const fs::IFile* file, unsigned int folderId, const std::string &name, Type type);

        static FilePtr create(DBConnection dbConnection, Type type, const fs::IFile* file , unsigned int folderId);
        static bool createTable( DBConnection connection );

        virtual unsigned int id() const;
        virtual Type type() override;
        virtual bool setType( Type type ) override;
        virtual const std::string& name() override;
        virtual bool setName( const std::string& name ) override;
        virtual AlbumTrackPtr albumTrack();
        virtual bool setAlbumTrack( AlbumTrackPtr albumTrack );
        virtual int64_t duration() const;
        virtual bool setDuration( int64_t duration) override;
        virtual std::shared_ptr<IShowEpisode> showEpisode();
        virtual bool setShowEpisode( ShowEpisodePtr showEpisode );
        virtual bool addLabel( LabelPtr label );
        virtual bool removeLabel( LabelPtr label );
        virtual std::vector<std::shared_ptr<ILabel> > labels();
        virtual int playCount() const;
        virtual const std::string& mrl() const;
        virtual MoviePtr movie();
        virtual bool setMovie( MoviePtr movie );
        virtual bool addVideoTrack( const std::string& codec, unsigned int width,
                                    unsigned int height, float fps );
        virtual std::vector<VideoTrackPtr> videoTracks();
        virtual bool addAudioTrack(const std::string& codec, unsigned int bitrate , unsigned int sampleRate, unsigned int nbChannels);
        virtual std::vector<AudioTrackPtr> audioTracks();
        virtual const std::string& snapshot() override;
        virtual bool setSnapshot( const std::string& snapshot ) override;

        virtual bool isStandAlone() override;
        virtual unsigned int lastModificationDate() override;

        virtual bool markParsed() override;
        virtual bool isParsed() const override;

    private:
        DBConnection m_dbConnection;

        // DB fields:
        unsigned int m_id;
        Type m_type;
        int64_t m_duration;
        unsigned int m_albumTrackId;
        unsigned int m_playCount;
        unsigned int m_showEpisodeId;
        std::string m_mrl;
        unsigned int m_movieId;
        unsigned int m_folderId;
        unsigned int m_lastModificationDate;
        std::string m_snapshot;
        bool m_isParsed;
        std::string m_name;

        // Auto fetched related properties
        Album* m_album;
        AlbumTrackPtr m_albumTrack;
        ShowEpisodePtr m_showEpisode;
        MoviePtr m_movie;

        friend class Cache<File, IFile, policy::FileTable, policy::FileCache>;
        friend struct policy::FileTable;
};

#endif // FILE_H
