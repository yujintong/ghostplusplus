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
#include "ghostdb.h"
#include "gameplayer.h"
#include "gameprotocol.h"
#include "game_base.h"
#include "stats.h"
#include "statsdota.h"

#include <QRegExp>

//
// CStatsDOTA
//

CStatsDOTA :: CStatsDOTA( CBaseGame *nGame ) : CStats( nGame )
{
	CONSOLE_Print( "[STATSDOTA] using dota stats" );

	for( unsigned int i = 0; i < 12; i++ )
		m_Players[i] = NULL;

	m_Winner = 0;
	m_Min = 0;
	m_Sec = 0;
}

CStatsDOTA :: ~CStatsDOTA( )
{
	for( unsigned int i = 0; i < 12; i++ )
	{
		if( m_Players[i] )
			delete m_Players[i];
	}
}

bool CStatsDOTA :: ProcessAction( CIncomingAction *Action )
{
	unsigned int i = 0;
	const QByteArray &ActionData = Action->GetAction( );
	QByteArray Data;
	QByteArray Key;
	QByteArray Value;

	// dota actions with real time replay data start with 0x6b then the null terminated QString "dr.x"
	// unfortunately more than one action can be sent in a single packet and the length of each action isn't explicitly represented in the packet
	// so we have to either parse all the actions and calculate the length based on the type or we can search for an identifying sequence
	// parsing the actions would be more correct but would be a lot more difficult to write for relatively little gain
	// so we take the easy route (which isn't always guaranteed to work) and search the data for the sequence "6b 64 72 2e 78 00" and hope it identifies an action

	while( (unsigned int)ActionData.size( ) >= i + 6 )
	{
		if( ActionData.at(i) == 0x6b &&
			ActionData.at(i + 1) == 0x64 &&
			ActionData.at(i + 2) == 0x72 &&
			ActionData.at(i + 3) == 0x2e &&
			ActionData.at(i + 4) == 0x78 &&
			ActionData.at(i + 5) == 0x00 )
		{
			// we think we've found an action with real time replay data (but we can't be 100% sure)
			// next we parse out two null terminated strings and a 4 byte integer

			if( (unsigned int)ActionData.size( ) >= i + 7 )
			{
				// the first null terminated QString should either be the strings "Data" or "Global" or a player id in ASCII representation, e.g. "1" or "2"

				Data = UTIL_ExtractCString( ActionData, i + 6 );

				if( (unsigned int)ActionData.size( ) >= i + 8 + Data.size( ) )
				{
					// the second null terminated QString should be the key

					Key = UTIL_ExtractCString( ActionData, i + 7 + Data.size( ) );

					if( (unsigned int)ActionData.size( ) >= i + 12 + Data.size( ) + Key.size( ) )
					{
						// the 4 byte integer should be the value

						Value = ActionData.mid(i + 8 + Data.size( ) + Key.size( ), 4 );
						QString DataString = Data;
						QString KeyString = Key;
						quint32 ValueInt = Util::extractUInt32( Value );

						// CONSOLE_Print( "[STATS] " + DataString + ", " + KeyString + ", " + QString::number( ValueInt ) );

						if( DataString == "Data" )
						{
							// these are received during the game
							// you could use these to calculate killing sprees and double or triple kills (you'd have to make up your own time restrictions though)
							// you could also build a table of "who killed who" data

							if( KeyString.size( ) >= 5 && KeyString.mid( 0, 4 ) == "Hero" )
							{
								// a hero died

								quint32 VictimColour = KeyString.mid( 4 ).toUInt();
								CGamePlayer *Killer = m_Game->GetPlayerFromColour( ValueInt );
								CGamePlayer *Victim = m_Game->GetPlayerFromColour( VictimColour );

								if( Killer && Victim )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] player [" + Killer->GetName( ) + "] killed player [" + Victim->GetName( ) + "]" );
								else if( Victim )
								{
									if( ValueInt == 0 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Sentinel killed player [" + Victim->GetName( ) + "]" );
									else if( ValueInt == 6 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Scourge killed player [" + Victim->GetName( ) + "]" );
								}
							}
							else if( KeyString.size( ) >= 8 && KeyString.mid( 0, 7 ) == "Courier" )
							{
								// a courier died

								if( ( ValueInt >= 1 && ValueInt <= 5 ) || ( ValueInt >= 7 && ValueInt <= 11 ) )
								{
									if( !m_Players[ValueInt] )
										m_Players[ValueInt] = new CDBDotAPlayer( );

									m_Players[ValueInt]->SetCourierKills( m_Players[ValueInt]->GetCourierKills( ) + 1 );
								}

								quint32 VictimColour = KeyString.mid( 7 ).toUInt();
								CGamePlayer *Killer = m_Game->GetPlayerFromColour( ValueInt );
								CGamePlayer *Victim = m_Game->GetPlayerFromColour( VictimColour );

								if( Killer && Victim )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] player [" + Killer->GetName( ) + "] killed a courier owned by player [" + Victim->GetName( ) + "]" );
								else if( Victim )
								{
									if( ValueInt == 0 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Sentinel killed a courier owned by player [" + Victim->GetName( ) + "]" );
									else if( ValueInt == 6 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Scourge killed a courier owned by player [" + Victim->GetName( ) + "]" );
								}
							}
							else if( KeyString.size( ) >= 8 && KeyString.mid( 0, 5 ) == "Tower" )
							{
								// a tower died

								if( ( ValueInt >= 1 && ValueInt <= 5 ) || ( ValueInt >= 7 && ValueInt <= 11 ) )
								{
									if( !m_Players[ValueInt] )
										m_Players[ValueInt] = new CDBDotAPlayer( );

									m_Players[ValueInt]->SetTowerKills( m_Players[ValueInt]->GetTowerKills( ) + 1 );
								}

								QString Alliance = KeyString.mid( 5, 1 );
								QString Level = KeyString.mid( 6, 1 );
								QString Side = KeyString.mid( 7, 1 );
								CGamePlayer *Killer = m_Game->GetPlayerFromColour( ValueInt );
								QString AllianceString;
								QString SideString;

								if( Alliance == "0" )
									AllianceString = "Sentinel";
								else if( Alliance == "1" )
									AllianceString = "Scourge";
								else
									AllianceString = "unknown";

								if( Side == "0" )
									SideString = "top";
								else if( Side == "1" )
									SideString = "mid";
								else if( Side == "2" )
									SideString = "bottom";
								else
									SideString = "unknown";

								if( Killer )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] player [" + Killer->GetName( ) + "] destroyed a level [" + Level + "] " + AllianceString + " tower (" + SideString + ")" );
								else
								{
									if( ValueInt == 0 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Sentinel destroyed a level [" + Level + "] " + AllianceString + " tower (" + SideString + ")" );
									else if( ValueInt == 6 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Scourge destroyed a level [" + Level + "] " + AllianceString + " tower (" + SideString + ")" );
								}
							}
							else if( KeyString.size( ) >= 6 && KeyString.mid( 0, 3 ) == "Rax" )
							{
								// a rax died

								if( ( ValueInt >= 1 && ValueInt <= 5 ) || ( ValueInt >= 7 && ValueInt <= 11 ) )
								{
									if( !m_Players[ValueInt] )
										m_Players[ValueInt] = new CDBDotAPlayer( );

									m_Players[ValueInt]->SetRaxKills( m_Players[ValueInt]->GetRaxKills( ) + 1 );
								}

								QString Alliance = KeyString.mid( 3, 1 );
								QString Side = KeyString.mid( 4, 1 );
								QString Type = KeyString.mid( 5, 1 );
								CGamePlayer *Killer = m_Game->GetPlayerFromColour( ValueInt );
								QString AllianceString;
								QString SideString;
								QString TypeString;

								if( Alliance == "0" )
									AllianceString = "Sentinel";
								else if( Alliance == "1" )
									AllianceString = "Scourge";
								else
									AllianceString = "unknown";

								if( Side == "0" )
									SideString = "top";
								else if( Side == "1" )
									SideString = "mid";
								else if( Side == "2" )
									SideString = "bottom";
								else
									SideString = "unknown";

								if( Type == "0" )
									TypeString = "melee";
								else if( Type == "1" )
									TypeString = "ranged";
								else
									TypeString = "unknown";

								if( Killer )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] player [" + Killer->GetName( ) + "] destroyed a " + TypeString + " " + AllianceString + " rax (" + SideString + ")" );
								else
								{
									if( ValueInt == 0 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Sentinel destroyed a " + TypeString + " " + AllianceString + " rax (" + SideString + ")" );
									else if( ValueInt == 6 )
										CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Scourge destroyed a " + TypeString + " " + AllianceString + " rax (" + SideString + ")" );
								}
							}
							else if( KeyString.size( ) >= 6 && KeyString.mid( 0, 6 ) == "Throne" )
							{
								// the frozen throne got hurt

								CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the Frozen Throne is now at " + QString::number( ValueInt ) + "% HP" );
							}
							else if( KeyString.size( ) >= 4 && KeyString.mid( 0, 4 ) == "Tree" )
							{
								// the world tree got hurt

								CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] the World Tree is now at " + QString::number( ValueInt ) + "% HP" );
							}
							else if( KeyString.size( ) >= 2 && KeyString.mid( 0, 2 ) == "CK" )
							{
								// a player disconnected
							}
						}
						else if( DataString == "Global" )
						{
							// these are only received at the end of the game

							if( KeyString == "Winner" )
							{
								// Value 1 -> sentinel
								// Value 2 -> scourge

								m_Winner = ValueInt;

								if( m_Winner == 1 )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] detected winner: Sentinel" );
								else if( m_Winner == 2 )
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] detected winner: Scourge" );
								else
									CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] detected winner: " + QString::number( ValueInt ) );
							}
							else if( KeyString == "m" )
								m_Min = ValueInt;
							else if( KeyString == "s" )
								m_Sec = ValueInt;
						}
						else if( DataString.size( ) <= 2 && DataString.indexOf(QRegExp( "[^0-9]" )) == -1 )
						{
							// these are only received at the end of the game

							quint32 ID = DataString.toUInt();

							if( ( ID >= 1 && ID <= 5 ) || ( ID >= 7 && ID <= 11 ) )
							{
								if( !m_Players[ID] )
								{
									m_Players[ID] = new CDBDotAPlayer( );
									m_Players[ID]->SetColour( ID );
								}

								// Key "1"		-> Kills
								// Key "2"		-> Deaths
								// Key "3"		-> Creep Kills
								// Key "4"		-> Creep Denies
								// Key "5"		-> Assists
								// Key "6"		-> Current Gold
								// Key "7"		-> Neutral Kills
								// Key "8_0"	-> Item 1
								// Key "8_1"	-> Item 2
								// Key "8_2"	-> Item 3
								// Key "8_3"	-> Item 4
								// Key "8_4"	-> Item 5
								// Key "8_5"	-> Item 6
								// Key "id"		-> ID (1-5 for sentinel, 6-10 for scourge, accurate after using -sp and/or -switch)

								if( KeyString == "1" )
									m_Players[ID]->SetKills( ValueInt );
								else if( KeyString == "2" )
									m_Players[ID]->SetDeaths( ValueInt );
								else if( KeyString == "3" )
									m_Players[ID]->SetCreepKills( ValueInt );
								else if( KeyString == "4" )
									m_Players[ID]->SetCreepDenies( ValueInt );
								else if( KeyString == "5" )
									m_Players[ID]->SetAssists( ValueInt );
								else if( KeyString == "6" )
									m_Players[ID]->SetGold( ValueInt );
								else if( KeyString == "7" )
									m_Players[ID]->SetNeutralKills( ValueInt );
								else if( KeyString == "8_0" )
									m_Players[ID]->SetItem( 0, QString( Util::reverse(Value) ) );
								else if( KeyString == "8_1" )
									m_Players[ID]->SetItem( 1, QString( Util::reverse(Value) ) );
								else if( KeyString == "8_2" )
									m_Players[ID]->SetItem( 2, QString( Util::reverse(Value) ) );
								else if( KeyString == "8_3" )
									m_Players[ID]->SetItem( 3, QString( Util::reverse(Value) ) );
								else if( KeyString == "8_4" )
									m_Players[ID]->SetItem( 4, QString( Util::reverse(Value) ) );
								else if( KeyString == "8_5" )
									m_Players[ID]->SetItem( 5, QString( Util::reverse(Value) ) );
								else if( KeyString == "9" )
									m_Players[ID]->SetHero( QString( Util::reverse(Value) ) );
								else if( KeyString == "id" )
								{
									// DotA sends id values from 1-10 with 1-5 being sentinel players and 6-10 being scourge players
									// unfortunately the actual player colours are from 1-5 and from 7-11 so we need to deal with this case here

									if( ValueInt >= 6 )
										m_Players[ID]->SetNewColour( ValueInt + 1 );
									else
										m_Players[ID]->SetNewColour( ValueInt );
								}
							}
						}

						i += 12 + Data.size( ) + Key.size( );
					}
					else
						i++;
				}
				else
					i++;
			}
			else
				i++;
		}
		else
			i++;
	}

	return m_Winner != 0;
}

void CStatsDOTA :: Save( CGHost *GHost, CGHostDB *DB, quint32 GameID )
{
	if( DB->Begin( ) )
	{
		// since we only record the end game information it's possible we haven't recorded anything yet if the game didn't end with a tree/throne death
		// this will happen if all the players leave before properly finishing the game
		// the dotagame stats are always saved (with winner = 0 if the game didn't properly finish)
		// the dotaplayer stats are only saved if the game is properly finished

		unsigned int Players = 0;

		// save the dotagame

		GHost->m_Callables.push_back( DB->ThreadedDotAGameAdd( GameID, m_Winner, m_Min, m_Sec ) );

		// check for invalid colours and duplicates
		// this can only happen if DotA sends us garbage in the "id" value but we should check anyway

		for( unsigned int i = 0; i < 12; i++ )
		{
			if( m_Players[i] )
			{
				quint32 Colour = m_Players[i]->GetNewColour( );

				if( !( ( Colour >= 1 && Colour <= 5 ) || ( Colour >= 7 && Colour <= 11 ) ) )
				{
					CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] discarding player data, invalid colour found" );
					DB->Commit( );
					return;
				}

				for( unsigned int j = i + 1; j < 12; j++ )
				{
					if( m_Players[j] && Colour == m_Players[j]->GetNewColour( ) )
					{
						CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] discarding player data, duplicate colour found" );
						DB->Commit( );
						return;
					}
				}
			}
		}

		// save the dotaplayers

		for( unsigned int i = 0; i < 12; i++ )
		{
			if( m_Players[i] )
			{
				GHost->m_Callables.push_back( DB->ThreadedDotAPlayerAdd( GameID, m_Players[i]->GetColour( ), m_Players[i]->GetKills( ), m_Players[i]->GetDeaths( ), m_Players[i]->GetCreepKills( ), m_Players[i]->GetCreepDenies( ), m_Players[i]->GetAssists( ), m_Players[i]->GetGold( ), m_Players[i]->GetNeutralKills( ), m_Players[i]->GetItem( 0 ), m_Players[i]->GetItem( 1 ), m_Players[i]->GetItem( 2 ), m_Players[i]->GetItem( 3 ), m_Players[i]->GetItem( 4 ), m_Players[i]->GetItem( 5 ), m_Players[i]->GetHero( ), m_Players[i]->GetNewColour( ), m_Players[i]->GetTowerKills( ), m_Players[i]->GetRaxKills( ), m_Players[i]->GetCourierKills( ) ) );
				Players++;
			}
		}

		if( DB->Commit( ) )
			CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] saving " + QString::number( Players ) + " players" );
		else
			CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] unable to commit database transaction, data not saved" );
	}
	else
		CONSOLE_Print( "[STATSDOTA: " + m_Game->GetGameName( ) + "] unable to begin database transaction, data not saved" );
}
