/****************************************************************************
**
** This file is part of LAN Messenger.
** 
** Copyright (c) 2010 - 2012 Qualia Digital Solutions.
** 
** Contact:  qualiatech@gmail.com
** 
** LAN Messenger is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** LAN Messenger is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with LAN Messenger.  If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/


#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include "trace.h"
#include "netstreamer.h"

const qint64 bufferSize = 65535;
const quint32 maxMessageSize = 64 * 1024 * 1024;

/****************************************************************************
** Class: FileSender
** Description: Handles sending files.
****************************************************************************/
FileSender::FileSender(void) {
	file = NULL;
	socket = NULL;
	timer = NULL;
	buffer = NULL;
	active = false;
}

FileSender::FileSender(QString szId, QString szLocalId, QString szPeerId, QString szFilePath,
    QString szFileName, qint64 nFileSize, QString szAddress, int nPort, FileType nType) {

		id = szId;
        localId = szLocalId;
		peerId = szPeerId;
		filePath = szFilePath;
		fileName = szFileName;
		fileSize = nFileSize;
		address = szAddress;
		port = nPort;
		active = false;
		mile = fileSize / 36;
		milestone = mile;
		file = NULL;
		socket = NULL;
		timer = NULL;
		buffer = NULL;
		type = nType;
}

FileSender::~FileSender(void) {
	delete[] buffer;
	delete file;
}

void FileSender::init(void) {
	socket = new QTcpSocket(this);
	connect(socket, SIGNAL(connected()), this, SLOT(connected()));
	connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
	connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten(qint64)));

	QHostAddress hostAddress(address);
	socket->connectToHost(hostAddress, port);
}

void FileSender::stop(void) {
	active = false;

	if(timer)
		timer->stop();
	if(file && file->isOpen())
		file->close();
	if(socket && socket->isOpen())
		socket->close();
}

void FileSender::connected(void) {
    QByteArray data = id.toLocal8Bit();
    data.append(localId.toLocal8Bit());
	data.insert(0, "FILE");	// insert indicator that this socket handles file transfer
	//	send an id message and then wait for a START message 
	//	from receiver, which will trigger readyRead signal
	if(socket->write(data) != data.size()) {
		QString error;
		emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &error);
		stop();
	}
}

void FileSender::disconnected(void) {
	if(active) {
		QString data;
		emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
	}
}

void FileSender::readyRead(void) {
	if(socket->bytesAvailable() < 5)
		return;
	QByteArray command = socket->read(5);
	if(command != "START") {
		QString data;
		emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
		stop();
		return;
	}
	disconnect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
	// Valid start command received from the receiver; begin sending the file.
	sendFile();
}

void FileSender::timer_timeout(void) {
	if(!active)
		return;
	
	QString transferred = QString::number(file->pos());
	emit progressUpdated(FM_Send, FO_Progress, type, &id, &peerId, &transferred);
}

void FileSender::bytesWritten(qint64 bytes) {
    Q_UNUSED(bytes);

	if(!active)
		return;
	// Do not advance the source file while data from the previous block is
	// still queued. Closing as soon as QFile::pos() reached fileSize could
	// otherwise discard bytes that QTcpSocket had not flushed yet.
	if(socket->bytesToWrite() > 0)
		return;

	qint64 unsentBytes = fileSize - file->pos();

	if(unsentBytes <= 0) {
		active = false;
		if(timer)
			timer->stop();
		file->close();
		socket->disconnectFromHost();
        emit progressUpdated(FM_Send, FO_Complete, type, &id, &peerId, &filePath);
		return;
	}

	qint64 bytesToSend = (bufferSize < unsentBytes) ? bufferSize : unsentBytes;
	qint64 bytesRead = file->read(buffer, bytesToSend);
	if(bytesRead <= 0 || socket->write(buffer, bytesRead) != bytesRead) {
		QString data;
		emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
		stop();
	}

//	if(file->pos() > milestone) {
//		QString transferred = QString::number(file->pos());
//		emit progressUpdated(FM_Send, FO_Progress, type, &id, &peerId, &transferred);
//		milestone += mile;
//	}
}

void FileSender::sendFile(void) {
	file = new QFile(filePath);

	if(file->open(QIODevice::ReadOnly)) {
		active = true;

		// The advertised size is part of the transfer protocol. If the source
		// changed while waiting for acceptance, fail instead of hanging or
		// silently sending a truncated file.
		if(fileSize < 0 || file->size() != fileSize) {
			QString data;
			emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
			stop();
			return;
		}

		// A zero-byte write emits no bytesWritten signal, so complete it here.
		if(fileSize == 0) {
			active = false;
			file->close();
			socket->disconnectFromHost();
			emit progressUpdated(FM_Send, FO_Complete, type, &id, &peerId, &filePath);
			return;
		}

		buffer = new char[bufferSize];

		timer = new QTimer(this);
		connect(timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));
        timer->start(PROGRESS_TIMEOUT);

		qint64 unsentBytes = fileSize - file->pos();
		qint64 bytesToSend = (bufferSize < unsentBytes) ? bufferSize : unsentBytes;
		qint64 bytesRead = file->read(buffer, bytesToSend);
		if(bytesRead <= 0 || socket->write(buffer, bytesRead) != bytesRead) {
			QString data;
			emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
			stop();
		}
	} else {
		socket->close();
        QString data;
		emit progressUpdated(FM_Send, FO_Error, type, &id, &peerId, &data);
	}
}


/****************************************************************************
** Class: FileReceiver
** Description: Handles receiving files.
****************************************************************************/
FileReceiver::FileReceiver(void) {
	file = NULL;
	socket = NULL;
	timer = NULL;
	buffer = NULL;
	active = false;
}

FileReceiver::FileReceiver(QString szId, QString szPeerId, QString szFilePath, QString szFileName, 
	qint64 nFileSize, QString szAddress, int nPort, FileType nType) {

		id = szId;
		peerId = szPeerId;
		filePath = szFilePath;
		fileName = szFileName;
		fileSize = nFileSize;
		address = szAddress;
		port = nPort;
		active = false;
		mile = fileSize / 36;
		milestone = mile;
		file = NULL;
		socket = NULL;
		timer = NULL;
		buffer = NULL;
		type = nType;
        lastPosition = 0;
        numTimeOuts = 0;
}

FileReceiver::~FileReceiver(void) {
	delete[] buffer;
	delete file;
}

void FileReceiver::init(QTcpSocket* socket) {
	this->socket = socket;
	connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
	connect(this->socket, SIGNAL(readyRead()), this, SLOT(readyRead()));

	receiveFile();
	if(!active)
		return;
	//	now send a START message to sender
	if(socket->write("START") != 5) {
		QString data;
		emit progressUpdated(FM_Receive, FO_Error, type, &id, &peerId, &data);
		stop();
		return;
	}

	// No readyRead signal occurs for an empty payload. Both peers can finish
	// immediately after the START handshake has been queued.
	if(fileSize == 0) {
		socket->flush();
		active = false;
		if(timer)
			timer->stop();
		file->close();
		emit progressUpdated(FM_Receive, FO_Complete, type, &id, &peerId, &filePath);
	}
}

void FileReceiver::stop(void) {
	bool deleteFile = false;

	active = false;

	if(timer)
		timer->stop();
	if(file && file->isOpen()) {
		deleteFile = (file->pos() < fileSize);
		file->close();
	}
	if(socket && socket->isOpen())
		socket->close();

	if(deleteFile)
		QFile::remove(filePath);
}

void FileReceiver::disconnected(void) {
	if(active) {
		QString data;
		emit progressUpdated(FM_Receive, FO_Error, type, &id, &peerId, &data);
	}
}

void FileReceiver::readyRead(void) {
	while(active && socket->bytesAvailable() > 0) {
		qint64 unreceivedBytes = fileSize - file->pos();
		qint64 bytesToRead = (bufferSize < unreceivedBytes) ? bufferSize : unreceivedBytes;
		qint64 bytesReceived = socket->read(buffer, bytesToRead);
		if(bytesReceived <= 0)
			return;
		if(file->write(buffer, bytesReceived) != bytesReceived) {
			QString data;
			emit progressUpdated(FM_Receive, FO_Error, type, &id, &peerId, &data);
			stop();
			return;
		}

		if(fileSize - file->pos() <= 0) {
			active = false;
			if(timer)
				timer->stop();
			file->close();
			socket->disconnectFromHost();
			emit progressUpdated(FM_Receive, FO_Complete, type, &id, &peerId, &filePath);
			return;
		}
	}

//	if(file->pos() > milestone) {
//		QString transferred = QString::number(file->pos());
//		emit progressUpdated(FM_Receive, FO_Progress, type, &id, &peerId, &transferred);
//		milestone += mile;
//	}
}

void FileReceiver::timer_timeout(void) {
	if(!active)
		return;

    if(lastPosition < file->pos()) {
        lastPosition = file->pos();
        numTimeOuts = 0;
    } else {
        numTimeOuts++;
        if(numTimeOuts > 20) {
            QString data;
            emit progressUpdated(FM_Receive, FO_Error, type, &id, &peerId, &data);
            stop();
            return;
        }
    }

	QString transferred = QString::number(file->pos());
	emit progressUpdated(FM_Receive, FO_Progress, type, &id, &peerId, &transferred);
}

void FileReceiver::receiveFile(void) {
	QDir dir = QFileInfo(filePath).dir();
	if(!dir.exists())
		dir.mkpath(dir.absolutePath());

	file = new QFile(filePath);

	if(fileSize >= 0 && file->open(QIODevice::WriteOnly)) {
		if(fileSize > 0)
			buffer = new char[bufferSize];
		active = true;

		timer = new QTimer(this);
		connect(timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));
        timer->start(PROGRESS_TIMEOUT);
	} else {
		socket->close();
		emit progressUpdated(FM_Receive, FO_Error, type, &id, &peerId, &filePath);
	}
}


/****************************************************************************
** Class: MsgStream
** Description: Handles transmission and reception of TCP streaming messages.
****************************************************************************/
MsgStream::MsgStream(void) {
	socket = NULL;
	reading = false;
	outDataLen = 0;
	inDataLen = 0;
}

MsgStream::MsgStream(QString szLocalId, QString szPeerId, QString szPeerAddress, int nPort) {
	localId = szLocalId;
	peerId = szPeerId;
	peerAddress = szPeerAddress;
	port = nPort;
	socket = NULL;
	reading = false;
	outDataLen = 0;
	inDataLen = 0;
}

MsgStream::~MsgStream(void) {
}

void MsgStream::init(void) {
	socket = new QTcpSocket(this);
	connect(socket, SIGNAL(connected()), this, SLOT(connected()));
	connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
	connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten(qint64)));

	QHostAddress hostAddress(peerAddress);
	socket->connectToHost(hostAddress, port);
}

void MsgStream::init(QTcpSocket* socket) {
	this->socket = socket;
	connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));
	connect(this->socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
	connect(socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWritten(qint64)));
}

void MsgStream::stop(void) {
	if(socket && socket->isOpen())
		socket->close();
}

void MsgStream::sendMessage(QByteArray& data) {
	if((quint64)data.length() > maxMessageSize) {
		lmcTrace::write("Error: Message is too large to send");
		return;
	}
	qint32 dataLen = sizeof(quint32) + data.length();
	outDataLen += dataLen;
	outData.resize(dataLen);

	QDataStream stream(&outData, QIODevice::WriteOnly);
	stream << (quint32)data.length();
	stream.writeRawData(data.data(), data.length());

	qint64 numBytesWritten = socket->write(outData);
	if(numBytesWritten < 0)
		lmcTrace::write("Error: Socket write failed");
}

void MsgStream::connected(void) {
	outData = localId.toLocal8Bit();
	outData.insert(0, "MSG");	// insert indicator that this socket handles messages
	outDataLen = outData.length();

	//	send an id message and then wait for public key message 
	//	from receiver, which will trigger readyRead signal
	qint64 numBytesWritten = socket->write(outData);
	if(numBytesWritten < 0)
		lmcTrace::write("Error: Socket write failed");
}

void MsgStream::disconnected(void) {
	emit connectionLost(&peerId);
}

void MsgStream::readyRead(void) {
	while(socket->bytesAvailable() > 0) {
		if(!reading) {
			if(socket->bytesAvailable() < (qint64)sizeof(quint32))
				return;
			reading = true;
			QByteArray len = socket->read(4);
			QDataStream stream(len);
			stream >> inDataLen;
			if(stream.status() != QDataStream::Ok || inDataLen == 0 || inDataLen > maxMessageSize) {
				lmcTrace::write("Warning: Invalid TCP message length");
				reading = false;
				socket->disconnectFromHost();
				return;
			}
			inData.clear();
		}

		qint64 bytesToRead = qMin((qint64)inDataLen, socket->bytesAvailable());
		if(bytesToRead <= 0)
			return;
		QByteArray data = socket->read(bytesToRead);
		inData.append(data);
		inDataLen -= data.length();
		if(inDataLen == 0) {
			reading = false;
			emit messageReceived(&peerId, &peerAddress, inData);
		}
	}
}

void MsgStream::bytesWritten(qint64 bytes) {
	outDataLen -= bytes;
	if(outDataLen == 0)
		return;

	if(outDataLen > 0)
		lmcTrace::write("Warning: Socket write operation not completed");
	if(outDataLen < 0)
		lmcTrace::write("Warning: Socket write overrun");

	//	TODO: handle situation when entire message is not written to stream in one write operation
	//	The following code is not functional currently, hence commented out.
	/*outData = outData.mid(outDataLen);
	socket->write(outData);*/
}
