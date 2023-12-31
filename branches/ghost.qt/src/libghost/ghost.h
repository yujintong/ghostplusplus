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

#ifndef GHOST_H
#define GHOST_H

#include "includes.h"

#include <QTimer>
#include <QTime>
#include <QList>
#include <QScopedPointer>
#include <QHostAddress>

//
// CGHost
//

class CGPSProtocol;
class CCRC32;
class CSHA1;
class CBNET;
class CBaseGame;
class CGame;
class CAdminGame;
class CGHostDB;
class CBaseCallable;
class CLanguage;
class CMap;
class CSaveGame;
class CConfig;
class CGHostPrivate;
class CGamePlayer;
class IGHostPlugin;
class ICommandProvider;
class ILogPlugin;

QT_FORWARD_DECLARE_CLASS(QDir)
QT_FORWARD_DECLARE_CLASS(QUdpSocket)
QT_FORWARD_DECLARE_CLASS(QTcpServer)

class CGHost : public QObject
{
		Q_OBJECT
public:
	CGame *GetCurrentGame( ) const;
	CAdminGame *GetAdminGame( ) const;
	CLanguage *GetLanguage( ) const;
	CMap *GetCurrentMap( ) const;
	void SetCurrentMap( CMap *map);
	CMap *GetAdminMap( ) const;
	CMap *GetAutoHostMap( ) const;
	void SetAutoHostMap( CMap *map );
	bool GetExiting( ) const { return m_Exiting; }
	bool GetExitingNice( ) const { return m_ExitingNice; }
	void ExitNice( );
	void Exit( );
	
	bool IsGameCreationEnabled( ) const { return m_Enabled; }
	void EnableGameCreation( ) { m_Enabled = true; }
	void DisableGameCreation( ) { m_Enabled = false; }
	
	void SendUdpBroadcast( const QByteArray &data, const quint16 &port, const QHostAddress& target);
	void SendUdpBroadcast( const QByteArray &data, const quint16 &port);
	void SendUdpBroadcast( const QByteArray &data);

	void LoadSavegame( const QString& path );
	
	const CGPSProtocol &GetGPSProtocol( ) { return *m_GPSProtocol; }
	
	const QString &GetVersion( ) { return m_Version; }

private: /*** Plugins ***/
	QList<IGHostPlugin *> m_Plugins;
	QList<ICommandProvider *> m_CommandProviders;
	QList<ILogPlugin *> m_LogPlugins;
	void LoadPlugins( QDir path, CConfig *cfg );
public:
	void LoadPlugin( QObject *plugin, CConfig *cfg );

private: /*** Logging ***/
	void Log( const QString &message, int level );
public:
	void LogInfo( const QString &message );
	void LogWarning( const QString &message );
	void LogError( const QString &message );

// slots for game events
public slots:
	void EventGameStarted();
	void EventGameDeleted();
	void EventGameCommand( CBaseGame *game, CGamePlayer *player, const QString &command, const QString &payload );
	void EventBnetCommand( CBNET *bnet, const QString &user, const QString &command, const QString &payload, bool whisper );
protected:
	QScopedPointer<CGHostPrivate> d_ptr;
private:
	Q_DECLARE_PRIVATE(CGHost)
	CGame *m_CurrentGame;					// this game is still in the lobby state
	CAdminGame *m_AdminGame;				// this "fake game" allows an admin who knows the password to control the bot from the local network
	CLanguage *m_Language;					// language
	CMap *m_Map;							// the currently loaded map
	CMap *m_AdminMap;						// the map to use in the admin game
	CMap *m_AutoHostMap;					// the map to use when autohosting
	

	
public slots:
	void EventIncomingReconnection();
	void EventReconnectionSocketReadyRead();
	void EventCallableUpdateTimeout();
	void EventDatabaseError(const QString &error);
	void EventExitNice();
	void EventWaitForNiceExitTimeout();
	void EventAutoHost();
	void EventAdminGameDeleted();
	void CreateReconnectServer();
	
private:
	QTime m_LastAutoHostTime;
	QUdpSocket *m_UDPSocket;				// a UDP socket for sending broadcasts and other junk (used with !sendlan)
	QHostAddress m_BroadcastTarget;
	QTcpServer *m_ReconnectSocket;			// listening socket for GProxy++ reliable reconnects
	CGPSProtocol *m_GPSProtocol;
	bool m_Exiting;							// set to true to force ghost to shutdown next update (used by SignalCatcher)
	bool m_ExitingNice;						// set to true to force ghost to disconnect from all battle.net connections and wait for all games to finish before shutting down
	bool m_Enabled;							// set to false to prevent new games from being created
	QString m_Version;						// GHost++ version QString
	QTimer m_CallableUpdateTimer, m_AutoHostTimer;
	CSaveGame *m_SaveGame;					// the save game to use

public:
	CCRC32 *m_CRC;							// for calculating CRC's
	CSHA1 *m_SHA;							// for calculating SHA1's
	QList<CBNET *> m_BNETs;				// all our battle.net connections (there can be more than one)
	QList<CBaseGame *> m_Games;			// these games are in progress
	CGHostDB *m_DB;							// database
	CGHostDB *m_DBLocal;					// local database (for temporary data)
	QList<CBaseCallable *> m_Callables;	// vector of orphaned callables waiting to die
	QList<QByteArray> m_LocalAddresses;		// vector of local IP addresses
	QList<PIDPlayer> m_EnforcePlayers;		// vector of pids to force players to use in the next game (used with saved games)
	

	QString m_AutoHostGameName;				// the base game name to auto host with
	QString m_AutoHostOwner;
	QString m_AutoHostServer;
	int m_AutoHostMaximumGames;		// maximum number of games to auto host
	quint32 m_AutoHostAutoStartPlayers;	// when using auto hosting auto start the game when this many players have joined
	bool m_AutoHostMatchMaking;
	double m_AutoHostMinimumScore;
	double m_AutoHostMaximumScore;
	bool m_AllGamesFinished;				// if all games finished (used when exiting nicely)
	quint32 m_AllGamesFinishedTime;		// GetTime when all games finished (used when exiting nicely)
	QString m_LanguageFile;					// config value: language file
	QByteArray m_Warcraft3Path;					// config value: Warcraft 3 path
	bool m_TFT;								// config value: TFT enabled or not
	QString m_BindAddress;					// config value: the address to host games on
	quint16 m_HostPort;					// config value: the port to host games on
	bool m_Reconnect;						// config value: GProxy++ reliable reconnects enabled or not
	quint16 m_ReconnectPort;				// config value: the port to listen for GProxy++ reliable reconnects on
	quint32 m_ReconnectWaitTime;			// config value: the maximum number of minutes to wait for a GProxy++ reliable reconnect
	int m_MaxGames;							// config value: maximum number of games in progress
	char m_CommandTrigger;					// config value: the command trigger inside games
	QString m_MapCFGPath;					// config value: map cfg path
	QString m_SaveGamePath;					// config value: savegame path
	QString m_MapPath;						// config value: map path
	bool m_SaveReplays;						// config value: save replays
	QString m_ReplayPath;					// config value: replay path
	QString m_VirtualHostName;				// config value: virtual host name
	bool m_HideIPAddresses;					// config value: hide IP addresses from players
	bool m_CheckMultipleIPUsage;			// config value: check for multiple IP address usage
	quint32 m_SpoofChecks;					// config value: do automatic spoof checks or not
	bool m_RequireSpoofChecks;				// config value: require spoof checks or not
	bool m_ReserveAdmins;					// config value: consider admins to be reserved players or not
	bool m_RefreshMessages;					// config value: display refresh messages or not (by default)
	bool m_AutoLock;						// config value: auto lock games when the owner is present
	bool m_AutoSave;						// config value: auto save before someone disconnects
	quint32 m_AllowDownloads;				// config value: allow map downloads or not
	bool m_PingDuringDownloads;				// config value: ping during map downloads or not
	quint32 m_MaxDownloaders;				// config value: maximum number of map downloaders at the same time
	quint32 m_MaxDownloadSpeed;			// config value: maximum total map download speed in KB/sec
	bool m_LCPings;							// config value: use LC style pings (divide actual pings by two)
	quint32 m_AutoKickPing;				// config value: auto kick players with ping higher than this
	quint32 m_BanMethod;					// config value: ban method (ban by name/ip/both)
	QString m_IPBlackListFile;				// config value: IP blacklist file (ipblacklist.txt)
	quint32 m_LobbyTimeLimit;				// config value: auto close the game lobby after this many minutes without any reserved players
	quint32 m_Latency;						// config value: the latency (by default)
	quint32 m_SyncLimit;					// config value: the maximum number of packets a player can fall out of sync before starting the lag screen (by default)
	bool m_VoteKickAllowed;					// config value: if votekicks are allowed or not
	quint32 m_VoteKickPercentage;			// config value: percentage of players required to vote yes for a votekick to pass
	QString m_DefaultMap;					// config value: default map (map.cfg)
	QString m_MOTDFile;						// config value: motd.txt
	QString m_GameLoadedFile;				// config value: gameloaded.txt
	QString m_GameOverFile;					// config value: gameover.txt
	bool m_LocalAdminMessages;				// config value: send local admin messages or not
	bool m_AdminGameCreate;					// config value: create the admin game or not
	quint16 m_AdminGamePort;				// config value: the port to host the admin game on
	QString m_AdminGamePassword;				// config value: the admin game password
	QString m_AdminGameMap;					// config value: the admin game map config to use
	unsigned char m_LANWar3Version;			// config value: LAN warcraft 3 version
	quint32 m_ReplayWar3Version;			// config value: replay warcraft 3 version (for saving replays)
	quint32 m_ReplayBuildNumber;			// config value: replay build number (for saving replays)
	bool m_TCPNoDelay;						// config value: use Nagle's algorithm or not
	quint32 m_MatchMakingMethod;			// config value: the matchmaking method
	QString m_ConfigFile;

	CGHost( CConfig *CFG, QString configFile );
	~CGHost( );

	// events

	void EventBNETConnecting( CBNET *bnet );
	void EventBNETConnected( CBNET *bnet );
	void EventBNETDisconnected( CBNET *bnet );
	void EventBNETLoggedIn( CBNET *bnet );
	void EventBNETGameRefreshed( CBNET *bnet );
	void EventBNETGameRefreshFailed( CBNET *bnet );
	void EventBNETConnectTimedOut( CBNET *bnet );
	void EventBNETWhisper( CBNET *bnet, const QString &user, const QString &message );
	void EventBNETChat( CBNET *bnet, const QString &user, const QString &message );
	void EventBNETEmote( CBNET *bnet, const QString &user, const QString &message );
//	void EventGameDeleted( CBaseGame *game );

	// other functions

	void ReloadConfigs( );
	void SetConfigs( CConfig *CFG );
	void ExtractScripts( );
	void LoadIPToCountryData( );
	void CreateGame( CMap *map, unsigned char gameState, bool saveGame, const QString &gameName, const QString &ownerName, const QString &creatorName, const QString &creatorServer, bool whisper );
};

#endif
