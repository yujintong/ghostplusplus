/*

   Copyright [2008] [Trevor Hogan]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

	   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#include "ghost.h"
#include "util.h"
#include "config.h"
#include "ghostdb.h"
#include "ghostdbsqlite.h"
#include "sqlite3.h"

//
// CQSLITE3 (wrapper class)
//

CSQLITE3 :: CSQLITE3( QString filename )
{
	m_Ready = true;

	if( sqlite3_open_v2( filename.toUtf8().data(), (sqlite3 **)&m_DB, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL ) != SQLITE_OK )
		m_Ready = false;
}

CSQLITE3 :: ~CSQLITE3( )
{
	sqlite3_close( (sqlite3 *)m_DB );
}

QString CSQLITE3 :: GetError( )
{
	return sqlite3_errmsg( (sqlite3 *)m_DB );
}

int CSQLITE3 :: Prepare( QString query, void **Statement )
{
	return sqlite3_prepare_v2( (sqlite3 *)m_DB, query.toUtf8().data( ), -1, (sqlite3_stmt **)Statement, NULL );
}

int CSQLITE3 :: Step( void *Statement )
{
	int RC = sqlite3_step( (sqlite3_stmt *)Statement );

	if( RC == SQLITE_ROW )
	{
		m_Row.clear( );

		for( int i = 0; i < sqlite3_column_count( (sqlite3_stmt *)Statement ); i++ )
		{
			char *ColumnText = (char *)sqlite3_column_text( (sqlite3_stmt *)Statement, i );

			if( ColumnText )
				m_Row.push_back( ColumnText );
			else
				m_Row.push_back( QString( ) );
		}
	}

	return RC;
}

int CSQLITE3 :: Finalize( void *Statement )
{
	return sqlite3_finalize( (sqlite3_stmt *)Statement );
}

int CSQLITE3 :: Reset( void *Statement )
{
	return sqlite3_reset( (sqlite3_stmt *)Statement );
}

int CSQLITE3 :: ClearBindings( void *Statement )
{
	return sqlite3_clear_bindings( (sqlite3_stmt *)Statement );
}

int CSQLITE3 :: Exec( QString query )
{
	return sqlite3_exec( (sqlite3 *)m_DB, query.toUtf8().data( ), NULL, NULL, NULL );
}

quint32 CSQLITE3 :: LastRowID( )
{
	return (quint32)sqlite3_last_insert_rowid( (sqlite3 *)m_DB );
}

//
// CGHostDBSQLite
//

CGHostDBSQLite :: CGHostDBSQLite( CConfig *CFG ) : CGHostDB( CFG )
{
	m_File = CFG->GetString( "db_sqlite3_file", "ghost.dbs" );
	CONSOLE_Print( "[SQLITE3] version " + QString( SQLITE_VERSION ) );
	CONSOLE_Print( "[SQLITE3] opening database [" + m_File + "]" );
	m_DB = new CSQLITE3( m_File );

	if( !m_DB->GetReady( ) )
	{
		// setting m_HasError to true indicates there's been a critical error and we want GHost to shutdown
		// this is okay here because we're in the constructor so we're not dropping any games or players

		CONSOLE_Print( QString( "[SQLITE3] error opening database [" + m_File + "] - " ) + m_DB->GetError( ) );
		SetError("error opening database");
		return;
	}

	// find the schema number so we can determine whether we need to upgrade or not

	QString SchemaNumber;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT value FROM config WHERE name=\"schema_number\"", (void **)&Statement );

	if( Statement )
	{
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
		{
			QList<QString> *Row = m_DB->GetRow( );

			if( Row->size( ) == 1 )
				SchemaNumber = (*Row)[0];
			else
				CONSOLE_Print( "[SQLITE3] error getting schema number - row doesn't have 1 column" );
		}
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error getting schema number - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error getting schema number - " + m_DB->GetError( ) );

	if( SchemaNumber.isEmpty( ) )
	{
		// couldn't find the schema number
		// unfortunately the very first schema didn't have a config table
		// so we might actually be looking at schema version 1 rather than an empty database
		// try to confirm this by looking for the admins table

		CONSOLE_Print( "[SQLITE3] couldn't find schema number, looking for admins table" );
		bool AdminTable = false;
		m_DB->Prepare( "SELECT * FROM sqlite_master WHERE type=\"table\" AND name=\"admins\"", (void **)&Statement );

		if( Statement )
		{
			int RC = m_DB->Step( Statement );

			// we're just checking to see if the query returned a row, we don't need to check the row data itself

			if( RC == SQLITE_ROW )
				AdminTable = true;
			else if( RC == SQLITE_ERROR )
				CONSOLE_Print( "[SQLITE3] error looking for admins table - " + m_DB->GetError( ) );

			m_DB->Finalize( Statement );
		}
		else
			CONSOLE_Print( "[SQLITE3] prepare error looking for admins table - " + m_DB->GetError( ) );

		if( AdminTable )
		{
			// the admins table exists, assume we're looking at schema version 1

			CONSOLE_Print( "[SQLITE3] found admins table, assuming schema number [1]" );
			SchemaNumber = "1";
		}
		else
		{
			// the admins table doesn't exist, assume the database is empty
			// note to self: update the SchemaNumber and the database structure when making a new schema

			CONSOLE_Print( "[SQLITE3] couldn't find admins table, assuming database is empty" );
			SchemaNumber = "8";

			if( m_DB->Exec( "CREATE TABLE admins ( id INTEGER PRIMARY KEY, name TEXT NOT NULL, server TEXT NOT NULL DEFAULT \"\" )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating admins table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE bans ( id INTEGER PRIMARY KEY, server TEXT NOT NULL, name TEXT NOT NULL, ip TEXT, date TEXT NOT NULL, gamename TEXT, admin TEXT NOT NULL, reason TEXT )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating bans table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE games ( id INTEGER PRIMARY KEY, server TEXT NOT NULL, map TEXT NOT NULL, datetime TEXT NOT NULL, gamename TEXT NOT NULL, ownername TEXT NOT NULL, duration INTEGER NOT NULL, gamestate INTEGER NOT NULL DEFAULT 0, creatorname TEXT NOT NULL DEFAULT \"\", creatorserver TEXT NOT NULL DEFAULT \"\" )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating games table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE gameplayers ( id INTEGER PRIMARY KEY, gameid INTEGER NOT NULL, name TEXT NOT NULL, ip TEXT NOT NULL, spoofed INTEGER NOT NULL, reserved INTEGER NOT NULL, loadingtime INTEGER NOT NULL, left INTEGER NOT NULL, leftreason TEXT NOT NULL, team INTEGER NOT NULL, colour INTEGER NOT NULL, spoofedrealm TEXT NOT NULL DEFAULT \"\" )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating gameplayers table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE dotagames ( id INTEGER PRIMARY KEY, gameid INTEGER NOT NULL, winner INTEGER NOT NULL, min INTEGER NOT NULL DEFAULT 0, sec INTEGER NOT NULL DEFAULT 0 )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating dotagames table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE dotaplayers ( id INTEGER PRIMARY KEY, gameid INTEGER NOT NULL, colour INTEGER NOT NULL, kills INTEGER NOT NULL, deaths INTEGER NOT NULL, creepkills INTEGER NOT NULL, creepdenies INTEGER NOT NULL, assists INTEGER NOT NULL, gold INTEGER NOT NULL, neutralkills INTEGER NOT NULL, item1 TEXT NOT NULL, item2 TEXT NOT NULL, item3 TEXT NOT NULL, item4 TEXT NOT NULL, item5 TEXT NOT NULL, item6 TEXT NOT NULL, hero TEXT NOT NULL DEFAULT \"\", newcolour NOT NULL DEFAULT 0, towerkills NOT NULL DEFAULT 0, raxkills NOT NULL DEFAULT 0, courierkills NOT NULL DEFAULT 0 )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating dotaplayers table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE config ( name TEXT NOT NULL PRIMARY KEY, value TEXT NOT NULL )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating config table - " + m_DB->GetError( ) );

			m_DB->Prepare( "INSERT INTO config VALUES ( \"schema_number\", ? )", (void **)&Statement );

			if( Statement )
			{
				sqlite3_bind_text( Statement, 1, SchemaNumber.toUtf8().data( ), -1, SQLITE_TRANSIENT );
				int RC = m_DB->Step( Statement );

				if( RC == SQLITE_ERROR )
					CONSOLE_Print( "[SQLITE3] error inserting schema number [" + SchemaNumber + "] - " + m_DB->GetError( ) );

				m_DB->Finalize( Statement );
			}
			else
				CONSOLE_Print( "[SQLITE3] prepare error inserting schema number [" + SchemaNumber + "] - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE downloads ( id INTEGER PRIMARY KEY, map TEXT NOT NULL, mapsize INTEGER NOT NULL, datetime TEXT NOT NULL, name TEXT NOT NULL, ip TEXT NOT NULL, spoofed INTEGER NOT NULL, spoofedrealm TEXT NOT NULL, downloadtime INTEGER NOT NULL )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating downloads table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE w3mmdplayers ( id INTEGER PRIMARY KEY, category TEXT NOT NULL, gameid INTEGER NOT NULL, pid INTEGER NOT NULL, name TEXT NOT NULL, flag TEXT NOT NULL, leaver INTEGER NOT NULL, practicing INTEGER NOT NULL )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating w3mmdplayers table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE TABLE w3mmdvars ( id INTEGER PRIMARY KEY, gameid INTEGER NOT NULL, pid INTEGER NOT NULL, varname TEXT NOT NULL, value_int INTEGER DEFAULT NULL, value_real REAL DEFAULT NULL, value_QString TEXT DEFAULT NULL )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating w3mmdvars table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE INDEX idx_gameid ON gameplayers ( gameid )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating idx_gameid index on gameplayers table - " + m_DB->GetError( ) );

			if( m_DB->Exec( "CREATE INDEX idx_gameid_colour ON dotaplayers ( gameid, colour )" ) != SQLITE_OK )
				CONSOLE_Print( "[SQLITE3] error creating idx_gameid_colour index on dotaplayers table - " + m_DB->GetError( ) );
		}
	}
	else
		CONSOLE_Print( "[SQLITE3] found schema number [" + SchemaNumber + "]" );

	if( SchemaNumber == "1" )
	{
		Upgrade1_2( );
		SchemaNumber = "2";
	}

	if( SchemaNumber == "2" )
	{
		Upgrade2_3( );
		SchemaNumber = "3";
	}

	if( SchemaNumber == "3" )
	{
		Upgrade3_4( );
		SchemaNumber = "4";
	}

	if( SchemaNumber == "4" )
	{
		Upgrade4_5( );
		SchemaNumber = "5";
	}

	if( SchemaNumber == "5" )
	{
		Upgrade5_6( );
		SchemaNumber = "6";
	}

	if( SchemaNumber == "6" )
	{
		Upgrade6_7( );
		SchemaNumber = "7";
	}

	if( SchemaNumber == "7" )
	{
		Upgrade7_8( );
		SchemaNumber = "8";
	}

	if( m_DB->Exec( "CREATE TEMPORARY TABLE iptocountry ( ip1 INTEGER NOT NULL, ip2 INTEGER NOT NULL, country TEXT NOT NULL, PRIMARY KEY ( ip1, ip2 ) )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating temporary iptocountry table - " + m_DB->GetError( ) );

	FromAddStmt = NULL;
}

CGHostDBSQLite :: ~CGHostDBSQLite( )
{
	if( FromAddStmt )
		m_DB->Finalize( FromAddStmt );

	CONSOLE_Print( "[SQLITE3] closing database [" + m_File + "]" );
	delete m_DB;
}

void CGHostDBSQLite :: Upgrade1_2( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v1 to v2 started" );

	// add new column to table dotaplayers
	//  + hero TEXT NOT NULL DEFAULT ""

	if( m_DB->Exec( "ALTER TABLE dotaplayers ADD hero TEXT NOT NULL DEFAULT \"\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column hero to table dotaplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column hero to table dotaplayers" );

	// add new columns to table dotagames
	//  + min INTEGER NOT NULL DEFAULT 0
	//  + sec INTEGER NOT NULL DEFAULT 0

	if( m_DB->Exec( "ALTER TABLE dotagames ADD min INTEGER NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column min to table dotagames - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column min to table dotagames" );

	if( m_DB->Exec( "ALTER TABLE dotagames ADD sec INTEGER NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column sec to table dotagames - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column sec to table dotagames" );

	// add new table config

	if( m_DB->Exec( "CREATE TABLE config ( name TEXT NOT NULL PRIMARY KEY, value TEXT NOT NULL )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating config table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] created config table" );

	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO config VALUES ( \"schema_number\", \"2\" )", (void **)&Statement );

	if( Statement )
	{
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			CONSOLE_Print( "[SQLITE3] inserted schema number [2]" );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error inserting schema number [2] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error inserting schema number [2] - " + m_DB->GetError( ) );

	CONSOLE_Print( "[SQLITE3] schema upgrade v1 to v2 finished" );
}

void CGHostDBSQLite :: Upgrade2_3( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v2 to v3 started" );

	// add new column to table admins
	//  + server TEXT NOT NULL DEFAULT ""

	if( m_DB->Exec( "ALTER TABLE admins ADD server TEXT NOT NULL DEFAULT \"\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column server to table admins - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column server to table admins" );

	// add new column to table games
	//  + gamestate INTEGER NOT NULL DEFAULT 0

	if( m_DB->Exec( "ALTER TABLE games ADD gamestate INTEGER NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column gamestate to table games - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column gamestate to table games" );

	// add new column to table gameplayers
	//  + spoofedrealm TEXT NOT NULL DEFAULT ""

	if( m_DB->Exec( "ALTER TABLE gameplayers ADD spoofedrealm TEXT NOT NULL DEFAULT \"\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column spoofedrealm to table gameplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column spoofedrealm to table gameplayers" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"3\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [3] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [3]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v2 to v3 finished" );
}

void CGHostDBSQLite :: Upgrade3_4( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v3 to v4 started" );

	// add new columns to table games
	//  + creatorname TEXT NOT NULL DEFAULT ""
	//  + creatorserver TEXT NOT NULL DEFAULT ""

	if( m_DB->Exec( "ALTER TABLE games ADD creatorname TEXT NOT NULL DEFAULT \"\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column creatorname to table games - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column creatorname to table games" );

	if( m_DB->Exec( "ALTER TABLE games ADD creatorserver TEXT NOT NULL DEFAULT \"\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column creatorserver to table games - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column creatorserver to table games" );

	// add new table downloads

	if( m_DB->Exec( "CREATE TABLE downloads ( id INTEGER PRIMARY KEY, map TEXT NOT NULL, mapsize INTEGER NOT NULL, datetime TEXT NOT NULL, name TEXT NOT NULL, ip TEXT NOT NULL, spoofed INTEGER NOT NULL, spoofedrealm TEXT NOT NULL, downloadtime INTEGER NOT NULL )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating downloads table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] created downloads table" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"4\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [4] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [4]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v3 to v4 finished" );
}

void CGHostDBSQLite :: Upgrade4_5( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v4 to v5 started" );

	// add new column to table dotaplayers
	//  + newcolour NOT NULL DEFAULT 0

	if( m_DB->Exec( "ALTER TABLE dotaplayers ADD newcolour NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column newcolour to table dotaplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column newcolour to table dotaplayers" );

	// set newcolour = colour on all existing dotaplayers rows

	if( m_DB->Exec( "UPDATE dotaplayers SET newcolour=colour WHERE newcolour=0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error setting newcolour = colour on all existing dotaplayers rows - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] set newcolour = colour on all existing dotaplayers rows" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"5\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [5] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [5]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v4 to v5 finished" );
}

void CGHostDBSQLite :: Upgrade5_6( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v5 to v6 started" );

	// add new columns to table dotaplayers
	//  + towerkills NOT NULL DEFAULT 0
	//  + raxkills NOT NULL DEFAULT 0
	//  + courierkills NOT NULL DEFAULT 0

	if( m_DB->Exec( "ALTER TABLE dotaplayers ADD towerkills NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column towerkills to table dotaplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column towerkills to table dotaplayers" );

	if( m_DB->Exec( "ALTER TABLE dotaplayers ADD raxkills NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column raxkills to table dotaplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column raxkills to table dotaplayers" );

	if( m_DB->Exec( "ALTER TABLE dotaplayers ADD courierkills NOT NULL DEFAULT 0" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error adding new column courierkills to table dotaplayers - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new column courierkills to table dotaplayers" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"6\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [6] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [6]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v5 to v6 finished" );
}

void CGHostDBSQLite :: Upgrade6_7( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v6 to v7 started" );

	// add new index to table gameplayers

	if( m_DB->Exec( "CREATE INDEX idx_gameid ON gameplayers ( gameid )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating idx_gameid index on gameplayers table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new index idx_gameid to table gameplayers" );

	// add new index to table dotaplayers

	if( m_DB->Exec( "CREATE INDEX idx_gameid_colour ON dotaplayers ( gameid, colour )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating idx_gameid_colour index on dotaplayers table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] added new index idx_gameid_colour to table dotaplayers" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"7\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [7] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [7]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v6 to v7 finished" );
}

void CGHostDBSQLite :: Upgrade7_8( )
{
	CONSOLE_Print( "[SQLITE3] schema upgrade v7 to v8 started" );

	// create new tables

	if( m_DB->Exec( "CREATE TABLE w3mmdplayers ( id INTEGER PRIMARY KEY, category TEXT NOT NULL, gameid INTEGER NOT NULL, pid INTEGER NOT NULL, name TEXT NOT NULL, flag TEXT NOT NULL, leaver INTEGER NOT NULL, practicing INTEGER NOT NULL )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating w3mmdplayers table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] created w3mmdplayers table" );

	if( m_DB->Exec( "CREATE TABLE w3mmdvars ( id INTEGER PRIMARY KEY, gameid INTEGER NOT NULL, pid INTEGER NOT NULL, varname TEXT NOT NULL, value_int INTEGER DEFAULT NULL, value_real REAL DEFAULT NULL, value_QString TEXT DEFAULT NULL )" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error creating w3mmdvars table - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] created w3mmdvars table" );

	// update schema number

	if( m_DB->Exec( "UPDATE config SET value=\"8\" where name=\"schema_number\"" ) != SQLITE_OK )
		CONSOLE_Print( "[SQLITE3] error updating schema number [8] - " + m_DB->GetError( ) );
	else
		CONSOLE_Print( "[SQLITE3] updated schema number [8]" );

	CONSOLE_Print( "[SQLITE3] schema upgrade v7 to v8 finished" );
}

bool CGHostDBSQLite :: Begin( )
{
	return m_DB->Exec( "BEGIN TRANSACTION" ) == SQLITE_OK;
}

bool CGHostDBSQLite :: Commit( )
{
	return m_DB->Exec( "COMMIT TRANSACTION" ) == SQLITE_OK;
}

quint32 CGHostDBSQLite :: AdminCount( QString server )
{
	quint32 Count = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT COUNT(*) FROM admins WHERE server=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
			Count = sqlite3_column_int( Statement, 0 );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error counting admins [" + server + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error counting admins [" + server + "] - " + m_DB->GetError( ) );

	return Count;
}

bool CGHostDBSQLite :: AdminCheck( QString server, QString user )
{
	user = user.toLower();
	bool IsAdmin = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT * FROM admins WHERE server=? AND name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		// we're just checking to see if the query returned a row, we don't need to check the row data itself

		if( RC == SQLITE_ROW )
			IsAdmin = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error checking admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error checking admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

	return IsAdmin;
}

bool CGHostDBSQLite :: AdminAdd( QString server, QString user )
{
	user = user.toLower();
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO admins ( server, name ) VALUES ( ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

	return Success;
}

bool CGHostDBSQLite :: AdminRemove( QString server, QString user )
{
	user = user.toLower();
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "DELETE FROM admins WHERE server=? AND name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error removing admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error removing admin [" + server + " : " + user + "] - " + m_DB->GetError( ) );

	return Success;
}

QList<QString> CGHostDBSQLite :: AdminList( QString server )
{
	QList<QString> AdminList;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT name FROM admins WHERE server=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		while( RC == SQLITE_ROW )
		{
			QList<QString> *Row = m_DB->GetRow( );

			if( Row->size( ) == 1 )
				AdminList.push_back( (*Row)[0] );

			RC = m_DB->Step( Statement );
		}

		if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error retrieving admin list [" + server + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error retrieving admin list [" + server + "] - " + m_DB->GetError( ) );

	return AdminList;
}

quint32 CGHostDBSQLite :: BanCount( QString server )
{
	quint32 Count = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT COUNT(*) FROM bans WHERE server=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
			Count = sqlite3_column_int( Statement, 0 );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error counting bans [" + server + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error counting bans [" + server + "] - " + m_DB->GetError( ) );

	return Count;
}

CDBBan *CGHostDBSQLite :: BanCheck( QString server, QString user, QString ip )
{
	user = user.toLower();
	CDBBan *Ban = NULL;
	sqlite3_stmt *Statement;

	if( ip.isEmpty( ) )
		m_DB->Prepare( "SELECT name, ip, date, gamename, admin, reason FROM bans WHERE server=? AND name=?", (void **)&Statement );
	else
		m_DB->Prepare( "SELECT name, ip, date, gamename, admin, reason FROM bans WHERE (server=? AND name=?) OR ip=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		if( !ip.isEmpty( ) )
			sqlite3_bind_text( Statement, 3, ip.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
		{
			QList<QString> *Row = m_DB->GetRow( );

			if( Row->size( ) == 6 )
				Ban = new CDBBan( server, (*Row)[0], (*Row)[1], (*Row)[2], (*Row)[3], (*Row)[4], (*Row)[5] );
			else
				CONSOLE_Print( "[SQLITE3] error checking ban [" + server + " : " + user + " : " + ip + "] - row doesn't have 6 columns" );
		}
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error checking ban [" + server + " : " + user + " : " + ip + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error checking ban [" + server + " : " + user + " : " + ip + "] - " + m_DB->GetError( ) );

	return Ban;
}

bool CGHostDBSQLite :: BanAdd( QString server, QString user, QString ip, QString gamename, QString admin, QString reason )
{
	user = user.toLower();
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO bans ( server, name, ip, date, gamename, admin, reason ) VALUES ( ?, ?, ?, date('now'), ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 3, ip.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 4, gamename.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 5, admin.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 6, reason.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding ban [" + server + " : " + user + " : " + ip + " : " + gamename + " : " + admin + " : " + reason + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding ban [" + server + " : " + user + " : " + ip + " : " + gamename + " : " + admin + " : " + reason + "] - " + m_DB->GetError( ) );

	return Success;
}

bool CGHostDBSQLite :: BanRemove( QString server, QString user )
{
	user = user.toLower();
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "DELETE FROM bans WHERE server=? AND name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error removing ban [" + server + " : " + user + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error removing ban [" + server + " : " + user + "] - " + m_DB->GetError( ) );

	return Success;
}

bool CGHostDBSQLite :: BanRemove( QString user )
{
	user = user.toLower();
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "DELETE FROM bans WHERE name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, user.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error removing ban [" + user + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error removing ban [" + user + "] - " + m_DB->GetError( ) );

	return Success;
}

QList<CDBBan *> CGHostDBSQLite :: BanList( QString server )
{
	QList<CDBBan *> BanList;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT name, ip, date, gamename, admin, reason FROM bans WHERE server=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		while( RC == SQLITE_ROW )
		{
			QList<QString> *Row = m_DB->GetRow( );

			if( Row->size( ) == 6 )
				BanList.push_back( new CDBBan( server, (*Row)[0], (*Row)[1], (*Row)[2], (*Row)[3], (*Row)[4], (*Row)[5] ) );

			RC = m_DB->Step( Statement );
		}

		if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error retrieving ban list [" + server + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error retrieving ban list [" + server + "] - " + m_DB->GetError( ) );

	return BanList;
}

quint32 CGHostDBSQLite :: GameAdd( QString server, QString map, QString gamename, QString ownername, quint32 duration, quint32 gamestate, QString creatorname, QString creatorserver )
{
	quint32 RowID = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO games ( server, map, datetime, gamename, ownername, duration, gamestate, creatorname, creatorserver ) VALUES ( ?, ?, datetime('now'), ?, ?, ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, server.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 2, map.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 3, gamename.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 4, ownername.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 5, duration );
		sqlite3_bind_int( Statement, 6, gamestate );
		sqlite3_bind_text( Statement, 7, creatorname.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 8, creatorserver.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			RowID = m_DB->LastRowID( );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding game [" + server + " : " + map + " : " + gamename + " : " + ownername + " : " + QString::number( duration ) + " : " + QString::number( gamestate ) + " : " + creatorname + " : " + creatorserver + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding game [" + server + " : " + map + " : " + gamename + " : " + ownername + " : " + QString::number( duration ) + " : " + QString::number( gamestate ) + " : " + creatorname + " : " + creatorserver + "] - " + m_DB->GetError( ) );

	return RowID;
}

quint32 CGHostDBSQLite :: GamePlayerAdd( quint32 gameid, QString name, QString ip, quint32 spoofed, QString spoofedrealm, quint32 reserved, quint32 loadingtime, quint32 left, QString leftreason, quint32 team, quint32 colour )
{
	name = name.toLower();
	quint32 RowID = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO gameplayers ( gameid, name, ip, spoofed, reserved, loadingtime, left, leftreason, team, colour, spoofedrealm ) VALUES ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_int( Statement, 1, gameid );
		sqlite3_bind_text( Statement, 2, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 3, ip.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 4, spoofed );
		sqlite3_bind_int( Statement, 5, reserved );
		sqlite3_bind_int( Statement, 6, loadingtime );
		sqlite3_bind_int( Statement, 7, left );
		sqlite3_bind_text( Statement, 8, leftreason.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 9, team );
		sqlite3_bind_int( Statement, 10, colour );
		sqlite3_bind_text( Statement, 11, spoofedrealm.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			RowID = m_DB->LastRowID( );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding gameplayer [" + QString::number( gameid ) + " : " + name + " : " + ip + " : " + QString::number( spoofed ) + " : " + spoofedrealm + " : " + QString::number( reserved ) + " : " + QString::number( loadingtime ) + " : " + QString::number( left ) + " : " + leftreason + " : " + QString::number( team ) + " : " + QString::number( colour ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding gameplayer [" + QString::number( gameid ) + " : " + name + " : " + ip + " : " + QString::number( spoofed ) + " : " + spoofedrealm + " : " + QString::number( reserved ) + " : " + QString::number( loadingtime ) + " : " + QString::number( left ) + " : " + leftreason + " : " + QString::number( team ) + " : " + QString::number( colour ) + "] - " + m_DB->GetError( ) );

	return RowID;
}

quint32 CGHostDBSQLite :: GamePlayerCount( QString name )
{
	name = name.toLower();
	quint32 Count = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT COUNT(*) FROM gameplayers LEFT JOIN games ON games.id=gameid WHERE name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
			Count = sqlite3_column_int( Statement, 0 );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error counting gameplayers [" + name + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error counting gameplayers [" + name + "] - " + m_DB->GetError( ) );

	return Count;
}

CDBGamePlayerSummary *CGHostDBSQLite :: GamePlayerSummaryCheck( QString name )
{
	if( GamePlayerCount( name ) == 0 )
		return NULL;

	name = name.toLower();
	CDBGamePlayerSummary *GamePlayerSummary = NULL;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT MIN(datetime), MAX(datetime), COUNT(*), MIN(loadingtime), AVG(loadingtime), MAX(loadingtime), MIN(left/CAST(duration AS REAL))*100, AVG(left/CAST(duration AS REAL))*100, MAX(left/CAST(duration AS REAL))*100, MIN(duration), AVG(duration), MAX(duration) FROM gameplayers LEFT JOIN games ON games.id=gameid WHERE name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
		{
			if( sqlite3_column_count( (sqlite3_stmt *)Statement ) == 12 )
			{
				char *First = (char *)sqlite3_column_text( (sqlite3_stmt *)Statement, 0 );
				char *Last = (char *)sqlite3_column_text( (sqlite3_stmt *)Statement, 1 );
				QString FirstGameDateTime;
				QString LastGameDateTime;

				if( First )
					FirstGameDateTime = First;

				if( Last )
					LastGameDateTime = Last;

				quint32 TotalGames = sqlite3_column_int( (sqlite3_stmt *)Statement, 2 );
				quint32 MinLoadingTime = sqlite3_column_int( (sqlite3_stmt *)Statement, 3 );
				quint32 AvgLoadingTime = sqlite3_column_int( (sqlite3_stmt *)Statement, 4 );
				quint32 MaxLoadingTime = sqlite3_column_int( (sqlite3_stmt *)Statement, 5 );
				quint32 MinLeftPercent = sqlite3_column_int( (sqlite3_stmt *)Statement, 6 );
				quint32 AvgLeftPercent = sqlite3_column_int( (sqlite3_stmt *)Statement, 7 );
				quint32 MaxLeftPercent = sqlite3_column_int( (sqlite3_stmt *)Statement, 8 );
				quint32 MinDuration = sqlite3_column_int( (sqlite3_stmt *)Statement, 9 );
				quint32 AvgDuration = sqlite3_column_int( (sqlite3_stmt *)Statement, 10 );
				quint32 MaxDuration = sqlite3_column_int( (sqlite3_stmt *)Statement, 11 );
				GamePlayerSummary = new CDBGamePlayerSummary( QString( ), name, FirstGameDateTime, LastGameDateTime, TotalGames, MinLoadingTime, AvgLoadingTime, MaxLoadingTime, MinLeftPercent, AvgLeftPercent, MaxLeftPercent, MinDuration, AvgDuration, MaxDuration );
			}
			else
				CONSOLE_Print( "[SQLITE3] error checking gameplayersummary [" + name + "] - row doesn't have 12 columns" );
		}
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error checking gameplayersummary [" + name + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error checking gameplayersummary [" + name + "] - " + m_DB->GetError( ) );

	return GamePlayerSummary;
}

quint32 CGHostDBSQLite :: DotAGameAdd( quint32 gameid, quint32 winner, quint32 min, quint32 sec )
{
	quint32 RowID = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO dotagames ( gameid, winner, min, sec ) VALUES ( ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_int( Statement, 1, gameid );
		sqlite3_bind_int( Statement, 2, winner );
		sqlite3_bind_int( Statement, 3, min );
		sqlite3_bind_int( Statement, 4, sec );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			RowID = m_DB->LastRowID( );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding dotagame [" + QString::number( gameid ) + " : " + QString::number( winner ) + " : " + QString::number( min ) + " : " + QString::number( sec ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding dotagame [" + QString::number( gameid ) + " : " + QString::number( winner ) + " : " + QString::number( min ) + " : " + QString::number( sec ) + "] - " + m_DB->GetError( ) );

	return RowID;
}

quint32 CGHostDBSQLite :: DotAPlayerAdd( quint32 gameid, quint32 colour, quint32 kills, quint32 deaths, quint32 creepkills, quint32 creepdenies, quint32 assists, quint32 gold, quint32 neutralkills, QString item1, QString item2, QString item3, QString item4, QString item5, QString item6, QString hero, quint32 newcolour, quint32 towerkills, quint32 raxkills, quint32 courierkills )
{
	quint32 RowID = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO dotaplayers ( gameid, colour, kills, deaths, creepkills, creepdenies, assists, gold, neutralkills, item1, item2, item3, item4, item5, item6, hero, newcolour, towerkills, raxkills, courierkills ) VALUES ( ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_int( Statement, 1, gameid );
		sqlite3_bind_int( Statement, 2, colour );
		sqlite3_bind_int( Statement, 3, kills );
		sqlite3_bind_int( Statement, 4, deaths );
		sqlite3_bind_int( Statement, 5, creepkills );
		sqlite3_bind_int( Statement, 6, creepdenies );
		sqlite3_bind_int( Statement, 7, assists );
		sqlite3_bind_int( Statement, 8, gold );
		sqlite3_bind_int( Statement, 9, neutralkills );
		sqlite3_bind_text( Statement, 10, item1.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 11, item2.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 12, item3.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 13, item4.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 14, item5.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 15, item6.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 16, hero.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 17, newcolour );
		sqlite3_bind_int( Statement, 18, towerkills );
		sqlite3_bind_int( Statement, 19, raxkills );
		sqlite3_bind_int( Statement, 20, courierkills );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			RowID = m_DB->LastRowID( );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding dotaplayer [" + QString::number( gameid ) + " : " + QString::number( colour ) + " : " + QString::number( kills ) + " : " + QString::number( deaths ) + " : " + QString::number( creepkills ) + " : " + QString::number( creepdenies ) + " : " + QString::number( assists ) + " : " + QString::number( gold ) + " : " + QString::number( neutralkills ) + " : " + item1 + " : " + item2 + " : " + item3 + " : " + item4 + " : " + item5 + " : " + item6 + " : " + hero + " : " + QString::number( newcolour ) + " : " + QString::number( towerkills ) + " : " + QString::number( raxkills ) + " : " + QString::number( courierkills ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding dotaplayer [" + QString::number( gameid ) + " : " + QString::number( colour ) + " : " + QString::number( kills ) + " : " + QString::number( deaths ) + " : " + QString::number( creepkills ) + " : " + QString::number( creepdenies ) + " : " + QString::number( assists ) + " : " + QString::number( gold ) + " : " + QString::number( neutralkills ) + " : " + item1 + " : " + item2 + " : " + item3 + " : " + item4 + " : " + item5 + " : " + item6 + " : " + hero + " : " + QString::number( newcolour ) + " : " + QString::number( towerkills ) + " : " + QString::number( raxkills ) + " : " + QString::number( courierkills ) + "] - " + m_DB->GetError( ) );

	return RowID;
}

quint32 CGHostDBSQLite :: DotAPlayerCount( QString name )
{
	name = name.toLower();
	quint32 Count = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT COUNT(dotaplayers.id) FROM gameplayers LEFT JOIN games ON games.id=gameplayers.gameid LEFT JOIN dotaplayers ON dotaplayers.gameid=games.id AND dotaplayers.colour=gameplayers.colour WHERE name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
			Count = sqlite3_column_int( Statement, 0 );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error counting dotaplayers [" + name + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error counting dotaplayers [" + name + "] - " + m_DB->GetError( ) );

	return Count;
}

CDBDotAPlayerSummary *CGHostDBSQLite :: DotAPlayerSummaryCheck( QString name )
{
	if( DotAPlayerCount( name ) == 0 )
		return NULL;

	name = name.toLower();
	CDBDotAPlayerSummary *DotAPlayerSummary = NULL;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT COUNT(dotaplayers.id), SUM(kills), SUM(deaths), SUM(creepkills), SUM(creepdenies), SUM(assists), SUM(neutralkills), SUM(towerkills), SUM(raxkills), SUM(courierkills) FROM gameplayers LEFT JOIN games ON games.id=gameplayers.gameid LEFT JOIN dotaplayers ON dotaplayers.gameid=games.id AND dotaplayers.colour=gameplayers.colour WHERE name=?", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
		{
			if( sqlite3_column_count( (sqlite3_stmt *)Statement ) == 10 )
			{
				quint32 TotalGames = sqlite3_column_int( (sqlite3_stmt *)Statement, 0 );
				quint32 TotalWins = 0;
				quint32 TotalLosses = 0;
				quint32 TotalKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 1 );
				quint32 TotalDeaths = sqlite3_column_int( (sqlite3_stmt *)Statement, 2 );
				quint32 TotalCreepKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 3 );
				quint32 TotalCreepDenies = sqlite3_column_int( (sqlite3_stmt *)Statement, 4 );
				quint32 TotalAssists = sqlite3_column_int( (sqlite3_stmt *)Statement, 5 );
				quint32 TotalNeutralKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 6 );
				quint32 TotalTowerKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 7 );
				quint32 TotalRaxKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 8 );
				quint32 TotalCourierKills = sqlite3_column_int( (sqlite3_stmt *)Statement, 9 );

				// calculate total wins

				sqlite3_stmt *Statement2;
				m_DB->Prepare( "SELECT COUNT(*) FROM gameplayers LEFT JOIN games ON games.id=gameplayers.gameid LEFT JOIN dotaplayers ON dotaplayers.gameid=games.id AND dotaplayers.colour=gameplayers.colour LEFT JOIN dotagames ON games.id=dotagames.gameid WHERE name=? AND ((winner=1 AND dotaplayers.newcolour>=1 AND dotaplayers.newcolour<=5) OR (winner=2 AND dotaplayers.newcolour>=7 AND dotaplayers.newcolour<=11))", (void **)&Statement2 );

				if( Statement2 )
				{
					sqlite3_bind_text( Statement2, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
					int RC2 = m_DB->Step( Statement2 );

					if( RC2 == SQLITE_ROW )
						TotalWins = sqlite3_column_int( Statement2, 0 );
					else if( RC2 == SQLITE_ERROR )
						CONSOLE_Print( "[SQLITE3] error counting dotaplayersummary wins [" + name + "] - " + m_DB->GetError( ) );

					m_DB->Finalize( Statement2 );
				}
				else
					CONSOLE_Print( "[SQLITE3] prepare error counting dotaplayersummary wins [" + name + "] - " + m_DB->GetError( ) );

				// calculate total losses

				sqlite3_stmt *Statement3;
				m_DB->Prepare( "SELECT COUNT(*) FROM gameplayers LEFT JOIN games ON games.id=gameplayers.gameid LEFT JOIN dotaplayers ON dotaplayers.gameid=games.id AND dotaplayers.colour=gameplayers.colour LEFT JOIN dotagames ON games.id=dotagames.gameid WHERE name=? AND ((winner=2 AND dotaplayers.newcolour>=1 AND dotaplayers.newcolour<=5) OR (winner=1 AND dotaplayers.newcolour>=7 AND dotaplayers.newcolour<=11))", (void **)&Statement3 );

				if( Statement3 )
				{
					sqlite3_bind_text( Statement3, 1, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
					int RC3 = m_DB->Step( Statement3 );

					if( RC3 == SQLITE_ROW )
						TotalLosses = sqlite3_column_int( Statement3, 0 );
					else if( RC3 == SQLITE_ERROR )
						CONSOLE_Print( "[SQLITE3] error counting dotaplayersummary losses [" + name + "] - " + m_DB->GetError( ) );

					m_DB->Finalize( Statement3 );
				}
				else
					CONSOLE_Print( "[SQLITE3] prepare error counting dotaplayersummary losses [" + name + "] - " + m_DB->GetError( ) );

				// done

				DotAPlayerSummary = new CDBDotAPlayerSummary( QString( ), name, TotalGames, TotalWins, TotalLosses, TotalKills, TotalDeaths, TotalCreepKills, TotalCreepDenies, TotalAssists, TotalNeutralKills, TotalTowerKills, TotalRaxKills, TotalCourierKills );
			}
			else
				CONSOLE_Print( "[SQLITE3] error checking dotaplayersummary [" + name + "] - row doesn't have 7 columns" );
		}
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error checking dotaplayersummary [" + name + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error checking dotaplayersummary [" + name + "] - " + m_DB->GetError( ) );

	return DotAPlayerSummary;
}

QString CGHostDBSQLite :: FromCheck( quint32 ip )
{
	// a big thank you to tjado for help with the iptocountry feature

	QString From = "??";
	sqlite3_stmt *Statement;
	m_DB->Prepare( "SELECT country FROM iptocountry WHERE ip1<=? AND ip2>=?", (void **)&Statement );

	if( Statement )
	{
		// we bind the ip as an int64 because SQLite treats it as signed

		sqlite3_bind_int64( Statement, 1, ip );
		sqlite3_bind_int64( Statement, 2, ip );
		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_ROW )
		{
			QList<QString> *Row = m_DB->GetRow( );

			if( Row->size( ) == 1 )
				From = (*Row)[0];
			else
				CONSOLE_Print( "[SQLITE3] error checking iptocountry [" + QString::number( ip ) + "] - row doesn't have 1 column" );
		}
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error checking iptocountry [" + QString::number( ip ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error checking iptocountry [" + QString::number( ip ) + "] - " + m_DB->GetError( ) );

	return From;
}

bool CGHostDBSQLite :: FromAdd( quint32 ip1, quint32 ip2, QString country )
{
	// a big thank you to tjado for help with the iptocountry feature

	bool Success = false;

	if( !FromAddStmt )
		m_DB->Prepare( "INSERT INTO iptocountry VALUES ( ?, ?, ? )", (void **)&FromAddStmt );

	if( FromAddStmt )
	{
		// we bind the ip as an int64 because SQLite treats it as signed

		sqlite3_bind_int64( (sqlite3_stmt *)FromAddStmt, 1, ip1 );
		sqlite3_bind_int64( (sqlite3_stmt *)FromAddStmt, 2, ip2 );
		sqlite3_bind_text( (sqlite3_stmt *)FromAddStmt, 3, country.toUtf8().data( ), -1, SQLITE_TRANSIENT );

		int RC = m_DB->Step( FromAddStmt );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding iptocountry [" + QString::number( ip1 ) + " : " + QString::number( ip2 ) + " : " + country + "] - " + m_DB->GetError( ) );

		m_DB->Reset( FromAddStmt );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding iptocountry [" + QString::number( ip1 ) + " : " + QString::number( ip2 ) + " : " + country + "] - " + m_DB->GetError( ) );

	return Success;
}

bool CGHostDBSQLite :: DownloadAdd( QString map, quint32 mapsize, QString name, QString ip, quint32 spoofed, QString spoofedrealm, quint32 downloadtime )
{
	bool Success = false;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO downloads ( map, mapsize, datetime, name, ip, spoofed, spoofedrealm, downloadtime ) VALUES ( ?, ?, datetime('now'), ?, ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, map.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 2, mapsize );
		sqlite3_bind_text( Statement, 3, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 4, ip.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 5, spoofed );
		sqlite3_bind_text( Statement, 6, spoofedrealm.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 7, downloadtime );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			Success = true;
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding download [" + map + " : " + QString::number( mapsize ) + " : " + name + " : " + ip + " : " + QString::number( spoofed ) + " : " + spoofedrealm + " : " + QString::number( downloadtime ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding download [" + map + " : " + QString::number( mapsize ) + " : " + name + " : " + ip + " : " + QString::number( spoofed ) + " : " + spoofedrealm + " : " + QString::number( downloadtime ) + "] - " + m_DB->GetError( ) );

	return Success;
}

quint32 CGHostDBSQLite :: W3MMDPlayerAdd( QString category, quint32 gameid, quint32 pid, QString name, QString flag, quint32 leaver, quint32 practicing )
{
	quint32 RowID = 0;
	sqlite3_stmt *Statement;
	m_DB->Prepare( "INSERT INTO w3mmdplayers ( category, gameid, pid, name, flag, leaver, practicing ) VALUES ( ?, ?, ?, ?, ?, ?, ? )", (void **)&Statement );

	if( Statement )
	{
		sqlite3_bind_text( Statement, 1, category.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 2, gameid );
		sqlite3_bind_int( Statement, 3, pid );
		sqlite3_bind_text( Statement, 4, name.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_text( Statement, 5, flag.toUtf8().data( ), -1, SQLITE_TRANSIENT );
		sqlite3_bind_int( Statement, 6, leaver );
		sqlite3_bind_int( Statement, 7, practicing );

		int RC = m_DB->Step( Statement );

		if( RC == SQLITE_DONE )
			RowID = m_DB->LastRowID( );
		else if( RC == SQLITE_ERROR )
			CONSOLE_Print( "[SQLITE3] error adding w3mmdplayer [" + category + " : " + QString::number( gameid ) + " : " + QString::number( pid ) + " : " + name + " : " + flag + " : " + QString::number( leaver ) + " : " + QString::number( practicing ) + "] - " + m_DB->GetError( ) );

		m_DB->Finalize( Statement );
	}
	else
		CONSOLE_Print( "[SQLITE3] prepare error adding w3mmdplayer [" + category + " : " + QString::number( gameid ) + " : " + QString::number( pid ) + " : " + name + " : " + flag + " : " + QString::number( leaver ) + " : " + QString::number( practicing ) + "] - " + m_DB->GetError( ) );

	return RowID;
}

bool CGHostDBSQLite :: W3MMDVarAdd( quint32 gameid, QMap<VarP,quint32> var_ints )
{
	if( var_ints.isEmpty( ) )
		return false;

	bool Success = true;
	sqlite3_stmt *Statement = NULL;

	for( QMap<VarP,quint32> :: const_iterator i = var_ints.begin( ); i != var_ints.end( ); i++ )
	{
		if( !Statement )
			m_DB->Prepare( "INSERT INTO w3mmdvars ( gameid, pid, varname, value_int ) VALUES ( ?, ?, ?, ? )", (void **)&Statement );

		if( Statement )
		{
			sqlite3_bind_int( Statement, 1, gameid );
			sqlite3_bind_int( Statement, 2, i.key().first );
			sqlite3_bind_text( Statement, 3, (char*)i.key().second.data( ), -1, SQLITE_TRANSIENT );
			sqlite3_bind_int( Statement, 4, i.value() );

			int RC = m_DB->Step( Statement );

			if( RC == SQLITE_ERROR )
			{
				Success = false;
				CONSOLE_Print( "[SQLITE3] error adding w3mmdvar-int [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + QString::number( i.value() ) + "] - " + m_DB->GetError( ) );
				break;
			}

			m_DB->Reset( Statement );
		}
		else
		{
			Success = false;
			CONSOLE_Print( "[SQLITE3] prepare error adding w3mmdvar-int [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + QString::number( i.value() ) + "] - " + m_DB->GetError( ) );
			break;
		}
	}

	if( Statement )
		m_DB->Finalize( Statement );

	return Success;
}

bool CGHostDBSQLite :: W3MMDVarAdd( quint32 gameid, QMap<VarP,double> var_reals )
{
	if( var_reals.isEmpty( ) )
		return false;

	bool Success = true;
	sqlite3_stmt *Statement = NULL;

	for( QMap<VarP,double> :: const_iterator i = var_reals.begin( ); i != var_reals.end( ); i++ )
	{
		if( !Statement )
			m_DB->Prepare( "INSERT INTO w3mmdvars ( gameid, pid, varname, value_real ) VALUES ( ?, ?, ?, ? )", (void **)&Statement );

		if( Statement )
		{
			sqlite3_bind_int( Statement, 1, gameid );
			sqlite3_bind_int( Statement, 2, i.key().first );
			sqlite3_bind_text( Statement, 3, (char*)i.key().second.data( ), -1, SQLITE_TRANSIENT );
			sqlite3_bind_double( Statement, 4, i.value() );

			int RC = m_DB->Step( Statement );

			if( RC == SQLITE_ERROR )
			{
				Success = false;
				CONSOLE_Print( "[SQLITE3] error adding w3mmdvar-real [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + QString::number( i.value(), 'g', 10 ) + "] - " + m_DB->GetError( ) );
				break;
			}

			m_DB->Reset( Statement );
		}
		else
		{
			Success = false;
			CONSOLE_Print( "[SQLITE3] prepare error adding w3mmdvar-real [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + QString::number( i.value(), 'g', 10 ) + "] - " + m_DB->GetError( ) );
			break;
		}
	}

	if( Statement )
		m_DB->Finalize( Statement );

	return Success;
}

bool CGHostDBSQLite :: W3MMDVarAdd( quint32 gameid, QMap<VarP,QString> var_strings )
{
	if( var_strings.isEmpty( ) )
		return false;

	bool Success = true;
	sqlite3_stmt *Statement = NULL;

	for( QMap<VarP,QString> :: const_iterator i = var_strings.begin( ); i != var_strings.end( ); i++ )
	{
		if( !Statement )
			m_DB->Prepare( "INSERT INTO w3mmdvars ( gameid, pid, varname, value_QString ) VALUES ( ?, ?, ?, ? )", (void **)&Statement );

		if( Statement )
		{
			sqlite3_bind_int( Statement, 1, gameid );
			sqlite3_bind_int( Statement, 2, i.key().first );
			sqlite3_bind_text( Statement, 3, (char*)i.key().second.data( ), -1, SQLITE_TRANSIENT );
			sqlite3_bind_text( Statement, 4, (char*)i.value().data( ), -1, SQLITE_TRANSIENT );

			int RC = m_DB->Step( Statement );

			if( RC == SQLITE_ERROR )
			{
				Success = false;
				CONSOLE_Print( "[SQLITE3] error adding w3mmdvar-QString [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + i.value() + "] - " + m_DB->GetError( ) );
				break;
			}

			m_DB->Reset( Statement );
		}
		else
		{
			Success = false;
			CONSOLE_Print( "[SQLITE3] prepare error adding w3mmdvar-QString [" + QString::number( gameid ) + " : " + QString::number( i.key().first ) + " : " + i.key().second + " : " + i.value() + "] - " + m_DB->GetError( ) );
			break;
		}
	}

	if( Statement )
		m_DB->Finalize( Statement );

	return Success;
}

CCallableAdminCount *CGHostDBSQLite :: ThreadedAdminCount( QString server )
{
	CCallableAdminCount *Callable = new CCallableAdminCount( server );
	Callable->SetResult( AdminCount( server ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableAdminCheck *CGHostDBSQLite :: ThreadedAdminCheck( QString server, QString user )
{
	CCallableAdminCheck *Callable = new CCallableAdminCheck( server, user );
	Callable->SetResult( AdminCheck( server, user ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableAdminAdd *CGHostDBSQLite :: ThreadedAdminAdd( QString server, QString user )
{
	CCallableAdminAdd *Callable = new CCallableAdminAdd( server, user );
	Callable->SetResult( AdminAdd( server, user ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableAdminRemove *CGHostDBSQLite :: ThreadedAdminRemove( QString server, QString user )
{
	CCallableAdminRemove *Callable = new CCallableAdminRemove( server, user );
	Callable->SetResult( AdminRemove( server, user ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableAdminList *CGHostDBSQLite :: ThreadedAdminList( QString server )
{
	CCallableAdminList *Callable = new CCallableAdminList( server );
	Callable->SetResult( AdminList( server ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanCount *CGHostDBSQLite :: ThreadedBanCount( QString server )
{
	CCallableBanCount *Callable = new CCallableBanCount( server );
	Callable->SetResult( BanCount( server ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanCheck *CGHostDBSQLite :: ThreadedBanCheck( QString server, QString user, QString ip )
{
	CCallableBanCheck *Callable = new CCallableBanCheck( server, user, ip );
	Callable->SetResult( BanCheck( server, user, ip ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanAdd *CGHostDBSQLite :: ThreadedBanAdd( QString server, QString user, QString ip, QString gamename, QString admin, QString reason )
{
	CCallableBanAdd *Callable = new CCallableBanAdd( server, user, ip, gamename, admin, reason );
	Callable->SetResult( BanAdd( server, user, ip, gamename, admin, reason ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanRemove *CGHostDBSQLite :: ThreadedBanRemove( QString server, QString user )
{
	CCallableBanRemove *Callable = new CCallableBanRemove( server, user );
	Callable->SetResult( BanRemove( server, user ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanRemove *CGHostDBSQLite :: ThreadedBanRemove( QString user )
{
	CCallableBanRemove *Callable = new CCallableBanRemove( QString( ), user );
	Callable->SetResult( BanRemove( user ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableBanList *CGHostDBSQLite :: ThreadedBanList( QString server )
{
	CCallableBanList *Callable = new CCallableBanList( server );
	Callable->SetResult( BanList( server ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableGameAdd *CGHostDBSQLite :: ThreadedGameAdd( QString server, QString map, QString gamename, QString ownername, quint32 duration, quint32 gamestate, QString creatorname, QString creatorserver )
{
	CCallableGameAdd *Callable = new CCallableGameAdd( server, map, gamename, ownername, duration, gamestate, creatorname, creatorserver );
	Callable->SetResult( GameAdd( server, map, gamename, ownername, duration, gamestate, creatorname, creatorserver ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableGamePlayerAdd *CGHostDBSQLite :: ThreadedGamePlayerAdd( quint32 gameid, QString name, QString ip, quint32 spoofed, QString spoofedrealm, quint32 reserved, quint32 loadingtime, quint32 left, QString leftreason, quint32 team, quint32 colour )
{
	CCallableGamePlayerAdd *Callable = new CCallableGamePlayerAdd( gameid, name, ip, spoofed, spoofedrealm, reserved, loadingtime, left, leftreason, team, colour );
	Callable->SetResult( GamePlayerAdd( gameid, name, ip, spoofed, spoofedrealm, reserved, loadingtime, left, leftreason, team, colour ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableGamePlayerSummaryCheck *CGHostDBSQLite :: ThreadedGamePlayerSummaryCheck( QString name )
{
	CCallableGamePlayerSummaryCheck *Callable = new CCallableGamePlayerSummaryCheck( name );
	Callable->SetResult( GamePlayerSummaryCheck( name ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableDotAGameAdd *CGHostDBSQLite :: ThreadedDotAGameAdd( quint32 gameid, quint32 winner, quint32 min, quint32 sec )
{
	CCallableDotAGameAdd *Callable = new CCallableDotAGameAdd( gameid, winner, min, sec );
	Callable->SetResult( DotAGameAdd( gameid, winner, min, sec ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableDotAPlayerAdd *CGHostDBSQLite :: ThreadedDotAPlayerAdd( quint32 gameid, quint32 colour, quint32 kills, quint32 deaths, quint32 creepkills, quint32 creepdenies, quint32 assists, quint32 gold, quint32 neutralkills, QString item1, QString item2, QString item3, QString item4, QString item5, QString item6, QString hero, quint32 newcolour, quint32 towerkills, quint32 raxkills, quint32 courierkills )
{
	CCallableDotAPlayerAdd *Callable = new CCallableDotAPlayerAdd( gameid, colour, kills, deaths, creepkills, creepdenies, assists, gold, neutralkills, item1, item2, item3, item4, item5, item6, hero, newcolour, towerkills, raxkills, courierkills );
	Callable->SetResult( DotAPlayerAdd( gameid, colour, kills, deaths, creepkills, creepdenies, assists, gold, neutralkills, item1, item2, item3, item4, item5, item6, hero, newcolour, towerkills, raxkills, courierkills ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableDotAPlayerSummaryCheck *CGHostDBSQLite :: ThreadedDotAPlayerSummaryCheck( QString name )
{
	CCallableDotAPlayerSummaryCheck *Callable = new CCallableDotAPlayerSummaryCheck( name );
	Callable->SetResult( DotAPlayerSummaryCheck( name ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableDownloadAdd *CGHostDBSQLite :: ThreadedDownloadAdd( QString map, quint32 mapsize, QString name, QString ip, quint32 spoofed, QString spoofedrealm, quint32 downloadtime )
{
	CCallableDownloadAdd *Callable = new CCallableDownloadAdd( map, mapsize, name, ip, spoofed, spoofedrealm, downloadtime );
	Callable->SetResult( DownloadAdd( map, mapsize, name, ip, spoofed, spoofedrealm, downloadtime ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableW3MMDPlayerAdd *CGHostDBSQLite :: ThreadedW3MMDPlayerAdd( QString category, quint32 gameid, quint32 pid, QString name, QString flag, quint32 leaver, quint32 practicing )
{
	CCallableW3MMDPlayerAdd *Callable = new CCallableW3MMDPlayerAdd( category, gameid, pid, name, flag, leaver, practicing );
	Callable->SetResult( W3MMDPlayerAdd( category, gameid, pid, name, flag, leaver, practicing ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBSQLite :: ThreadedW3MMDVarAdd( quint32 gameid, QMap<VarP,quint32> var_ints )
{
	CCallableW3MMDVarAdd *Callable = new CCallableW3MMDVarAdd( gameid, var_ints );
	Callable->SetResult( W3MMDVarAdd( gameid, var_ints ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBSQLite :: ThreadedW3MMDVarAdd( quint32 gameid, QMap<VarP,double> var_reals )
{
	CCallableW3MMDVarAdd *Callable = new CCallableW3MMDVarAdd( gameid, var_reals );
	Callable->SetResult( W3MMDVarAdd( gameid, var_reals ) );
	Callable->SetReady( true );
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBSQLite :: ThreadedW3MMDVarAdd( quint32 gameid, QMap<VarP,QString> var_strings )
{
	CCallableW3MMDVarAdd *Callable = new CCallableW3MMDVarAdd( gameid, var_strings );
	Callable->SetResult( W3MMDVarAdd( gameid, var_strings ) );
	Callable->SetReady( true );
	return Callable;
}
