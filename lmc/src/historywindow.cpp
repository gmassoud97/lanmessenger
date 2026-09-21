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

#include <QDesktopWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QSaveFile>
#include <QTextDocument>
#include "historywindow.h"
#include "strings.h"

lmcHistoryWindow::lmcHistoryWindow(QWidget *parent, Qt::WindowFlags flags) : QWidget(parent, flags) {
	ui.setupUi(this);
	ui.tvMsgList->header()->setSortIndicatorShown(true);
	ui.tvMsgList->header()->setSortIndicator(1, Qt::DescendingOrder);

	setAttribute(Qt::WA_DeleteOnClose, true);

	pMessageLog = new lmcMessageLog(ui.fraMessageLog);
	ui.logLayout->addWidget(pMessageLog);
	pMessageLog->setAcceptDrops(false);

	QList<int> sizes;
	sizes.append(width() * 0.35);
	sizes.append(width() - width() * 0.35 - ui.splitter->handleWidth());
	ui.splitter->setSizes(sizes);
	QRect scr = QApplication::desktop()->screenGeometry();
	move(scr.center() - rect().center());

	connect(ui.tvMsgList, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
		this, SLOT(tvMsgList_currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)));
	connect(ui.btnClearHistory, SIGNAL(clicked()), this, SLOT(btnClearHistory_clicked()));
	connect(ui.btnDeleteHistory, SIGNAL(clicked()), this, SLOT(btnDeleteHistory_clicked()));
	connect(ui.txtSearch, SIGNAL(textChanged(QString)), this, SLOT(txtSearch_textChanged(QString)));
	connect(ui.btnExportHistory, SIGNAL(clicked()), this, SLOT(btnExportHistory_clicked()));

    ui.tvMsgList->installEventFilter(this);
    pMessageLog->installEventFilter(this);
	ui.txtSearch->installEventFilter(this);
	ui.btnDeleteHistory->installEventFilter(this);
	ui.btnExportHistory->installEventFilter(this);
    ui.btnClearHistory->installEventFilter(this);
    ui.btnClose->installEventFilter(this);
}

lmcHistoryWindow::~lmcHistoryWindow() {
}

void lmcHistoryWindow::init(void) {
	setWindowIcon(QIcon(IDR_APPICON));
    ui.splitter->setStyleSheet("QSplitter::handle { image: url(" IDR_HGRIP "); }");

	pMessageLog->setAutoScroll(false);

	pSettings = new lmcSettings();
	restoreGeometry(pSettings->value(IDS_WINDOWHISTORY).toByteArray());
	ui.splitter->restoreState(pSettings->value(IDS_SPLITTERHISTORY).toByteArray());
	setUIText();

	displayList();
}

void lmcHistoryWindow::updateList(void) {
	displayList();
}

void lmcHistoryWindow::stop(void) {
	pSettings->setValue(IDS_WINDOWHISTORY, saveGeometry());
	pSettings->setValue(IDS_SPLITTERHISTORY, ui.splitter->saveState());
}

void lmcHistoryWindow::settingsChanged(void) {
}

bool lmcHistoryWindow::eventFilter(QObject* pObject, QEvent* pEvent) {
    if(pEvent->type() == QEvent::KeyPress) {
        QKeyEvent* pKeyEvent = static_cast<QKeyEvent*>(pEvent);
        if(pKeyEvent->key() == Qt::Key_Escape) {
            close();
            return true;
        }
		if(pObject == ui.tvMsgList && pKeyEvent->key() == Qt::Key_Delete) {
			deleteSelectedHistory();
			return true;
		}
    }

    return false;
}

void lmcHistoryWindow::changeEvent(QEvent* pEvent) {
	switch(pEvent->type()) {
	case QEvent::LanguageChange:
		setUIText();
		break;
    default:
        break;
	}

	QWidget::changeEvent(pEvent);
}

void lmcHistoryWindow::tvMsgList_currentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous) {
    Q_UNUSED(previous);
	ui.btnDeleteHistory->setEnabled(current != NULL);

	if(current) {
		qint64 offset = current->data(0, DataRole).toLongLong();
		QString data = History::getMessage(offset);
		// Keep an ordinary horizontal space between the timestamp and message.
		// This also repairs older saved history when it is displayed.
		data.replace("</span><span class='message'>", "</span> <span class='message'>");
		data.replace("</span><span class=\"message\">", "</span> <span class=\"message\">");

		pMessageLog->setHtml(data);
		highlightSearchText();
	} else {
		pMessageLog->setHtml("<html></html>");
	}
}

void lmcHistoryWindow::btnClearHistory_clicked(void) {
	if(QMessageBox::warning(this, tr("Clear Message History"),
		tr("Are you sure you want to delete all message history? This cannot be undone."),
		QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
		return;

	QFile::remove(History::historyFile());
	displayList();
}

void lmcHistoryWindow::btnDeleteHistory_clicked(void) {
	deleteSelectedHistory();
}

bool lmcHistoryWindow::deleteSelectedHistory(void) {
	QTreeWidgetItem* current = ui.tvMsgList->currentItem();
	if(!current)
		return false;

	QString name = current->text(0);
	QString date = current->text(1);
	if(QMessageBox::question(this, tr("Delete History Record"),
		tr("Delete the selected history record for %1 from %2?").arg(name, date),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return false;

	qint64 offset = current->data(0, DataRole).toLongLong();
	if(!History::remove(offset)) {
		QMessageBox::warning(this, tr("Delete History Record"),
			tr("The selected history record could not be deleted."));
		return false;
	}

	displayList();
	return true;
}

void lmcHistoryWindow::txtSearch_textChanged(const QString& text) {
	populateList(text.trimmed());
}

void lmcHistoryWindow::btnExportHistory_clicked(void) {
	QString fileName = QFileDialog::getSaveFileName(this, tr("Export Message History"),
		QString("LAN-Messenger-History-%1.txt").arg(QDate::currentDate().toString("yyyy-MM-dd")),
		tr("Text files (*.txt);;All files (*)"));
	if(fileName.isEmpty())
		return;

	QByteArray output;
	for(int index = 0; index < msgList.count(); index++) {
		const MsgInfo& info = msgList[index];
		output.append("============================================================\r\n");
		output.append(info.name.toUtf8());
		output.append(" - ");
		output.append(info.date.toString(Qt::ISODate).toUtf8());
		output.append("\r\n============================================================\r\n");
		output.append(messagePlainText(info.offset).toUtf8());
		output.append("\r\n\r\n");
	}

	QSaveFile file(fileName);
	if(!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) {
		QMessageBox::warning(this, tr("Export Message History"),
			tr("%1 could not write the history file.").arg(lmcStrings::appName()));
		return;
	}

	QMessageBox::information(this, tr("Export Message History"),
		tr("Message history was exported successfully."));
}

void lmcHistoryWindow::setUIText(void) {
	ui.retranslateUi(this);

	setWindowTitle(tr("Message History"));
}

void lmcHistoryWindow::displayList(void) {
	pMessageLog->setHtml("<html></html>");
	msgList.clear();
	messageTextCache.clear();

	msgList = History::getList();
	populateList(ui.txtSearch->text().trimmed());
	ui.btnExportHistory->setEnabled(!msgList.isEmpty());
	ui.btnClearHistory->setEnabled(!msgList.isEmpty());
	ui.btnDeleteHistory->setEnabled(ui.tvMsgList->currentItem() != NULL);
}

void lmcHistoryWindow::populateList(const QString& searchText) {
	const int sortColumn = ui.tvMsgList->header()->sortIndicatorSection();
	const Qt::SortOrder sortOrder = ui.tvMsgList->header()->sortIndicatorOrder();
	ui.tvMsgList->setSortingEnabled(false);
	ui.tvMsgList->clear();

	for(int index = 0; index < msgList.count(); index++) {
		const MsgInfo& info = msgList[index];
		if(!searchText.isEmpty() &&
			!info.name.contains(searchText, Qt::CaseInsensitive) &&
			!messagePlainText(info.offset).contains(searchText, Qt::CaseInsensitive))
			continue;

		lmcHistoryTreeWidgetItem* pItem = new lmcHistoryTreeWidgetItem();
		pItem->setText(0, info.name);
		pItem->setText(1, info.date.toString(Qt::SystemLocaleDate));
		pItem->setData(0, DataRole, info.offset);
		pItem->setData(1, DataRole, info.date);
		pItem->setSizeHint(0, QSize(0, 20));
		ui.tvMsgList->addTopLevelItem(pItem);
	}

	ui.tvMsgList->setSortingEnabled(true);
	ui.tvMsgList->sortByColumn(sortColumn, sortOrder);
	ui.tvMsgList->header()->setSortIndicator(sortColumn, sortOrder);

	if(ui.tvMsgList->topLevelItemCount() > 0)
		ui.tvMsgList->setCurrentItem(ui.tvMsgList->topLevelItem(0));
	else {
		pMessageLog->setHtml("<html></html>");
		ui.btnDeleteHistory->setEnabled(false);
	}
}

QString lmcHistoryWindow::messagePlainText(qint64 offset) {
	if(messageTextCache.contains(offset))
		return messageTextCache.value(offset);

	QTextDocument document;
	document.setHtml(History::getMessage(offset));
	QString text = document.toPlainText();
	messageTextCache.insert(offset, text);
	return text;
}

void lmcHistoryWindow::highlightSearchText(void) {
	QList<QTextEdit::ExtraSelection> selections;
	QString searchText = ui.txtSearch->text().trimmed();
	if(!searchText.isEmpty()) {
		QTextCursor cursor(pMessageLog->document());
		QTextCharFormat format;
		format.setBackground(QColor(255, 235, 59));
		format.setForeground(Qt::black);

		while(!(cursor = pMessageLog->document()->find(searchText, cursor)).isNull()) {
			QTextEdit::ExtraSelection selection;
			selection.cursor = cursor;
			selection.format = format;
			selections.append(selection);
		}
	}

	pMessageLog->setExtraSelections(selections);
	if(!selections.isEmpty())
		pMessageLog->setTextCursor(selections.first().cursor);
}
