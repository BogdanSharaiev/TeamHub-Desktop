#include "filebrowser.h"
#include <QVBoxLayout>
#include <QDir>
#include <QDirIterator>

FileBrowser::FileBrowser(QWidget* parent)
    : QWidget(parent)
{
    setupFileBrowser();
    setupFilter();
}

void FileBrowser::setupFileBrowser()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search files...");
    searchBox->setObjectName("fileSearch");
    layout->addWidget(searchBox);

    model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());

    tree = new QTreeView(this);
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::homePath()));
    tree->setHeaderHidden(true);
    tree->hideColumn(1);
    tree->hideColumn(2);
    tree->hideColumn(3);
    tree->setIndentation(16);
    tree->setAnimated(true);
    tree->setUniformRowHeights(true);
    tree->setObjectName("fileBrowserTree");
    layout->addWidget(tree);

    connect(tree, &QTreeView::doubleClicked,
            this, &FileBrowser::onItemDoubleClicked);

    connect(searchBox, &QLineEdit::textChanged,
            this, [this](const QString& text) {
                if (text.isEmpty()) {
                    model->setNameFilters({"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md"});
                } else {
                    model->setNameFilters({"*" + text + "*"});
                }
            });

    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &FileBrowser::showContextMenu);
}

void FileBrowser::setupFilter()
{
    model->setNameFilters({"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md"});
    model->setNameFilterDisables(false);
}

void FileBrowser::setRootPath(const QString& path)
{
    model->setRootPath(path);
    tree->setRootIndex(model->index(path));
}

QString FileBrowser::rootPath() const
{
    return model->rootPath();
}

QString FileBrowser::findFile(const QString& filename){
    QString root = rootPath();
    QDirIterator it(root,
        QStringList() << filename,
        QDir::Files,
        QDirIterator::Subdirectories);

    if(it.hasNext()){
        return it.next();
    }
    return "";
}

void FileBrowser::onItemDoubleClicked(const QModelIndex& index)
{
    if (model->isDir(index)) return;

    QString path = model->filePath(index);
    emit fileDoubleClicked(path);
}

void FileBrowser::showContextMenu(const QPoint& pos)
{
    QModelIndex index = tree->indexAt(pos);

    QMenu menu(this);

    if (index.isValid()) {
        menu.addAction("New File",   this, &FileBrowser::newFile);
        menu.addAction("New Folder", this, &FileBrowser::newFolder);
        menu.addSeparator();
        menu.addAction("Cut",    this, &FileBrowser::cutSelected);
        menu.addAction("Copy",   this, &FileBrowser::copySelected);
        menu.addAction("Paste",  this, &FileBrowser::pasteToSelected);
        menu.addSeparator();
        menu.addAction("Rename", this, &FileBrowser::renameSelected);
        menu.addAction("Delete", this, &FileBrowser::deleteSelected);
    } else {
        menu.addAction("New File",   this, &FileBrowser::newFile);
        menu.addAction("New Folder", this, &FileBrowser::newFolder);
        menu.addAction("Paste",      this, &FileBrowser::pasteToSelected);
    }

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void FileBrowser::deleteSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid()) return;

    QString path = model->filePath(index);
    QFileInfo info(path);

    auto btn = QMessageBox::question(this, "Delete",
                                     "Delete " + info.fileName() + "?",
                                     QMessageBox::Yes | QMessageBox::No);

    if (btn != QMessageBox::Yes) return;

    if (info.isDir())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

void FileBrowser::copySelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid()) return;
    clipboardPath = model->filePath(index);
    isCut = false;
}

void FileBrowser::cutSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid()) return;
    clipboardPath = model->filePath(index);
    isCut = true;
}

void FileBrowser::pasteToSelected()
{
    if (clipboardPath.isEmpty()) return;

    QModelIndex index = tree->currentIndex();
    QString destDir;

    if (index.isValid()) {
        QString selectedPath = model->filePath(index);
        destDir = model->isDir(index)
                      ? selectedPath
                      : QFileInfo(selectedPath).absolutePath();
    } else {
        destDir = model->rootPath();
    }

    QString fileName = QFileInfo(clipboardPath).fileName();
    QString destPath = destDir + "/" + fileName;

    if (isCut) {
        QFile::rename(clipboardPath, destPath);
        clipboardPath.clear();
        isCut = false;
    } else {
        QFile::copy(clipboardPath, destPath);
    }
}

void FileBrowser::newFile()
{
    QModelIndex index = tree->currentIndex();
    QString dir = index.isValid() && model->isDir(index)
                      ? model->filePath(index)
                      : (index.isValid()
                             ? QFileInfo(model->filePath(index)).absolutePath()
                             : model->rootPath());

    bool ok;
    QString name = QInputDialog::getText(this, "New File",
                                         "File name:", QLineEdit::Normal,
                                         "new_file.py", &ok);
    if (!ok || name.isEmpty()) return;

    QFile file(dir + "/" + name);
    file.open(QIODevice::WriteOnly);
    file.close();
}

void FileBrowser::newFolder()
{
    QModelIndex index = tree->currentIndex();
    QString dir = index.isValid() && model->isDir(index)
                      ? model->filePath(index)
                      : (index.isValid()
                             ? QFileInfo(model->filePath(index)).absolutePath()
                             : model->rootPath());

    bool ok;
    QString name = QInputDialog::getText(this, "New Folder",
                                         "Folder name:", QLineEdit::Normal,
                                         "new_folder", &ok);
    if (!ok || name.isEmpty()) return;

    QDir(dir).mkdir(name);
}

void FileBrowser::renameSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid()) return;

    QString oldPath = model->filePath(index);
    QString oldName = QFileInfo(oldPath).fileName();
    QString dir     = QFileInfo(oldPath).absolutePath();

    bool ok;
    QString newName = QInputDialog::getText(this, "Rename",
                                            "New name:", QLineEdit::Normal,
                                            oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;

    QFile::rename(oldPath, dir + "/" + newName);
}
