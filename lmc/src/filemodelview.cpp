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


#include <QtGui>
#include <QPixmap>
#include <QFileInfo>
#include <QSaveFile>
#include <math.h>

#include "filemodelview.h"

namespace {
const quint32 TransferHistoryMagic = 0x4c4d4346; // LMCF
const quint32 TransferHistoryVersion = 2;

void restoreLegacyTransferDate(FileView& view) {
	// The first searchable-history build temporarily stored the date at the
	// end of fileDisplay. Preserve dates from those records while migrating
	// them to the dedicated startTime field.
	int separator = view.fileDisplay.lastIndexOf(" - ");
	if(separator < 0)
		return;

	QString dateText = view.fileDisplay.mid(separator + 3);
	QDateTime parsed = QDateTime::fromString(dateText, "yyyy-MM-dd HH:mm");
	if(parsed.isValid()) {
		view.startTime = parsed;
		view.fileDisplay = view.fileDisplay.left(separator);
	}
}
}

/****************************************************************************
**	Class: FileView
**	Description: Takes care of rendering the item
****************************************************************************/
FileView::FileView(QString id) {
	type = 0;
	fileSize = 0;
	position = 0;
	speed = 0;
	mode = TM_Max;
	state = TS_Max;
	posDisplay = tr("0 bytes");
	speedDisplay = tr("0 bytes/sec");
	timeDisplay = tr("Calculating time");
	if(!id.isNull())
		this->id = id;
}

QSize FileView::sizeHint() const {
	const int rowHeight = startTime.isValid() ? 74 : 56;
	switch(state) {
	case TS_Wait:
	case TS_Confirm:
	case TS_Complete:
	case TS_Cancel:
		return QSize(0, rowHeight);
		break;
	default:
		return QSize(0, rowHeight);
		break;
	}
}

void FileView::paint(QPainter* painter, const QRect& rect, const QPalette& palette, DisplayMode displayMode) const {
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);

	painter->translate(rect.x(), rect.y());

	if(displayMode == DM_Selected)
		painter->setBrush(palette.highlightedText());
	else
		painter->setBrush(palette.foreground());

	painter->setPen(QPen(painter->brush(), 1));
	
	int textFlags = Qt::AlignLeft | Qt::AlignVCenter;

	QFont font(painter->font());
	font.setBold(true);
	painter->setFont(font);

	QString nameDisplay = (mode == TM_Send) ? tr("To: ") : tr("From: ");
	nameDisplay.append(userName);
	QRect textRect = QRect(54, 0, rect.width() - 58, 18);
	QString text = painter->fontMetrics().elidedText(nameDisplay, Qt::ElideRight, textRect.width());
	painter->drawText(textRect, textFlags, text);
	
	font.setBold(false);
	painter->setFont(font);
	textRect = QRect(54, 18, rect.width() - 58, 18);
	text = painter->fontMetrics().elidedText(fileDisplay, Qt::ElideMiddle, textRect.width());
	painter->drawText(textRect, textFlags, text);

	bool drawProgress = false;
	QString stateDisplay;
	switch(state) {
	case TS_Send:
		stateDisplay = timeDisplay + " left - " + posDisplay + " of " + sizeDisplay + " (" + speedDisplay + ")";
		drawProgress = true;
		break;
	case TS_Receive:
		stateDisplay = timeDisplay + " left - " + posDisplay + " of " + sizeDisplay + " (" + speedDisplay + ")";
		drawProgress = true;
		break;
	case TS_Complete:
		stateDisplay = tr("Completed");
		break;
	case TS_Cancel:
		stateDisplay = tr("Canceled");
		break;
	case TS_Abort:
		stateDisplay = tr("Interrupted");
		break;
    default:
        break;
	}
	textRect = QRect(54, 36, rect.width() - 58, 18);
	text = painter->fontMetrics().elidedText(stateDisplay, Qt::ElideRight, textRect.width());
	painter->drawText(textRect, textFlags, text);

	if(startTime.isValid()) {
		QString dateDisplay = tr("Date: %1").arg(startTime.toString("yyyy-MM-dd HH:mm"));
		textRect = QRect(54, 54, rect.width() - 58, 18);
		text = painter->fontMetrics().elidedText(dateDisplay, Qt::ElideRight, textRect.width());
		painter->drawText(textRect, textFlags, text);
	}

	if(drawProgress) {
		int spanAngle = fileSize > 0 ? ((double)position / (double)fileSize) * 360 : 0;

		painter->setBrush(QBrush(QColor(230, 230, 230)));
		painter->setPen(QPen(QColor(230, 230, 230)));
		painter->drawPie(4, 4, 46, 46, (90 - spanAngle) * 16, -(360 - spanAngle) * 16);

		painter->setBrush(QBrush(QColor(150, 225, 110)));
		painter->setPen(QPen(QColor(150, 225, 110)));
		painter->drawPie(4, 4, 46, 46, 90 * 16, -spanAngle * 16);

		painter->setBrush(QBrush(QColor(255, 255, 255)));
		painter->setPen(QPen(QColor(255, 255, 255), 2));
		painter->drawLine(27, 5, 27, 49);
		painter->drawLine(5, 27, 49, 27);
		painter->drawLine(11, 11, 42, 42);
		painter->drawLine(42, 11, 11, 42);
	}

	QRect imageRect(11, 11, 32, 32);
	painter->drawPixmap(imageRect, icon);

	painter->restore();
}

QDataStream &operator << (QDataStream &out, const FileView &view) {
	out << view.id << qint32(view.mode) << view.filePath << view.userName << view.fileDisplay
		<< qint32(view.state) << view.icon << view.startTime;
	return out;
}

QDataStream &operator >> (QDataStream &in, FileView &view) {
	QString id;
	qint32 mode;
	QString filePath;
	QString userName;
	QString fileDisplay;
	qint32 state;
	QPixmap icon;
	QDateTime startTime;
	in >> id >> mode >> filePath >> userName >> fileDisplay >> state >> icon >> startTime;
	view = FileView(id);
	view.mode = (FileView::TransferMode)mode;
	view.filePath = filePath;
	view.userName = userName;
	view.fileDisplay = fileDisplay;
	view.state = (FileView::TransferState)state;
	view.icon = icon;
	view.startTime = startTime;
	return in;
}


/****************************************************************************
**	Class: FileDelegate
**	Description: Delegates rendering and user input
****************************************************************************/
void FileDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if(index.data().canConvert<FileView>()) {
        FileView fileView = index.data().value<FileView>();

		FileView::DisplayMode mode = FileView::DM_Normal;
		if(option.state & QStyle::State_Selected) {
			painter->fillRect(option.rect, option.palette.highlight());
			mode = FileView::DM_Selected;
		} else {
			if(index.row() % 2 == 1)
				painter->fillRect(option.rect, option.palette.alternateBase());
		}

		fileView.paint(painter, option.rect, option.palette, mode);
	}
	else {
		QStyledItemDelegate::paint(painter, option, index);
	}
}

QSize FileDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if(index.data().canConvert<FileView>()) {
        FileView fileView = index.data().value<FileView>();
		return fileView.sizeHint();
	}
	else {
		return QStyledItemDelegate::sizeHint(option, index);
	}
}


/****************************************************************************
**	Class: FileModel
**	Description: Handles the underlying data
****************************************************************************/
int FileModel::rowCount(const QModelIndex& index) const {
    Q_UNUSED(index);
	return transferList.count();
}

QVariant FileModel::data(const QModelIndex& index, int role) const {
	if(!index.isValid())
		return QVariant();

	if(index.row() >= transferList.size())
		return QVariant();

	if(role == Qt::DisplayRole || role == Qt::EditRole)
		return qVariantFromValue(transferList.at(index.row()));
	else
		return QVariant();
}

Qt::ItemFlags FileModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}

bool FileModel::insertRows(int position, int rows, const QModelIndex& index) {
	Q_UNUSED(index);
	if(position < 0 || position > transferList.size() || rows <= 0)
		return false;
	beginInsertRows(QModelIndex(), position, position + rows - 1);

	for(int row = 0; row < rows; row++) {
		FileView fileView;
		transferList.insert(position, fileView);
	}

	endInsertRows();
	return true;
}

bool FileModel::removeRows(int position, int rows, const QModelIndex& index) {
	Q_UNUSED(index);
	if(position < 0 || rows <= 0 || position + rows > transferList.size())
		return false;
	beginRemoveRows(QModelIndex(), position, position + rows - 1);

	for(int row = 0; row < rows; row++)
		transferList.removeAt(position);

	endRemoveRows();
	return true;
}

bool FileModel::setData(const QModelIndex &index, const QVariant &value, int role) {
	if(index.isValid() && index.model() == this && index.row() >= 0 &&
		index.row() < transferList.count() && role == Qt::UserRole) {
		int row = index.row();
		transferList[row] = value.value<FileView>();
		emit dataChanged(index, index);

		return true;
	}

	return false;
}

void FileModel::insertItem(int position, FileView* fileView) {
	this->insertRows(position, 1, QModelIndex());

	QModelIndex index = this->index(position);
	FileView view = *fileView;
	this->setData(index, qVariantFromValue(view), Qt::UserRole);
}

void FileModel::removeItem(int position) {
	removeRows(position, 1, QModelIndex());
}

FileView* FileModel::item(int position) {
	if(position < 0 || position >= transferList.count())
		return NULL;

	return &transferList[position];
}

FileView* FileModel::item(QString id) {
	for(int index = 0; index < transferList.count(); index++) {
		if(transferList[index].id.compare(id) == 0)
			return &transferList[index];
	}

	return NULL;
}

FileView* FileModel::item(QString id, FileView::TransferMode mode) {
	for(int index = 0; index < transferList.count(); index++) {
		if(transferList[index].id.compare(id) == 0 && transferList[index].mode == mode)
			return &transferList[index];
	}

	return NULL;
}

int FileModel::itemIndex(QString id, FileView::TransferMode mode) {
	for(int index = 0; index < transferList.count(); index++) {
		if(transferList[index].id.compare(id) == 0 && transferList[index].mode == mode)
			return index;
	}

	return -1;
}

void FileModel::itemChanged(int position) {
    if(position < 0 || position >= transferList.count())
        return;

	QModelIndex index = this->index(position);
	emit dataChanged(index, index);
}

void FileModel::loadData(QString filePath) {
	transferList.clear();
	
	QFile file(filePath);
	if(!file.open(QIODevice::ReadOnly))
		return;

	QDataStream stream(&file);
	quint32 marker = 0;
	stream >> marker;
	if(marker == TransferHistoryMagic) {
		quint32 version = 0;
		stream >> version;
		if(version == TransferHistoryVersion) {
			quint32 count = 0;
			stream >> count;
			if(count <= 100000) {
				for(quint32 index = 0; index < count; ++index) {
					FileView view;
					stream >> view;
					if(stream.status() != QDataStream::Ok ||
						view.mode < FileView::TM_Send || view.mode >= FileView::TM_Max ||
						view.state < FileView::TS_Wait || view.state >= FileView::TS_Max) {
						transferList.clear();
						break;
					}
					if(view.state < FileView::TS_Complete)
						view.state = FileView::TS_Abort;
					transferList.append(view);
				}
			}
		}
	} else {
		// Original transfer histories were an unversioned QList<FileView>.
		// The first value is therefore the item count already read as marker.
		const quint32 count = marker;
		if(count <= 100000) {
			for(quint32 index = 0; index < count; ++index) {
				QString id;
				qint32 mode;
				QString filePath;
				QString userName;
				QString fileDisplay;
				qint32 state;
				QPixmap icon;
				stream >> id >> mode >> filePath >> userName >> fileDisplay >> state >> icon;
				if(stream.status() != QDataStream::Ok) {
					transferList.clear();
					break;
				}

				FileView view(id);
				view.mode = (FileView::TransferMode)mode;
				view.filePath = filePath;
				view.userName = userName;
				view.fileDisplay = fileDisplay;
				if(mode < FileView::TM_Send || mode >= FileView::TM_Max ||
					state < FileView::TS_Wait || state >= FileView::TS_Max) {
					transferList.clear();
					break;
				}
				view.state = (state < FileView::TS_Complete)
					? FileView::TS_Abort : (FileView::TransferState)state;
				view.icon = icon;
				restoreLegacyTransferDate(view);
				transferList.append(view);
			}
		}
	}

	file.close();
}

void FileModel::saveData(QString filePath) {
	QDir dir = QFileInfo(filePath).dir();
	if(!dir.exists())
		dir.mkpath(dir.absolutePath());

	QSaveFile file(filePath);
	if(!file.open(QIODevice::WriteOnly))
		return;

	QDataStream stream(&file);
	stream << TransferHistoryMagic << TransferHistoryVersion << transferList;

	file.commit();
}
