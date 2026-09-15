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
****************************************************************************/

#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QSaveFile>
#include <QSet>
#include "history.h"

QString History::historyFile(void) {
	lmcSettings settings;
	bool sysPath = settings.value(IDS_SYSHISTORYPATH, IDS_SYSHISTORYPATH_VAL).toBool();
    QString path = QDir::toNativeSeparators(QStandardPaths::writableLocation(
        QStandardPaths::DataLocation) + "/" HC_FILENAME);
	if(!sysPath)
		path = settings.value(IDS_HISTORYPATH, path).toString();
	return path;
}

void History::create(QString path) {
	QFile file(path);
	if(!file.open(QIODevice::ReadWrite))
		return;

	QDataStream stream(&file);

	DBHeader header(HC_DBMARKER, HC_HDRSIZE, HC_VERSION, 0, 0, 0);
	writeHeader(&stream, &header);

	file.close();
}

void History::writeHeader(QDataStream* pStream, DBHeader* pHeader) {
	pStream->device()->seek(0);

	*pStream << pHeader->marker;
	*pStream << pHeader->version;
	*pStream << pHeader->headerSize;
	*pStream << pHeader->count;
	*pStream << pHeader->first;
	*pStream << pHeader->last;
}

DBHeader History::readHeader(QDataStream* pStream) {
	DBHeader header;
	
	*pStream >> header.marker;
	*pStream >> header.version;
	*pStream >> header.headerSize;
	*pStream >> header.count;
	*pStream >> header.first;
	*pStream >> header.last;

	return header;
}

int History::save(QString user, QDateTime date, QString* lpszData) {
	QString path = historyFile();
	
	QDir dir = QFileInfo(path).dir();
	if(!dir.exists())
		dir.mkpath(dir.absolutePath());

	if(!QFile::exists(path))
		create(path);

	QFile file(path);
	if(!file.open(QIODevice::ReadWrite))
		return -1;

	QDataStream stream(&file);

	DBHeader header = readHeader(&stream);
	if(header.marker.compare(HC_DBMARKER) != 0 || header.version != HC_VERSION ||
		header.headerSize != HC_HDRSIZE || header.count < 0 ||
		(header.count == 0 && (header.first != 0 || header.last != 0)) ||
		(header.count > 0 && (header.first < header.headerSize || header.first >= file.size() ||
			header.last < header.headerSize || header.last >= file.size()))) {
		file.close();
		return -1;
	}

	qint64 dataPos = insertData(&stream, lpszData);
	qint64 newIndex = insertIndex(&stream, dataPos, user, date);
	updateIndex(&stream, header.last, newIndex);

	header.count++;
	header.first = (header.first == 0) ? newIndex : header.first;
	header.last = newIndex;

	writeHeader(&stream, &header);

	file.close();
	return 0;
}

qint64 History::insertData(QDataStream* pStream, QString* lpszData) {
	qint64 lastPos = pStream->device()->size();
	pStream->device()->seek(lastPos);

	QByteArray data = lpszData->toUtf8();
	
	*pStream << QString(HC_DTMARKER);
	*pStream << data.length();
	*pStream << data;

	return lastPos;
}

qint64 History::insertIndex(QDataStream* pStream, qint64 dataPos, QString user, QDateTime date) {
	qint64 lastPos = pStream->device()->size();
	pStream->device()->seek(lastPos);

	qint64 nextPos = 0;

	*pStream << QString(HC_IDMARKER);
	*pStream << nextPos;
	*pStream << dataPos;
	*pStream << date.toMSecsSinceEpoch();
	*pStream << user;

	return lastPos;
}

void History::updateIndex(QDataStream* pStream, qint64 oldIndex, qint64 newIndex) {
	if(oldIndex <= 0)
		return;
	pStream->device()->seek(oldIndex);

	*pStream << QString(HC_IDMARKER);
	*pStream << newIndex;
}

QList<MsgInfo> History::getList(void) {
	QList<MsgInfo> list;

	lmcSettings settings;
	QString path = historyFile();

	if(!QFile::exists(path))
		return list;

	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return list;

	QDataStream stream(&file);

	DBHeader header = readHeader(&stream);
	if(stream.status() != QDataStream::Ok || header.marker != HC_DBMARKER ||
		header.version != HC_VERSION || header.headerSize != HC_HDRSIZE ||
		header.count < 0 || (header.count == 0 && (header.first != 0 || header.last != 0)))
		return list;
	qint64 next = header.first;
	QSet<qint64> visited;
	
	QString marker;
	qint64 offset;
	qint64 date;
	QString name;
	
	while(next != 0 && list.count() < header.count) {
		if(next < header.headerSize || next >= file.size() || visited.contains(next) ||
			!stream.device()->seek(next))
			break;
		visited.insert(next);
		MsgInfo info;
		stream >> marker;
		stream >> next;
		stream >> offset;
		stream >> date;
		stream >> name;
		if(stream.status() != QDataStream::Ok || marker != HC_IDMARKER ||
			offset < header.headerSize || offset >= file.size())
			break;
		list.append(MsgInfo(name, QDateTime::fromMSecsSinceEpoch(date), offset));
	}

	return list;
}

QString History::getMessage(qint64 offset) {
	QString data;

	lmcSettings settings;
	QString path = historyFile();

	if(!QFile::exists(path))
		return data;

	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return data;

	QDataStream stream(&file);

	if(offset < HC_HDRSIZE || offset >= file.size() || !stream.device()->seek(offset))
		return data;

	QString marker;
	qint32 length;
	quint32 encodedLength;
	QByteArray buffer;

	stream >> marker;
	stream >> length;
	if(stream.status() != QDataStream::Ok || marker != HC_DTMARKER || length < 0 ||
		file.size() - file.pos() < (qint64)sizeof(quint32) + length)
		return data;

	stream >> encodedLength;
	if(stream.status() != QDataStream::Ok || encodedLength != (quint32)length ||
		file.size() - file.pos() < length)
		return data;

	buffer.resize(length);
	if(length > 0 && stream.readRawData(buffer.data(), length) != length)
		return QString();
	
	data = QString::fromUtf8(buffer, buffer.length());

	file.close();
	return data;
}

bool History::remove(qint64 offset) {
	QList<MsgInfo> records = getList();
	if(records.isEmpty())
		return false;

	bool found = false;
	struct HistoryRecord {
		QString name;
		QDateTime date;
		QString data;
	};
	QList<HistoryRecord> keep;

	for(int i = 0; i < records.count(); ++i) {
		const MsgInfo& info = records.at(i);
		if(info.offset == offset) {
			found = true;
			continue;
		}
		HistoryRecord record;
		record.name = info.name;
		record.date = info.date;
		record.data = getMessage(info.offset);
		keep.append(record);
	}

	if(!found)
		return false;

	QString path = historyFile();
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly))
		return false;

	QDataStream stream(&file);
	DBHeader header(HC_DBMARKER, HC_HDRSIZE, HC_VERSION, 0, 0, 0);
	writeHeader(&stream, &header);

	qint64 previousIndex = 0;
	for(int i = 0; i < keep.count(); ++i) {
		QString data = keep[i].data;
		qint64 dataPos = insertData(&stream, &data);
		qint64 newIndex = insertIndex(&stream, dataPos, keep[i].name, keep[i].date);
		updateIndex(&stream, previousIndex, newIndex);
		if(header.first == 0)
			header.first = newIndex;
		header.last = newIndex;
		header.count++;
		previousIndex = newIndex;
	}

	writeHeader(&stream, &header);
	return stream.status() == QDataStream::Ok && file.commit();
}
