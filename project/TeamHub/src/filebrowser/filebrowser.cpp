#include "filebrowser.h"
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QHeaderView>
#include <QStyle>
#include <QVBoxLayout>
#include <functional>

FileBrowser::FileBrowser(QWidget *parent)
    : QWidget(parent)
{
    setupFileBrowser();
    setupFilter();
}

void FileBrowser::setupFileBrowser()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search files...");
    searchBox->setObjectName("fileSearch");
    layout->addWidget(searchBox);

    stack = new QStackedWidget(this);
    layout->addWidget(stack);

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

    connect(tree, &QTreeView::doubleClicked, this, &FileBrowser::onItemDoubleClicked);
    connect(searchBox, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.isEmpty())
            model->setNameFilters({"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md"});
        else
            model->setNameFilters({"*" + text + "*"});
    });
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested, this, &FileBrowser::showContextMenu);

    stack->addWidget(tree);

    remoteTree = new QTreeWidget(this);
    remoteTree->setHeaderHidden(true);
    remoteTree->setIndentation(16);
    remoteTree->setObjectName("fileBrowserTree");
    remoteTree->setContextMenuPolicy(Qt::NoContextMenu);
    remoteTree->setAnimated(true);
    remoteTree->setUniformRowHeights(true);

    connect(remoteTree, &QTreeWidget::itemClicked, this, &FileBrowser::onRemoteItemDoubleClicked);

    stack->addWidget(remoteTree);
    stack->setCurrentIndex(0);
}

void FileBrowser::setupFilter()
{
    model->setNameFilters({"*.py", "*.cpp", "*.h", "*.pro", "*.txt", "*.md"});
    model->setNameFilterDisables(false);
}

void FileBrowser::setRootPath(const QString &path)
{
    model->setRootPath(path);
    tree->setRootIndex(model->index(path));
}

QString FileBrowser::rootPath() const
{
    return model->rootPath();
}

QString FileBrowser::findFile(const QString &filename)
{
    QDirIterator it(rootPath(),
                    QStringList() << filename,
                    QDir::Files,
                    QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString();
}

void FileBrowser::setRemoteFiles(const QStringList &relPaths)
{
    remoteTree->clear();

    const QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    QMap<QString, QTreeWidgetItem *> dirItems;

    std::function<QTreeWidgetItem *(const QString &)> getOrCreateDir =
        [&](const QString &dirPath) -> QTreeWidgetItem * {
        if (dirPath.isEmpty())
            return nullptr;
        if (dirItems.contains(dirPath))
            return dirItems[dirPath];

        const int sep = dirPath.lastIndexOf('/');
        QString parentPath = sep >= 0 ? dirPath.left(sep) : QString();
        QString name = sep >= 0 ? dirPath.mid(sep + 1) : dirPath;

        QTreeWidgetItem *parentItem = getOrCreateDir(parentPath);
        QTreeWidgetItem *item = parentItem ? new QTreeWidgetItem(parentItem, QStringList(name))
                                           : new QTreeWidgetItem(remoteTree, QStringList(name));
        item->setIcon(0, folderIcon);
        item->setData(0, Qt::UserRole, QString());
        dirItems[dirPath] = item;
        return item;
    };

    for (const QString &relPath : relPaths) {
        const int sep = relPath.lastIndexOf('/');
        QString dirPart = sep >= 0 ? relPath.left(sep) : QString();
        QString fileName = sep >= 0 ? relPath.mid(sep + 1) : relPath;

        QTreeWidgetItem *parent = getOrCreateDir(dirPart);
        QTreeWidgetItem *fileItem = parent ? new QTreeWidgetItem(parent, QStringList(fileName))
                                           : new QTreeWidgetItem(remoteTree, QStringList(fileName));
        fileItem->setIcon(0, fileIcon);
        fileItem->setData(0, Qt::UserRole, relPath);
    }

    remoteTree->expandAll();
    stack->setCurrentIndex(1);
    searchBox->setEnabled(false);
}

void FileBrowser::clearRemoteMode()
{
    remoteTree->clear();
    stack->setCurrentIndex(0);
    searchBox->setEnabled(true);
}

void FileBrowser::onItemDoubleClicked(const QModelIndex &index)
{
    if (model->isDir(index))
        return;
    emit fileDoubleClicked(model->filePath(index));
}

void FileBrowser::onRemoteItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    const QString relPath = item->data(0, Qt::UserRole).toString();
    if (!relPath.isEmpty())
        emit fileDoubleClicked(relPath);
}

void FileBrowser::showContextMenu(const QPoint &pos)
{
    QModelIndex index = tree->indexAt(pos);
    QMenu menu(this);

    if (index.isValid()) {
        menu.addAction("New File", this, &FileBrowser::newFile);
        menu.addAction("New Folder", this, &FileBrowser::newFolder);
        menu.addSeparator();
        menu.addAction("Cut", this, &FileBrowser::cutSelected);
        menu.addAction("Copy", this, &FileBrowser::copySelected);
        menu.addAction("Paste", this, &FileBrowser::pasteToSelected);
        menu.addSeparator();
        menu.addAction("Rename", this, &FileBrowser::renameSelected);
        menu.addAction("Delete", this, &FileBrowser::deleteSelected);
    } else {
        menu.addAction("New File", this, &FileBrowser::newFile);
        menu.addAction("New Folder", this, &FileBrowser::newFolder);
        menu.addAction("Paste", this, &FileBrowser::pasteToSelected);
    }

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

void FileBrowser::deleteSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid())
        return;
    QString path = model->filePath(index);
    QFileInfo info(path);
    auto btn = QMessageBox::question(this,
                                     "Delete",
                                     "Delete " + info.fileName() + "?",
                                     QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes)
        return;
    if (info.isDir())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

void FileBrowser::copySelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid())
        return;
    clipboardPath = model->filePath(index);
    isCut = false;
}

void FileBrowser::cutSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid())
        return;
    clipboardPath = model->filePath(index);
    isCut = true;
}

void FileBrowser::pasteToSelected()
{
    if (clipboardPath.isEmpty())
        return;
    QModelIndex index = tree->currentIndex();
    QString destDir;
    if (index.isValid()) {
        QString selectedPath = model->filePath(index);
        destDir = model->isDir(index) ? selectedPath : QFileInfo(selectedPath).absolutePath();
    } else {
        destDir = model->rootPath();
    }
    QString fileName = QFileInfo(clipboardPath).fileName();
    QString destPath = destDir + "/" + fileName;
    if (isCut) {
        QFile::rename(clipboardPath, destPath);
        clipboardPath.clear();
        isCut = false;
    } else
        QFile::copy(clipboardPath, destPath);
}

void FileBrowser::newFile()
{
    QModelIndex index = tree->currentIndex();
    QString dir = index.isValid() && model->isDir(index)
                      ? model->filePath(index)
                      : (index.isValid() ? QFileInfo(model->filePath(index)).absolutePath()
                                         : model->rootPath());
    bool ok;
    QString name = QInputDialog::getText(this,
                                         "New File",
                                         "File name:",
                                         QLineEdit::Normal,
                                         "new_file.py",
                                         &ok);
    if (!ok || name.isEmpty())
        return;
    QFile file(dir + "/" + name);
    file.open(QIODevice::WriteOnly);
    file.close();
}

void FileBrowser::newFolder()
{
    QModelIndex index = tree->currentIndex();
    QString dir = index.isValid() && model->isDir(index)
                      ? model->filePath(index)
                      : (index.isValid() ? QFileInfo(model->filePath(index)).absolutePath()
                                         : model->rootPath());
    bool ok;
    QString name = QInputDialog::getText(this,
                                         "New Folder",
                                         "Folder name:",
                                         QLineEdit::Normal,
                                         "new_folder",
                                         &ok);
    if (!ok || name.isEmpty())
        return;
    QDir(dir).mkdir(name);
}

void FileBrowser::renameSelected()
{
    QModelIndex index = tree->currentIndex();
    if (!index.isValid())
        return;
    QString oldPath = model->filePath(index);
    QString oldName = QFileInfo(oldPath).fileName();
    QString dir = QFileInfo(oldPath).absolutePath();
    bool ok;
    QString newName
        = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName)
        return;
    QFile::rename(oldPath, dir + "/" + newName);
}
