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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "SqliteConnection.h"

#include "database/SqliteTools.h"

namespace medialibrary
{

SqliteConnection::SqliteConnection( const std::string &dbPath )
    : m_dbPath( dbPath )
    , m_readLock( m_contextLock )
    , m_writeLock( m_contextLock )
{
    if ( sqlite3_threadsafe() == 0 )
        throw std::runtime_error( "SQLite isn't built with threadsafe mode" );
    if ( sqlite3_config( SQLITE_CONFIG_MULTITHREAD ) == SQLITE_ERROR )
        throw std::runtime_error( "Failed to enable sqlite multithreaded mode" );
}

SqliteConnection::~SqliteConnection()
{
    sqlite::Statement::FlushStatementCache();
}

SqliteConnection::Handle SqliteConnection::getConn()
{
    std::unique_lock<compat::Mutex> lock( m_connMutex );
    sqlite3* dbConnection;
    auto it = m_conns.find( compat::this_thread::get_id() );
    if ( it == end( m_conns ) )
    {
        auto res = sqlite3_open( m_dbPath.c_str(), &dbConnection );
        ConnPtr dbConn( dbConnection, &sqlite3_close );
        if ( res != SQLITE_OK )
            throw sqlite::errors::Generic( std::string( "Failed to connect to database: " )
                                           + sqlite3_errstr( res ) );
        sqlite3_extended_result_codes( dbConnection, 1 );
        sqlite3_busy_timeout( dbConnection, 500 );
        // Don't use public wrapper, they need to be able to call getConn, which
        // would result from a recursive call and a deadlock from here.
        setPragmaEnabled( dbConnection, "foreign_keys", true );
        setPragmaEnabled( dbConnection, "recursive_triggers", true );

        m_conns.emplace( compat::this_thread::get_id(), std::move( dbConn ) );
        sqlite3_update_hook( dbConnection, &updateHook, this );
        return dbConnection;
    }
    return it->second.get();
}

std::unique_ptr<sqlite::Transaction> SqliteConnection::newTransaction()
{
    return std::unique_ptr<sqlite::Transaction>{ new sqlite::Transaction( this ) };
}

SqliteConnection::ReadContext SqliteConnection::acquireReadContext()
{
    return ReadContext{ m_readLock };
}

SqliteConnection::WriteContext SqliteConnection::acquireWriteContext()
{
    return WriteContext{ m_writeLock };
}

void SqliteConnection::setPragmaEnabled( Handle conn,
                                         const std::string& pragmaName,
                                         bool value )
{
    std::string reqBase = std::string{ "PRAGMA " } + pragmaName;
    std::string reqSet = reqBase + " = " + ( value ? "ON" : "OFF" );

    sqlite::Statement stmt( conn, reqSet );
    stmt.execute();
    if ( stmt.row() != nullptr )
        throw std::runtime_error( "Failed to enable/disable " + pragmaName );

    sqlite::Statement stmtCheck( conn, reqBase );
    stmtCheck.execute();
    auto resultRow = stmtCheck.row();
    bool resultValue;
    resultRow >> resultValue;
    if( resultValue != value )
        throw std::runtime_error( "PRAGMA " + pragmaName + " value mismatch" );
}

void SqliteConnection::setForeignKeyEnabled( bool value )
{
    // Ensure no transaction will be started during the pragma change
    auto ctx = acquireWriteContext();
    // Changing this pragma during a transaction is a no-op (silently ignored by
    // sqlite), so ensure we're doing something usefull here:
    assert( sqlite::Transaction::transactionInProgress() == false );
    setPragmaEnabled( getConn(), "foreign_keys", value );
}

void SqliteConnection::setRecursiveTriggers( bool value )
{
    // Ensure no request will run while we change this setting
    auto ctx = acquireWriteContext();

    // Changing the recursive_triggers setting affects the execution of all
    // statements prepared using the database connection, including those
    // prepared before the setting was changed. Any existing statements prepared
    // using the legacy sqlite3_prepare() interface may fail with an
    // SQLITE_SCHEMA error after the recursive_triggers setting is changed.
    // https://sqlite.org/pragma.html#pragma_recursive_triggers
    sqlite::Statement::FlushStatementCache();

    setPragmaEnabled( getConn(), "recursive_triggers", value );
}

void SqliteConnection::registerUpdateHook( const std::string& table, SqliteConnection::UpdateHookCb cb )
{
    m_hooks.emplace( table, cb );
}

void SqliteConnection::updateHook( void* data, int reason, const char*,
                                   const char* table, sqlite_int64 rowId )
{
    const auto self = reinterpret_cast<SqliteConnection*>( data );
    auto it = self->m_hooks.find( table );
    if ( it == end( self->m_hooks ) )
        return;
    switch ( reason )
    {
    case SQLITE_INSERT:
        it->second( HookReason::Insert, rowId );
        break;
    case SQLITE_UPDATE:
        it->second( HookReason::Update, rowId );
        break;
    case SQLITE_DELETE:
        it->second( HookReason::Delete, rowId );
        break;
    }
}

SqliteConnection::WeakDbContext::WeakDbContext( SqliteConnection* conn )
    : m_conn( conn )
{
    m_conn->setForeignKeyEnabled( false );
    m_conn->setRecursiveTriggers( false );
}

SqliteConnection::WeakDbContext::~WeakDbContext()
{
    m_conn->setForeignKeyEnabled( true );
    m_conn->setRecursiveTriggers( true );
}


}