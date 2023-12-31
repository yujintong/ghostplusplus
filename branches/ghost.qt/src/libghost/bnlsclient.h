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

#ifndef BNLSCLIENT_H
#define BNLSCLIENT_H

//
// CBNLSClient
//
#include "includes.h"
#include <QTcpSocket>
#include <QTimer>

class CBNLSProtocol;
class CCommandPacket;

class CBNLSClient : public QObject
{
	Q_OBJECT

public slots:
	void socketConnected();
	void socketDisconnected();
	void socketDataReady();
	void socketConnect();
	void socketError();
	void timeout_NULL();

signals:
	emit void newWardenResponse(const QByteArray &data);

private:
	QTimer m_NULLTimer;
	QTcpSocket *m_Socket;							// the connection to the BNLS server
	CBNLSProtocol *m_Protocol;						// battle.net protocol
	QQueue<CCommandPacket *> m_Packets;				// queue of incoming packets
	bool m_WasConnected;
	QString m_Server;
	quint16 m_Port;
	quint32 m_LastNullTime;
	quint32 m_WardenCookie;						// the warden cookie
	QQueue<QByteArray> m_OutPackets;					// queue of outgoing packets to be sent
	quint32 m_TotalWardenIn;
	quint32 m_TotalWardenOut;
	int m_Retries;

public:
	CBNLSClient( QString nServer, quint16 nPort, quint32 nWardenCookie );
	~CBNLSClient( );

	QByteArray GetWardenResponse( );
	quint32 GetTotalWardenIn( )		{ return m_TotalWardenIn; }
	quint32 GetTotalWardenOut( )		{ return m_TotalWardenOut; }

	// processing functions

	void ExtractPackets( );
	void ProcessPackets( );

	// other functions

	void QueueWardenSeed( quint32 seed );
	void QueueWardenRaw( QByteArray wardenRaw );
};

#endif
