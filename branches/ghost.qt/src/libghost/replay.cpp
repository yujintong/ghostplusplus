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
#include "packed.h"
#include "replay.h"
#include "gameprotocol.h"

//
// CReplay
//

CReplay :: CReplay( ) : CPacked( )
{
	m_HostPID = 0;
	m_PlayerCount = 0;
	m_MapGameType = 0;
	m_RandomSeed = 0;
	m_SelectMode = 0;
	m_StartSpotCount = 0;
	m_CompiledBlocks.reserve( 262144 );
}

CReplay :: ~CReplay( )
{

}

void CReplay :: AddLeaveGame( quint32 reason, unsigned char PID, quint32 result )
{
	QByteArray Block;
	Block.push_back( REPLAY_LEAVEGAME );
	Block.append(Util::fromUInt32(reason));
	Block.push_back( PID );
	Block.append(Util::fromUInt32(result));
	Block.append(Util::fromUInt32(1));
	m_CompiledBlocks += Block;
}

void CReplay :: AddLeaveGameDuringLoading( quint32 reason, unsigned char PID, quint32 result )
{
	QByteArray Block;
	Block.push_back( REPLAY_LEAVEGAME );
	Block.append(Util::fromUInt32(reason));
	Block.push_back( PID );
	Block.append(Util::fromUInt32(result));
	Block.append(Util::fromUInt32(1));
	m_LoadingBlocks.enqueue( Block );
}

void CReplay :: AddTimeSlot2( QQueue<CIncomingAction *> actions )
{
	QByteArray Block;
	Block.push_back( REPLAY_TIMESLOT2 );
	Block.append(Util::fromUInt16(0));
	Block.append(Util::fromUInt16(0));

	while( !actions.isEmpty( ) )
	{
		CIncomingAction *Action = actions.front( );
		actions.dequeue( );
		Block.push_back( Action->GetPID( ) );
		Block.append(Util::fromUInt16(Action->GetAction( ).size( )));
		Block.append(Action->GetAction( ));
	}

	// assign length

	QByteArray LengthBytes = Util::fromUInt16( Block.size( ) - 3 );
	Block[1] = LengthBytes[0];
	Block[2] = LengthBytes[1];
	m_CompiledBlocks += Block;
}

void CReplay :: AddTimeSlot( quint16 timeIncrement, QQueue<CIncomingAction *> actions )
{
	QByteArray Block;
	Block.push_back( REPLAY_TIMESLOT );
	Block.append(Util::fromUInt16(0));
	Block.append(Util::fromUInt16(timeIncrement));

	while( !actions.isEmpty( ) )
	{
		CIncomingAction *Action = actions.front( );
		actions.dequeue( );
		Block.push_back( Action->GetPID( ) );
		Block.append(Util::fromUInt16(Action->GetAction( ).size( )));
		Block.append(Action->GetAction( ));
	}

	// assign length

	QByteArray LengthBytes = Util::fromUInt16( Block.size( ) - 3 );
	Block[1] = LengthBytes[0];
	Block[2] = LengthBytes[1];
	m_CompiledBlocks += Block;
	m_ReplayLength += timeIncrement;
}

void CReplay :: AddChatMessage( unsigned char PID, unsigned char flags, quint32 chatMode, QString message )
{
	QByteArray Block;
	Block.push_back( REPLAY_CHATMESSAGE );
	Block.push_back( PID );
	Block.append(Util::fromUInt16(0));
	Block.push_back( flags );
	Block.append(Util::fromUInt32(chatMode));
	Block.append(message);

	// assign length

	QByteArray LengthBytes = Util::fromUInt16( Block.size( ) - 4 );
	Block[2] = LengthBytes[0];
	Block[3] = LengthBytes[1];
	m_CompiledBlocks += Block;
}

void CReplay :: AddLoadingBlock( QByteArray &loadingBlock )
{
	m_LoadingBlocks.enqueue( loadingBlock );
}

void CReplay :: BuildReplay( QString gameName, QString statString, quint32 war3Version, quint16 buildNumber )
{
	m_War3Version = war3Version;
	m_BuildNumber = buildNumber;
	m_Flags = 32768;

	CONSOLE_Print( "[REPLAY] building replay" );

	quint32 LanguageID = 0x0012F8B0;

	QByteArray Replay;
	Replay.push_back( (char)16 );								// Unknown (4.0)
	Replay.push_back( (char)1 );								// Unknown (4.0)
	Replay.push_back( (char)0 );								// Unknown (4.0)
	Replay.push_back( (char)0 );								// Unknown (4.0)
	Replay.push_back( (char)0 );								// Host RecordID (4.1)
	Replay.push_back( (char)m_HostPID );						// Host PlayerID (4.1)
	Replay.append(m_HostName);									// Host PlayerName (4.1)
	Replay.push_back( (char)1 );								// Host AdditionalSize (4.1)
	Replay.push_back( (char)0 );								// Host AdditionalData (4.1)
	Replay.append(gameName);									// GameName (4.2)
	Replay.push_back( (char)0 );								// Null (4.0)
	Replay.append(statString);									// StatString (4.3)
	Replay.append(Util::fromUInt32(m_Slots.size( )));			// PlayerCount (4.6)
	Replay.append(Util::fromUInt32(m_MapGameType));				// GameType (4.7)
	Replay.append(Util::fromUInt32(LanguageID));				// LanguageID (4.8)

	// PlayerList (4.9)

	for( QList<PIDPlayer> :: const_iterator i = m_Players.begin( ); i != m_Players.end( ); i++ )
	{
		if( (*i).first != m_HostPID )
		{
			Replay.push_back( 22 );													// Player RecordID (4.1)
			Replay.push_back( (*i).first );											// Player PlayerID (4.1)
			Replay.append((*i).second);												// Player PlayerName (4.1)
			Replay.push_back( 1 );													// Player AdditionalSize (4.1)
			Replay.push_back( (char)0 );											// Player AdditionalData (4.1)
			Replay.append(Util::fromUInt32(0));										// Unknown
		}
	}

	// GameStartRecord (4.10)

	Replay.push_back( 25 );															// RecordID (4.10)
	Replay.append(Util::fromUInt16( 7 + m_Slots.size( ) * 9 ));						// Size (4.10)
	Replay.push_back( m_Slots.size( ) );											// NumSlots (4.10)

	for( unsigned char i = 0; i < m_Slots.size( ); i++ )
		Replay.append(m_Slots[i].GetQByteArray( ));

	Replay.append(Util::fromUInt32(m_RandomSeed));									// RandomSeed (4.10)
	Replay.push_back( m_SelectMode );												// SelectMode (4.10)
	Replay.push_back( m_StartSpotCount );											// StartSpotCount (4.10)

	// ReplayData (5.0)

	Replay.push_back( REPLAY_FIRSTSTARTBLOCK );
	Replay.append(Util::fromUInt32(1));
	Replay.push_back( REPLAY_SECONDSTARTBLOCK );
	Replay.append(Util::fromUInt32(1));

	// leavers during loading need to be stored between the second and third start blocks

	while( !m_LoadingBlocks.isEmpty( ) )
	{
		Replay.append(m_LoadingBlocks.front( ));
		m_LoadingBlocks.dequeue( );
	}

	Replay.push_back( REPLAY_THIRDSTARTBLOCK );
	Replay.append(Util::fromUInt32(1));

	// done

	m_Decompressed = Replay;
	m_Decompressed += m_CompiledBlocks;
}

#define READB( x, y, z )	(x).read( (char *)(y), (z) )
#define READSTR( x, y )		getline( (x), (y), '\0' )

void CReplay :: ParseReplay( bool /*parseBlocks*/ )
{
	/*
	m_HostPID = 0;
	m_HostName.clear( );
	m_GameName.clear( );
	m_StatString.clear( );
	m_PlayerCount = 0;
	m_MapGameType = 0;
	m_Players.clear( );
	m_Slots.clear( );
	m_RandomSeed = 0;
	m_SelectMode = 0;
	m_StartSpotCount = 0;
	m_LoadingBlocks = QQueue<QByteArray>( );
	m_Blocks = QQueue<QByteArray>( );
	m_CheckSums = QQueue<quint32>( );

	if( m_Flags != 32768 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (flags mismatch)" );
		m_Valid = false;
		return;
	}

	istringstream ISS( m_Decompressed );

	unsigned char Garbage1;
	quint32 Garbage4;
	QString GarbageString;
	unsigned char GarbageData[65535];

	READB( ISS, &Garbage4, 4 );				// Unknown (4.0)

	if( Garbage4 != 272 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.0 Unknown mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &Garbage1, 1 );				// Host RecordID (4.1)

	if( Garbage1 != 0 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.1 Host RecordID mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &m_HostPID, 1 );

	if( m_HostPID > 15 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.1 Host PlayerID is invalid)" );
		m_Valid = false;
		return;
	}

	READSTR( ISS, m_HostName );				// Host PlayerName (4.1)
	READB( ISS, &Garbage1, 1 );				// Host AdditionalSize (4.1)

	if( Garbage1 != 1 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.1 Host AdditionalSize mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &Garbage1, 1 );				// Host AdditionalData (4.1)

	if( Garbage1 != 0 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.1 Host AdditionalData mismatch)" );
		m_Valid = false;
		return;
	}

	AddPlayer( m_HostPID, m_HostName );
	READSTR( ISS, m_GameName );				// GameName (4.2)
	READSTR( ISS, GarbageString );			// Null (4.0)
	READSTR( ISS, m_StatString );			// StatString (4.3)
	READB( ISS, &m_PlayerCount, 4 );		// PlayerCount (4.6)

	if( m_PlayerCount > 12 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.6 PlayerCount is invalid)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &m_MapGameType, 4 );		// GameType (4.7)
	READB( ISS, &Garbage4, 4 );				// LanguageID (4.8)

	while( 1 )
	{
		READB( ISS, &Garbage1, 1 );			// Player RecordID (4.1)

		if( Garbage1 == 22 )
		{
			unsigned char PlayerID;
			QString PlayerName;
			READB( ISS, &PlayerID, 1 );		// Player PlayerID (4.1)

			if( PlayerID > 15 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (4.9 Player PlayerID is invalid)" );
				m_Valid = false;
				return;
			}

			READSTR( ISS, PlayerName );		// Player PlayerName (4.1)
			READB( ISS, &Garbage1, 1 );		// Player AdditionalSize (4.1)

			if( Garbage1 != 1 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (4.9 Player AdditionalSize mismatch)" );
				m_Valid = false;
				return;
			}

			READB( ISS, &Garbage1, 1 );		// Player AdditionalData (4.1)

			if( Garbage1 != 0 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (4.9 Player AdditionalData mismatch)" );
				m_Valid = false;
				return;
			}

			READB( ISS, &Garbage4, 4 );		// Unknown

			if( Garbage4 != 0 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (4.9 Unknown mismatch)" );
				m_Valid = false;
				return;
			}

			AddPlayer( PlayerID, PlayerName );
		}
		else if( Garbage1 == 25 )
			break;
		else
		{
			CONSOLE_Print( "[REPLAY] invalid replay (4.9 Player RecordID mismatch)" );
			m_Valid = false;
			return;
		}
	}

	quint16 Size;
	unsigned char NumSlots;
	READB( ISS, &Size, 2 );					// Size (4.10)
	READB( ISS, &NumSlots, 1 );				// NumSlots (4.10)

	if( Size != 7 + NumSlots * 9 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.10 Size is invalid)" );
		m_Valid = false;
		return;
	}

	if( NumSlots == 0 || NumSlots > 12 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (4.10 NumSlots is invalid)" );
		m_Valid = false;
		return;
	}

	for( int i = 0; i < NumSlots; i++ )
	{
		unsigned char SlotData[9];
		READB( ISS, SlotData, 9 );
		QByteArray SlotDataBA = UTIL_CreateBYTEARRAY( SlotData, 9 );
		m_Slots.push_back( CGameSlot( SlotDataBA ) );
	}

	READB( ISS, &m_RandomSeed, 4 );			// RandomSeed (4.10)
	READB( ISS, &m_SelectMode, 1 );			// SelectMode (4.10)
	READB( ISS, &m_StartSpotCount, 1 );		// StartSpotCount (4.10)

	if( ISS.eof( ) || ISS.fail( ) )
	{
		CONSOLE_Print( "[SAVEGAME] failed to parse replay header" );
		m_Valid = false;
		return;
	}

	if( !parseBlocks )
		return;

	READB( ISS, &Garbage1, 1 );				// first start block ID (5.0)

	if( Garbage1 != CReplay :: REPLAY_FIRSTSTARTBLOCK )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (5.0 first start block ID mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &Garbage4, 4 );				// first start block data (5.0)

	if( Garbage4 != 1 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (5.0 first start block data mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &Garbage1, 1 );				// second start block ID (5.0)

	if( Garbage1 != CReplay :: REPLAY_SECONDSTARTBLOCK )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (5.0 second start block ID mismatch)" );
		m_Valid = false;
		return;
	}

	READB( ISS, &Garbage4, 4 );				// second start block data (5.0)

	if( Garbage4 != 1 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (5.0 second start block data mismatch)" );
		m_Valid = false;
		return;
	}

	while( 1 )
	{
		READB( ISS, &Garbage1, 1 );			// third start block ID *or* loading block ID (5.0)

		if( ISS.eof( ) || ISS.fail( ) )
		{
			CONSOLE_Print( "[REPLAY] invalid replay (5.0 third start block unexpected end of file found)" );
			m_Valid = false;
			return;
		}
		if( Garbage1 == CReplay :: REPLAY_LEAVEGAME )
		{
			READB( ISS, GarbageData, 13 );
			QByteArray LoadingBlock;
			LoadingBlock.push_back( Garbage1 );
			UTIL_AppendBYTEARRAY( LoadingBlock, GarbageData, 13 );
			m_LoadingBlocks.enqueue( LoadingBlock );
		}
		else if( Garbage1 == CReplay :: REPLAY_THIRDSTARTBLOCK )
			break;
		else
		{
			CONSOLE_Print( "[REPLAY] invalid replay (5.0 third start block ID mismatch)" );
			m_Valid = false;
			return;
		}
	}

	READB( ISS, &Garbage4, 4 );				// third start block data (5.0)

	if( Garbage4 != 1 )
	{
		CONSOLE_Print( "[REPLAY] invalid replay (5.0 third start block data mismatch)" );
		m_Valid = false;
		return;
	}

	if( ISS.eof( ) || ISS.fail( ) )
	{
		CONSOLE_Print( "[SAVEGAME] failed to parse replay start blocks" );
		m_Valid = false;
		return;
	}

	quint32 ActualReplayLength = 0;

	while( 1 )
	{
		READB( ISS, &Garbage1, 1 );			// block ID (5.0)

		if( ISS.eof( ) || ISS.fail( ) )
			break;
		else if( Garbage1 == CReplay :: REPLAY_LEAVEGAME )
		{
			READB( ISS, GarbageData, 13 );

			// reconstruct the block

			QByteArray Block;
			Block.push_back( CReplay :: REPLAY_LEAVEGAME );
			UTIL_AppendBYTEARRAY( Block, GarbageData, 13 );
			m_Blocks.enqueue( Block );
		}
		else if( Garbage1 == CReplay :: REPLAY_TIMESLOT )
		{
			quint16 BlockSize;
			READB( ISS, &BlockSize, 2 );
			READB( ISS, GarbageData, BlockSize );

			if( BlockSize >= 2 )
				ActualReplayLength += GarbageData[0] | GarbageData[1] << 8;

			// reconstruct the block

			QByteArray Block;
			Block.push_back( CReplay :: REPLAY_TIMESLOT );
			Block.append(Util::fromUInt32(BlockSize));
			UTIL_AppendBYTEARRAY( Block, GarbageData, BlockSize );
			m_Blocks.enqueue( Block );
		}
		else if( Garbage1 == CReplay :: REPLAY_CHATMESSAGE )
		{
			unsigned char PID;
			quint16 BlockSize;
			READB( ISS, &PID, 1 );

			if( PID > 15 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (5.0 chatmessage pid is invalid)" );
				m_Valid = false;
				return;
			}

			READB( ISS, &BlockSize, 2 );
			READB( ISS, GarbageData, BlockSize );

			// reconstruct the block

			QByteArray Block;
			Block.push_back( CReplay :: REPLAY_CHATMESSAGE );
			Block.push_back( PID );
			Block.append(Util::fromUInt32(BlockSize));
			UTIL_AppendBYTEARRAY( Block, GarbageData, BlockSize );
			m_Blocks.enqueue( Block );
		}
		else if( Garbage1 == CReplay :: REPLAY_CHECKSUM )
		{
			READB( ISS, &Garbage1, 1 );

			if( Garbage1 != 4 )
			{
				CONSOLE_Print( "[REPLAY] invalid replay (5.0 checksum unknown mismatch)" );
				m_Valid = false;
				return;
			}

			quint32 CheckSum;
			READB( ISS, &CheckSum, 4 );
			m_CheckSums.enqueue( CheckSum );
		}
		else
		{
			// it's not necessarily an error if we encounter an unknown block ID since replays can contain extra data

			break;
		}
	}

	if( m_ReplayLength != ActualReplayLength )
		CONSOLE_Print( "[REPLAY] warning - replay length mismatch (" + UTIL_ToString( m_ReplayLength ) + "ms/" + UTIL_ToString( ActualReplayLength ) + "ms)" );

	m_Valid = true;
	*/
}
