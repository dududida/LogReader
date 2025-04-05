#include "LogViewer.h"

#include <QSharedMemory>
#include <QTimer>
#include <QTableView>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QSortFilterProxyModel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QHeaderView>
#include <QPushButton>
#include <QMessageBox>

constexpr uint32_t SHMEM_SIZE = 10 * 1024 * 1024;
constexpr int LOG_SIZE = 256;

CLogViewer::CLogViewer(QString strShmemName, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CLogViewerClass())
    , shmem(strShmemName)
{
    ui->setupUi(this);

    SetupUI();
    InitDatabase();
    SetupConnections();
    ConnectSharedMemory();
    startTimer(100); // 每100ms读取一次
}

CLogViewer::~CLogViewer()
{
    delete ui;
}

void CLogViewer::Slot_SearchClicked()
{
    QString keyword = searchEdit->text();
    if (keyword.isEmpty()) return;

    // 遍历模型查找匹配项
    QModelIndexList matches = proxyModel->match(
        proxyModel->index(0, 1),
        Qt::DisplayRole,
        keyword,
        -1,
        Qt::MatchContains
    );

    if (!matches.isEmpty()) {
        tableView->scrollTo(matches.first());
        tableView->selectRow(matches.first().row());
    }
}

void CLogViewer::SetupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 筛选行
    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText("Filter Keys...");
    filterLayout->addWidget(filterEdit);

    // 搜索行
    QHBoxLayout* searchLayout = new QHBoxLayout();
    searchEdit = new QLineEdit(this);
    QPushButton* searchBtn = new QPushButton("Search", this);
    searchLayout->addWidget(searchEdit);
    searchLayout->addWidget(searchBtn);

    mainLayout->addLayout(filterLayout);
    mainLayout->addLayout(searchLayout);

    // 表格视图
    sourceModel = new QStandardItemModel(0, 2, this);
    sourceModel->setHorizontalHeaderLabels({ "Date", "Context" });

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(sourceModel);
    proxyModel->setFilterKeyColumn(1);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    tableView = new QTableView(this);
    tableView->setModel(proxyModel);
    tableView->setSortingEnabled(true);
    tableView->horizontalHeader()->setStretchLastSection(true);

    mainLayout->addWidget(tableView);

    // 连接信号
    connect(searchBtn, &QPushButton::clicked, this, &CLogViewer::Slot_SearchClicked);
}

void CLogViewer::InitDatabase()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("logs.db");
    if (!db.open()) {
        qFatal("无法打开数据库: %s", qPrintable(db.lastError().text()));
    }

    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "timestamp DATETIME, "
        "content TEXT)");
}

void CLogViewer::ConnectSharedMemory()
{
    if (!shmem.attach()) {
        QMessageBox::critical(this, "错误", "共享内存附加失败: " + shmem.errorString());
        return;
    }
}

void CLogViewer::SetupConnections()
{
    // 实时筛选延迟处理
    QTimer* filterTimer = new QTimer(this);
    filterTimer->setSingleShot(true);
    filterTimer->setInterval(300);

    connect(filterEdit, &QLineEdit::textChanged, [=] {
        filterTimer->start();
        });
    connect(filterTimer, &QTimer::timeout, [=] {
        proxyModel->setFilterFixedString(filterEdit->text());
        });
}

void CLogViewer::processLogs(char* start, uint32_t length, QVector<QString>& output)
{
    const int logCount = length / LOG_SIZE;
    for (int i = 0; i < logCount; ++i) {
        char* log = start + i * LOG_SIZE;
        if (log[0] != '\0') {
            output.append(QString::fromLocal8Bit(log));
        }
    }
}

//void CLogViewer::UpdateUIAndDatabase(const QVector<QString>& logs)
//{
//    QSqlDatabase db = QSqlDatabase::database();
//    db.transaction();
//
//    QSqlQuery query;
//    query.prepare("INSERT INTO logs (timestamp, content) VALUES (?, ?)");
//
//    foreach(const QString & log, logs) {
//        // 解析时间戳（根据写入格式）
//        QString timestamp = log.mid(0, 24);
//        QString content = log.mid(25);
//
//        // 插入数据库
//        query.addBindValue(timestamp);
//        query.addBindValue(content);
//        if (!query.exec()) {
//            qWarning() << "数据库插入失败:" << query.lastError();
//        }
//
//        // 更新表格
//        QList<QStandardItem*> row;
//        row << new QStandardItem(timestamp);
//        row << new QStandardItem(content);
//        sourceModel->appendRow(row);
//    }
//
//    db.commit();
//
//    // 自动滚动到底部
//    tableView->scrollToBottom();
//}

void CLogViewer::UpdateSystem(const QVector<QString>& logs)
{
    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    QSqlQuery query;
    query.prepare("INSERT INTO logs (timestamp, content) VALUES (?, ?)");

    foreach(const QString & log, logs) {
        QStringList parts = parseLog(log);
        if (parts.size() != 2) continue;

        // 数据库插入
        query.addBindValue(parts[0]);
        query.addBindValue(parts[1]);
        if (!query.exec()) {
            qWarning() << "插入失败:" << query.lastError();
        }

        // 更新表格
        QList<QStandardItem*> row;
        row << new QStandardItem(parts[0]);
        row << new QStandardItem(parts[1]);
        sourceModel->appendRow(row);
    }

    db.commit();
    //tableView->scrollToBottom();
}

QStringList CLogViewer::parseLog(const QString& log)
{
    // 假设日志格式: [时间] 内容
    int endIndex = log.indexOf("]");
    if (endIndex == -1) return {};

    return {
        log.mid(1, endIndex - 1).trimmed(),  // 时间部分
        log.mid(endIndex + 2).trimmed()      // 内容部分
    };
}

void CLogViewer::timerEvent(QTimerEvent*)
{
    if (!shmem.lock()) {
        qWarning() << "共享内存锁定失败:" << shmem.errorString();
        return;
    }

    char* data = static_cast<char*>(shmem.data());
    uint32_t writePos = *reinterpret_cast<uint32_t*>(data);
    uint32_t& readPos = *reinterpret_cast<uint32_t*>(data + 4);
    char* logArea = data + 8;
    const uint32_t logAreaSize = SHMEM_SIZE - 8;

    if (writePos != readPos) {
        QVector<QString> newLogs;

        if (writePos > readPos) {
            processLogs(logArea + readPos, writePos - readPos, newLogs);
        }
        else {
            processLogs(logArea + readPos, logAreaSize - readPos, newLogs);
            processLogs(logArea, writePos, newLogs);
        }

        readPos = writePos;
        UpdateSystem(newLogs);
    }

    shmem.unlock();
}
