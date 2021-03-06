#include "find_in_files.h"
#include "ui_find_in_files.h"
#include "settings.h"
#include "shell_utils.h"


#include <QDebug>
#include <QThread>
#include <QElapsedTimer>

#include <string>
#include <functional>

namespace TotalFinder
{
  class Worker: public QThread
  {
    Q_OBJECT
  public:
    Worker(QObject* parent);
    void StartSearch(const QString& where, const QString& what, const QString& content);
    void Cancel();
  protected:
    virtual void run();
  signals:
    void GotResult(const QString& item);
    void Progress(const QString& currentFolder);
    void Complete();
  private:
    bool FilterCallback(const std::string& filename, Filesys::FileObjectType ftype);

    bool CancelFlag;
    QString Where;
    QRegExp What;
    QString Content;
    QDir::Filters DirFilters;
  };

  class SearchResultModel: public QAbstractListModel
  {
    Q_OBJECT
    const qint64 MODEL_UPDATE_INTERVAL_MILLISECONDS = 1000;

  public:
    SearchResultModel(QObject* parent);
    void AddItem(const QString& item);
    void FlushResults();
    void Clear();
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex& index = QModelIndex(), int role = Qt::DisplayRole) const;
  private:
    void FlushReadyToInsertRows();

    QStringList ModelData;
    QStringList RowsReadyToInsert;
    QElapsedTimer RefreshTimer;
  };

 #include "find_in_files.moc"

  Worker::Worker(QObject* parent)
    : QThread(parent)
    , CancelFlag(false)
    , DirFilters(Settings::LoadDirFilters())
  {
  }

  void Worker::StartSearch(const QString& where, const QString& what, const QString& content)
  {
    Where = where;
    What = QRegExp(what);
    What.setCaseSensitivity(Qt::CaseInsensitive);
    What.setPatternSyntax(QRegExp::Wildcard);
    Content = content;
    qDebug() << "Search started; where:" << where << ", content:" << Content;
    start();
  }

  void Worker::Cancel()
  {
    if (!isRunning())
    {
      return;
    }
    CancelFlag = true;
    QThread::wait();
    qDebug() << "Search canceled";
  }

  void Worker::run()
  {
    qDebug() << "Start search";

    Filesys::WalkDir(
      Filesys::Dir(Where.toStdWString()),
      std::bind(
        &Worker::FilterCallback,
        this,
        std::placeholders::_1,
        std::placeholders::_2
      ),
      false
    );

    emit Complete();
  }

  bool Worker::FilterCallback(const std::string& filename, Filesys::FileObjectType ftype)
  {
    if (CancelFlag)
    {
      QThread::terminate();
      return false;
    }

    const QString& fullPath = QString::fromStdString(filename);
    const int lastSep = fullPath.lastIndexOf(Filesys::PATH_SEPARATOR);
    const QString& folder = fullPath.right(fullPath.size() - lastSep - 1);

    if (ftype == Filesys::FILE_DIRECTORY)
    {
      // TODO: use actual file attributes
      if (folder.startsWith(".") && !(DirFilters & QDir::Hidden))
      {
        qDebug() << "exclude from search" << folder;
        return false;
      }
      emit Progress(QString::fromStdString(filename));
    }

    const QStringList& items = fullPath.split(Filesys::PATH_SEPARATOR);
    if (items.empty())
    {
      return true;
    }
    const QString& item = items.last();

    if (What.exactMatch(item))
    {
      if (Content.isEmpty())
      {
        emit GotResult(fullPath);
        return true;
      }

      if (ftype == Filesys::FILE_REGULAR)
      {
        QFile f(fullPath);
        f.open(QIODevice::ReadOnly);
        QString chunk;
        do
        {
          if (CancelFlag)
          {
            QThread::terminate();
          }

          chunk = f.read(1024);
          if (chunk.indexOf(Content, Qt::CaseInsensitive) != -1)
          {
            emit GotResult(fullPath);
            return true;
          }
        }
        while (!chunk.isEmpty());
        f.close();
      }
    }
    return true;
  }

  SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractListModel(parent)
  {
  }

  void SearchResultModel::FlushReadyToInsertRows()
  {
    const QModelIndex parent = QModelIndex();
    const int rows = rowCount(parent);
    QAbstractListModel::beginInsertRows(parent, rows, rows);
    ModelData << RowsReadyToInsert;
    QAbstractListModel::endInsertRows();
    RefreshTimer.invalidate();
  }

  void SearchResultModel::AddItem(const QString& item)
  {
    if (!RefreshTimer.isValid())
    {
      RefreshTimer.start();
    }
    if (RefreshTimer.elapsed() < MODEL_UPDATE_INTERVAL_MILLISECONDS)
    {
      RowsReadyToInsert << item;
    }
    else
    {
      FlushReadyToInsertRows();
      RefreshTimer.start();
    }
  }

  void SearchResultModel::FlushResults()
  {
    FlushReadyToInsertRows();
    RowsReadyToInsert.clear();
  }

  void SearchResultModel::Clear()
  {
    const QModelIndex parent = QModelIndex();
    QAbstractListModel::beginRemoveRows(parent, 0, rowCount() - 1);
    RowsReadyToInsert.clear();
    ModelData.clear();
    QAbstractListModel::endRemoveRows();
  }

  int SearchResultModel::rowCount(const QModelIndex& parent) const
  {
    return ModelData.size();
  }

  QVariant SearchResultModel::data(const QModelIndex& index, int role) const
  {
    if (!index.isValid())
    {
      return QVariant();
    }

    const QString& currentItem = ModelData[index.row()];

   if (role == Qt::DisplayRole)
   {
     return currentItem;
   }

   return QVariant();
  }

  FindInFilesDialog::FindInFilesDialog(const Filesys::Dir& startDir, QWidget* parent)
    : QDialog(parent)
    , Ui(new Ui_FindInFilesDialog)
    , Searcher(new Worker(this))
    , Model(new SearchResultModel(this))
  {
    Ui->setupUi(this);
    Ui->SearchInEdit->setText(QString::fromStdWString(startDir.GetPath()));
    connect(Ui->ResultView, SIGNAL(activated(const QModelIndex&)), SLOT(OnResultItemActivated(const QModelIndex&)));
    Ui->ResultView->setModel(Model);
    Ui->ResultView->setLayoutMode(QListView::Batched);
    Ui->ResultView->setBatchSize(10);
    connect(Searcher, SIGNAL(GotResult(const QString&)), SLOT(OnGotResult(const QString&)));
    connect(Searcher, SIGNAL(Progress(const QString&)), SLOT(OnProgress(const QString&)));
    connect(Searcher, SIGNAL(Complete()), SLOT(OnComplete()));
    Ui->FilenameMaskEdit->setFocus();
  }

  FindInFilesDialog::~FindInFilesDialog()
  {
    Searcher->Cancel();
  }

  void FindInFilesDialog::OnResultItemActivated(const QModelIndex& item)
  {
    Shell::OpenEditorForFile(Model->data(item, Qt::DisplayRole).toString());
  }

  void FindInFilesDialog::StartSearch()
  {
    Model->Clear();
    Searcher->StartSearch(
      Ui->SearchInEdit->text(),
      Ui->FilenameMaskEdit->lineEdit()->text(),
      Ui->FindTextEdit->lineEdit()->text()
    );
  }

  void FindInFilesDialog::OnGotResult(const QString& result)
  {
    Model->AddItem(result);
  }

  void FindInFilesDialog::OnProgress(const QString& folder)
  {
    Ui->ProgressLabel->setText(folder);
  }

  void FindInFilesDialog::OnComplete()
  {
    Model->FlushResults();
    Ui->ResultView->setFocus();
    qDebug() << "search complete, found" << Model->rowCount() << "items";
    Ui->ProgressLabel->setText(QString("Search complete, %1 items found").arg(Model->rowCount()));
  }

  void FindInFilesDialog::keyPressEvent(QKeyEvent* event)
  {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
      StartSearch();
    }
    else if (event->key() == Qt::Key_Escape)
    {
      accept();
    }
    QDialog::keyPressEvent(event);
  }
} // namespace TotalFinder
