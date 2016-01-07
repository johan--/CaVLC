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

#include "VLCThumbnailer.h"

#include <cstring>
#ifdef WITH_JPEG
#include <jpeglib.h>
#if ( !defined(JPEG_LIB_VERSION_MAJOR) && !defined(JPEG_LIB_VERSION_MINOR) ) || \
    ( JPEG_LIB_VERSION_MAJOR <= 8 && JPEG_LIB_VERSION_MINOR < 4 )
//FIXME: I don't think we can expect this to work without VLC outputing BGR...
#define JPEG_COLORSPACE JCS_EXT_BGR
#else
#define JPEG_COLORSPACE JCS_RGB
#endif
#elif defined(WITH_EVAS)
#include <Evas_Engine_Buffer.h>
#endif
#include <setjmp.h>

#include "Media.h"
#include "File.h"
#include "logging/Logger.h"
#include "MediaLibrary.h"

VLCThumbnailer::VLCThumbnailer(const VLC::Instance &vlc)
    : m_instance( vlc )
    , m_cb( nullptr )
    , m_ml( nullptr )
#ifdef WITH_EVAS
    , m_canvas( nullptr, &evas_free )
#endif
    , m_thumbnailRequired( false )
    , m_width( 0 )
    , m_height( 0 )
    , m_prevSize( 0 )
{
#ifdef WITH_EVAS
    static int fakeBuffer;
#ifndef TIZEN
    evas_init();
#endif
    auto method = evas_render_method_lookup("buffer");
    m_canvas.reset( evas_new() );
    if ( m_canvas == nullptr )
        throw std::runtime_error( "Failed to allocate canvas" );
    evas_output_method_set( m_canvas.get(), method );
    evas_output_size_set(m_canvas.get(), 1, 1 );
    evas_output_viewport_set( m_canvas.get(), 0, 0, 1, 1 );
    auto einfo = (Evas_Engine_Info_Buffer *)evas_engine_info_get( m_canvas.get() );
    einfo->info.depth_type = EVAS_ENGINE_BUFFER_DEPTH_ARGB32;
    einfo->info.dest_buffer = &fakeBuffer;
    einfo->info.dest_buffer_row_bytes = 4;
    einfo->info.use_color_key = 0;
    einfo->info.alpha_threshold = 0;
    einfo->info.func.new_update_region = NULL;
    einfo->info.func.free_update_region = NULL;
    evas_engine_info_set( m_canvas.get(), (Evas_Engine_Info *)einfo );
    m_cropBuffer.reset( new uint8_t[ DesiredWidth * DesiredHeight * Bpp ] );
#endif
}

VLCThumbnailer::~VLCThumbnailer()
{
#if defined(WITH_EVAS) && !defined(TIZEN)
    evas_shutdown();
#endif
}

bool VLCThumbnailer::initialize(IMetadataServiceCb *callback, MediaLibrary* ml)
{
    m_cb = callback;
    m_ml = ml;
    return true;
}

unsigned int VLCThumbnailer::priority() const
{
    // This needs to be lower than the VLCMetadataService, since we want to know the media type.
    return 50;
}

void VLCThumbnailer::run( std::shared_ptr<Media> media, std::shared_ptr<File> file, void *data )
{
    if ( media->type() == IMedia::Type::UnknownType )
    {
        // If we don't know the media type yet, it actually looks more like a bug
        // since this should run after media type deduction, and not run in case
        // that step fails.
        m_cb->done( media, file, Status::Fatal, data );
        return;
    }
    else if ( media->type() != IMedia::Type::VideoType )
    {
        // There's no point in generating a thumbnail for a non-video media.
        m_cb->done( media, file, Status::Success, data );
        return;
    }
    else if ( media->thumbnail().empty() == false )
    {
        LOG_INFO(media->thumbnail(), " already has a thumbnail" );
        m_cb->done( media, file, Status::Success, data );
        return;
    }

    LOG_INFO( "Generating ", file->mrl(), " thumbnail..." );

    VLC::Media vlcMedia( m_instance, file->mrl(), VLC::Media::FromPath );
    vlcMedia.addOption( ":no-audio" );
    VLC::MediaPlayer mp( vlcMedia );

    setupVout( mp );

    if ( startPlayback( media, file, mp, data ) == false )
    {
        LOG_WARN( "Failed to generate ", file->mrl(), " thumbnail" );
        return;
    }
    // Seek ahead to have a significant preview
    if ( seekAhead( media, file, mp, data ) == false )
    {
        LOG_WARN( "Failed to generate ", file->mrl(), " thumbnail" );
        return;
    }
    takeThumbnail( media, file, mp, data );
    LOG_INFO( "Done generating ", file->mrl(), " thumbnail" );
}

bool VLCThumbnailer::startPlayback(std::shared_ptr<Media> media, std::shared_ptr<File> file, VLC::MediaPlayer &mp, void* data )
{

    mp.eventManager().onPlaying([this]() {
        m_cond.notify_all();
    });
    mp.eventManager().onEncounteredError([this]() {
        m_cond.notify_all();
    });
    mp.play();
    std::unique_lock<std::mutex> lock( m_mutex );
    bool success = m_cond.wait_for( lock, std::chrono::seconds( 3 ), [&mp]() {
        auto s = mp.state();
        return s == libvlc_Playing || s == libvlc_Error || s == libvlc_Ended;
    });
    auto s = mp.state();
    if ( success == false || s == libvlc_Error || s == libvlc_Ended )
    {
        // In case of timeout or error, don't go any further
        m_cb->done( media, file, Status::Error, data );
        return false;
    }
    return true;
}

bool VLCThumbnailer::seekAhead(std::shared_ptr<Media> media, std::shared_ptr<File> file, VLC::MediaPlayer& mp, void* data )
{
    std::unique_lock<std::mutex> lock( m_mutex );
    float pos = .0f;
    auto event = mp.eventManager().onPositionChanged([this, &pos](float p) {
        std::unique_lock<std::mutex> lock( m_mutex );
        pos = p;
        m_cond.notify_all();
    });
    mp.setPosition( .4f );
    bool success = m_cond.wait_for( lock, std::chrono::seconds( 3 ), [&pos]() {
        return pos >= .1f;
    });
    // Since we're locking a mutex for each position changed, let's unregister ASAP
    event->unregister();
    if ( success == false )
    {
        m_cb->done( media, file, Status::Error, data );
        return false;
    }
    return true;
}

void VLCThumbnailer::setupVout( VLC::MediaPlayer& mp )
{
    mp.setVideoFormatCallbacks(
        // Setup
        [this, &mp](char* chroma, unsigned int* width, unsigned int *height, unsigned int *pitches, unsigned int *lines) {
            strcpy( chroma, VLC_FOURCC );

            const float inputAR = (float)*width / *height;

            m_width = DesiredWidth;
            m_height = (float)m_width / inputAR + 1;
            if ( m_height < DesiredHeight )
            {
                // Avoid downscaling too much for really wide pictures
                m_width = inputAR * DesiredHeight;
                m_height = DesiredHeight;
            }
            auto size = m_width * m_height * Bpp;
            // If our buffer isn't enough anymore, reallocate a new one.
            if ( size > m_prevSize )
            {
                m_buff.reset( new uint8_t[size] );
                m_prevSize = size;
            }
            *width = m_width;
            *height = m_height;
            *pitches = m_width * Bpp;
            *lines = m_height;
            return 1;
        },
        // Cleanup
        nullptr);
    mp.setVideoCallbacks(
        // Lock
        [this](void** pp_buff) {
            *pp_buff = m_buff.get();
            return nullptr;
        },
        //unlock
        [this](void*, void*const*) {
            bool expected = true;
            if ( m_thumbnailRequired.compare_exchange_strong( expected, false ) )
            {
                m_cond.notify_all();
            }
        }
        ,
        //display
        nullptr
    );
}

bool VLCThumbnailer::takeThumbnail(std::shared_ptr<Media> media, std::shared_ptr<File> file, VLC::MediaPlayer &mp, void *data)
{
    // lock, signal that we want a thumbnail, and wait.
    {
        std::unique_lock<std::mutex> lock( m_mutex );
        m_thumbnailRequired = true;
        bool success = m_cond.wait_for( lock, std::chrono::seconds( 3 ), [this]() {
            // Keep waiting if the vmem thread hasn't restored m_thumbnailRequired to false
            return m_thumbnailRequired == false;
        });
        if ( success == false )
        {
            m_cb->done( media, file, Status::Error, data );
            return false;
        }
    }
    mp.stop();
    return compress( media, file, data );
}

#ifdef WITH_JPEG

struct jpegError : public jpeg_error_mgr
{
    jmp_buf buff;
    char message[JMSG_LENGTH_MAX];

    static void jpegErrorHandler(j_common_ptr common)
    {
        auto error = reinterpret_cast<jpegError*>(common->err);
        (*error->format_message)( common, error->message );
        longjmp(error->buff, 1);
    }
};

#endif

bool VLCThumbnailer::compress( std::shared_ptr<Media> media, std::shared_ptr<File> file, void *data )
{
    auto path = m_ml->thumbnailPath();
    path += "/";
    path += std::to_string( media->id() ) +
#ifdef WITH_EVAS
            ".png";
#else
            ".jpg";
#endif

    auto hOffset = m_width > DesiredWidth ? ( m_width - DesiredWidth ) / 2 : 0;
    auto vOffset = m_height > DesiredHeight ? ( m_height - DesiredHeight ) / 2 : 0;
    const auto stride = m_width * Bpp;

#ifdef WITH_JPEG
    //FIXME: Abstract this away, though libjpeg requires a FILE*...
    auto fOut = std::unique_ptr<FILE, int(*)(FILE*)>( fopen( path.c_str(), "wb" ), &fclose );
    if ( fOut == nullptr )
    {
        LOG_ERROR("Failed to open thumbnail file ", path);
        m_cb->done( media, file, Status::Error, data );
        return false;
    }

    jpeg_compress_struct compInfo;
    JSAMPROW row_pointer[1];

    //libjpeg's default error handling is to call exit(), which would
    //be slightly problematic...
    jpegError err;
    compInfo.err = jpeg_std_error(&err);
    err.error_exit = jpegError::jpegErrorHandler;

    if ( setjmp( err.buff ) )
    {
        LOG_ERROR("JPEG failure: ", err.message);
        jpeg_destroy_compress(&compInfo);
        m_cb->done( media, file, Status::Error, data );
        return false;
    }

    jpeg_create_compress(&compInfo);
    jpeg_stdio_dest(&compInfo, fOut.get());

    compInfo.image_width = DesiredWidth;
    compInfo.image_height = DesiredHeight;
    compInfo.input_components = Bpp;
    compInfo.in_color_space = JPEG_COLORSPACE;
    jpeg_set_defaults( &compInfo );
    jpeg_set_quality( &compInfo, 85, TRUE );

    jpeg_start_compress( &compInfo, TRUE );

    while (compInfo.next_scanline < DesiredHeight)
    {
        row_pointer[0] = &m_buff[(compInfo.next_scanline + vOffset ) * stride + hOffset];
        jpeg_write_scanlines(&compInfo, row_pointer, 1);
    }
    jpeg_finish_compress(&compInfo);
    jpeg_destroy_compress(&compInfo);
#elif defined(WITH_EVAS)
    auto evas_obj = std::unique_ptr<Evas_Object, void(*)(Evas_Object*)>( evas_object_image_add( m_canvas.get() ), evas_object_del );
    if ( evas_obj == nullptr )
        return false;

    uint8_t *p_buff = m_buff.get();
    if ( DesiredWidth != m_width )
    {
        p_buff += vOffset * stride;
        for ( auto y = 0u; y < DesiredHeight; ++y )
        {
            memcpy( m_cropBuffer.get() + y * DesiredWidth * Bpp, p_buff + (hOffset * Bpp), DesiredWidth * Bpp );
            p_buff += stride;
        }
        vOffset = 0;
        p_buff = m_cropBuffer.get();
    }

    evas_object_image_colorspace_set( evas_obj.get(), EVAS_COLORSPACE_ARGB8888 );
    evas_object_image_size_set( evas_obj.get(), DesiredWidth, DesiredHeight );
    evas_object_image_data_set( evas_obj.get(), p_buff + vOffset * stride );
    evas_object_image_save( evas_obj.get(), path.c_str(), NULL, "quality=100 compress=9");
#else
#error FIXME
#endif

    media->setThumbnail( path );
    if ( media->save() == false )
        m_cb->done( media, file, Status::Error, data );
    else
        m_cb->done( media, file, Status::Success, data );
    return true;
}
