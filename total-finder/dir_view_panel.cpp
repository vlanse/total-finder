#include "dir_view_panel.h"
#include "ui_dir_view_panel.h"

#include "create_dir.h"
#include "dir_model.h"
#include "edit_file.h"
#include "event_filters.h"
#include "find_in_files.h"
#include "settings.h"
#include "shell_utils.h"

#include <common/filesystem.h>

#include <QDebug>
#include <QDesktopServices>
#include <QMenu>
#include <QProcess>
#include <QUrl>

#include <functional>

namespace TotalFinder
{
  namespace
  {
    void PostKeyEvent(QWidget* widget, const QKeyEvent& event)
    {
      QKeyEvent * pressEvent = new QKeyEvent (QEvent::KeyPress, event.key(), event.modifiers(), event.text());
      QKeyEvent * releaseEvent = new QKeyEvent (QEvent::KeyRelease, event.key(), event.modifiers(), event.text());
      qApp->postEvent(widget, pressEvent);
      qApp->postEvent(widget, releaseEvent);
    }

    void EnsureFileExists(const QString& filePath)
    {
      QFile f(filePath);
      if (!f.exists())
      {
        f.open(QIODevice::ReadWrite);
        f.close();
      }
    }
  } // namespace

#include "dir_view_panel.moc"

  DirViewPanel::DirViewPanel(const TabContext& context, QWidget* parent)
    : BasePanel(parent)
    , Model(new DirModel(this))
    , CurrentRow(0)
    , Context(context)
  {
    Ui = new Ui_DirViewPanel();
    Ui->setupUi(this);
    Ui->SearchEdit->hide();

    // TODO: move in settings
    setFont(QFont("Menlo Regular", 11));

    Model->SetRoot(QDir("/"));
    Ui->DirView->setModel(Model);
    connect(Model, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)), this, SLOT(OnDirModelChange()));
    connect(
      Ui->DirView->selectionModel(),
      SIGNAL(currentChanged(const QModelIndex, const QModelIndex&)),
      SLOT(OnSelectionChanged(const QModelIndex&, const QModelIndex&))
    );

    connect(Ui->DirView->horizontalHeader(), SIGNAL(sectionResized(int, int, int)), SLOT(OnHeaderGeometryChanged()));
    Ui->DirView->horizontalHeader()->restoreState(Settings::LoadViewHeaderState());

    Ui->DirView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(Ui->DirView, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(OnShowViewContextMenu(const QPoint&)));

    FocusFilter* focusDetector = new FocusFilter(this);
    connect(focusDetector, SIGNAL(GotFocusEvent(QFocusEvent)), SLOT(OnFocusEvent(QFocusEvent)));
    Ui->DirView->installEventFilter(focusDetector);

    connect(Ui->DirView, SIGNAL(activated(const QModelIndex&)), SLOT(OnItemActivated(const QModelIndex&)));
    connect(Ui->AddressBar, SIGNAL(returnPressed()), SLOT(OnAddressBarEnter()));

    BasePanel::InstallKeyEventFilter(QWidgetList() << Ui->DirView << Ui->AddressBar);
  }

  QString DirViewPanel::GetName() const
  {
    return this->GetRootDir().isRoot() ? "/" : this->GetRootDir().dirName();
  }

  QFileInfo DirViewPanel::GetCurrentSelection() const
  {
    return CurrentSelection;
  }

  void DirViewPanel::OnShowViewContextMenu(const QPoint& point)
  {
    QMenu menu;
    QAction* revealAction = menu.addAction("Reveal in Finder");
    connect(revealAction, SIGNAL(triggered(bool)), SLOT(OnRevealInFinder()));

    QAction* openTerminalAction = menu.addAction("Open Terminal");
    connect(openTerminalAction, SIGNAL(triggered(bool)), SLOT(OnOpenTerminal()));

    menu.exec(Ui->DirView->viewport()->mapToGlobal(point));
  }

  void DirViewPanel::OnRevealInFinder()
  {
    Shell::RevealInFinder(CurrentSelection.absoluteFilePath());
  }

  void DirViewPanel::OnOpenTerminal()
  {
    Shell::OpenTerminal(Model->GetRoot().absolutePath());
  }

  void DirViewPanel::OnHeaderGeometryChanged()
  {
    qDebug() << "Save view header state";
    const QByteArray& state = Ui->DirView->horizontalHeader()->saveState();
    Settings::SaveViewHeaderState(state);
  }

  void DirViewPanel::OnDirModelChange()
  {
    // adjust selection after model has changed
    QModelIndex currentIndex = Model->GetIndex(CurrentSelection);
    qDebug() << currentIndex;
    if (!currentIndex.isValid())
    {
      currentIndex = Model->index(0, 0);
      if (Model->rowCount() >= CurrentRow)
      {
        // case when item has been deleted
        currentIndex = Model->index(CurrentRow - 1, 0);
      }
    }
    Ui->DirView->selectionModel()->setCurrentIndex(
      currentIndex,
      QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows
    );

    CurrentRow = currentIndex.row();
    CurrentSelection = Model->GetItem(currentIndex);
  }

  void DirViewPanel::OnSelectionChanged(const QModelIndex& current, const QModelIndex& /*previous*/)
  {
    if (!current.isValid())
    {
      return;
    }
    CurrentSelection = Model->GetItem(current);
    CurrentRow = current.row();
  }

  void DirViewPanel::SetRootDir(const QDir& dir)
  {
    if (!Model)
    {
      return;
    }
    HandleDirSelection(dir);
  }

  QDir DirViewPanel::GetRootDir() const
  {
    if (!Model)
    {
      return QDir();
    }
    return Model->GetRoot();
  }

  void DirViewPanel::SetFocus()
  {
    Ui->DirView->setFocus();
  }

  void DirViewPanel::OnFocusEvent(QFocusEvent event)
  {
    if (event.gotFocus())
    {
      emit ChangeSideRequest(false);
    }
  }

  void DirViewPanel::OnItemActivated(const QModelIndex& index)
  {
    HandleItemSelection(Model->GetItem(index).absoluteFilePath());
  }

  void DirViewPanel::OnAddressBarEnter()
  {
    HandleItemSelection(QFileInfo(Ui->AddressBar->text()));
    Ui->DirView->setFocus();
  }

  void DirViewPanel::KeyHandler(Qt::KeyboardModifiers modifiers, Qt::Key key, const QString& text)
  {
    if (modifiers == Qt::NoModifier)
    {
      if (key == Qt::Key_Return)
      {
        HandleItemSelection(CurrentSelection);
      }
      else if (key == Qt::Key_F4)
      {
        qDebug() << "Request to edit file detected:" << CurrentSelection.absoluteFilePath();
        Shell::OpenEditorForFile(CurrentSelection.absoluteFilePath());
      }
      else if (key == Qt::Key_Delete) // Fn + Backspace
      {
        qDebug() << "Request to delete item, path is" << CurrentSelection.absoluteFilePath();
        Filesys::RemoveDirRecursive(Filesys::Dir(CurrentSelection.absoluteFilePath().toStdWString()));
      }
      else if (key == Qt::Key_F7)
      {
        QString newDirPath = Model->GetRoot().absolutePath() + "/";
        CreateDirDialog* dlg = new CreateDirDialog(this);
        dlg->exec();

        newDirPath += dlg->GetDirName();
        qDebug() << "Request to create directory, path is" << newDirPath;
        Filesys::CreateDir(newDirPath.toStdWString());
      }
      else if (key == Qt::Key_F5)
      {
        if (!Context.IsOppositeTabDirView(this))
        {
          qDebug() << "Opposite tab is not dir view, skip copy request";
          return;
        }
        const QDir& dest = Context.GetOppositeTabRootDir(this);
        qDebug() << "Request to copy file or dir" << CurrentSelection.absoluteFilePath() << "to" << dest.absolutePath();
        Filesys::Copy(
          Filesys::FileInfo(CurrentSelection.absoluteFilePath().toStdWString()),
          Filesys::FileInfo(dest.absolutePath().toStdWString())
        );
      }
      else if (!text.isEmpty() && !(key == Qt::Key_Return || key == Qt::Key_Tab))
      {
        // this means that some alphanumeric key has been pressed
        // leaving this for later possible use
      }
    }
    else if (modifiers == Qt::ShiftModifier)
    {
      if (key == Qt::Key_F4)
      {
        QString filePath = Model->GetRoot().absolutePath() + "/";
        EditFileDialog* dlg = new EditFileDialog(this);
        dlg->exec();

        filePath += dlg->GetFileName();
        qDebug() << "Request to edit file by entered name, full path is" << filePath;
        EnsureFileExists(filePath);
        Shell::OpenEditorForFile(filePath);
      }
    }
    else if (modifiers == Qt::ControlModifier)
    {
      if (key == Qt::Key_F)
      {
        const QString& searchRoot = Model->GetRoot().absolutePath();
        qDebug() << "Find in files request, root dir" << searchRoot;
        FindInFilesDialog dlg(Filesys::Dir(searchRoot.toStdWString()), this);
        dlg.exec();
      }
    }
    else if (modifiers == Qt::MetaModifier)
    {
      if (key == Qt::Key_C)
      {
        qDebug() << "Open terminal request";
        Shell::OpenTerminal(Model->GetRoot().absolutePath());
      }
    }
    else if (modifiers == (Qt::KeypadModifier | Qt::ControlModifier))
    {
      if (key == Qt::Key_Up)
      {
        qDebug() << "Go to parent dir request";
        QDir currentRoot = GetRootDir();
        if (currentRoot.cdUp())
        {
          HandleDirSelection(currentRoot);
        }
      }
      else if (key == Qt::Key_Down)
      {
        HandleDirSelection(CurrentSelection.absoluteFilePath());
      }
    }
  }

  void DirViewPanel::HandleItemSelection(const QFileInfo& item)
  {
    if (item.isDir())
    {
      HandleDirSelection(item.absoluteFilePath());
      return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(item.absoluteFilePath()));
  }

  void DirViewPanel::HandleDirSelection(const QDir& dir)
  {
    qDebug() << "Set dir in view:" << dir.absolutePath();
    const QDir& previousDir = Model->GetRoot();
    Model->SetRoot(dir);
    Ui->AddressBar->setText(Model->GetRoot().absolutePath());

    const QModelIndex& index = Model->GetIndex(previousDir);
    Ui->DirView->setCurrentIndex(index.isValid() ? index : Model->index(0,0));

    Ui->DirView->scrollTo(Ui->DirView->selectionModel()->currentIndex());

    emit TitleChanged(GetName());
  }
} // namespace TotalFinder
