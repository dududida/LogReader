#pragma once

#include <QtWidgets/QWidget>
#include <QSharedMemory>

#include "ui_CLogViewer.h"

class QStandardItemModel;
class QSortFilterProxyModel;
class QTableView;
class QLineEdit;

QT_BEGIN_NAMESPACE
namespace Ui { class CLogViewerClass; };
QT_END_NAMESPACE

class CLogViewer : public QWidget
{
    Q_OBJECT

public:
    CLogViewer(QString strShmemName,QWidget *parent = nullptr);
    ~CLogViewer();

private slots:
    void Slot_SearchClicked();

private:
    void SetupUI();

    void InitDatabase();

    void ConnectSharedMemory();
    
    void SetupConnections();

    void processLogs(char* start, uint32_t length, QVector<QString>& output);

    //void updateUIAndDatabase(const QVector<QString>& logs);

    void UpdateSystem(const QVector<QString>& logs);

    QStringList parseLog(const QString& log);

    void timerEvent(QTimerEvent*) override;


private:
    Ui::CLogViewerClass *ui;

    QSharedMemory shmem;
    QStandardItemModel* sourceModel;
    QSortFilterProxyModel* proxyModel;
    QTableView* tableView;
    QLineEdit* filterEdit;
    QLineEdit* searchEdit;
};
