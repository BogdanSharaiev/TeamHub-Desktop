#include "filebrowser.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QHeaderView>
#include <QImage>
#include <QPixmap>
#include <QStyle>
#include <QVBoxLayout>
#include <functional>

static QIcon loadIconTransparent(const QString &path)
{
    QImage img(path);
    if (img.isNull())
        return QIcon();
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c(img.pixel(x, y));
            if (c.red() > 230 && c.green() > 230 && c.blue() > 230)
                img.setPixel(x, y, qRgba(0, 0, 0, 0));
        }
    }
    return QIcon(QPixmap::fromImage(img));
}

class TeamHubIconProvider : public QFileIconProvider
{
    QIcon pyIcon;

public:
    explicit TeamHubIconProvider(const QIcon &icon)
        : pyIcon(icon)
    {}
    QIcon icon(const QFileInfo &info) const override
    {
        if (!pyIcon.isNull() && info.suffix().toLower() == "py")
            return pyIcon;
        return QFileIconProvider::icon(info);
    }
};

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

    placeholder = new QWidget(this);
    auto *phLayout = new QVBoxLayout(placeholder);
    phLayout->setAlignment(Qt::AlignCenter);
    phLayout->setSpacing(10);

    const QString btnStyle = "QPushButton {"
                             "  background: #0e639c;"
                             "  color: #ffffff;"
                             "  border: none;"
                             "  border-radius: 6px;"
                             "  padding: 8px 18px;"
                             "  font-size: 12px;"
                             "}"
                             "QPushButton:hover { background: #1177bb; }"
                             "QPushButton:pressed { background: #0a4f7e; }";

    auto *btnOpen = new QPushButton("Open Folder", placeholder);
    btnOpen->setFixedWidth(160);
    btnOpen->setCursor(Qt::PointingHandCursor);
    btnOpen->setStyleSheet(btnStyle);
    connect(btnOpen, &QPushButton::clicked, this, &FileBrowser::openFolderRequested);

    auto *btnClone = new QPushButton("Clone Repository", placeholder);
    btnClone->setFixedWidth(160);
    btnClone->setCursor(Qt::PointingHandCursor);
    btnClone->setStyleSheet(btnStyle);
    connect(btnClone, &QPushButton::clicked, this, &FileBrowser::cloneRepoRequested);

    phLayout->addWidget(btnOpen);
    phLayout->addWidget(btnClone);
    layout->addWidget(placeholder);

    stack = new QStackedWidget(this);
    stack->setVisible(false);
    layout->addWidget(stack);

    QString iconPath = QCoreApplication::applicationDirPath() + "/icons/python.png";
    pythonIcon = loadIconTransparent(iconPath);
    if (pythonIcon.isNull())
        pythonIcon = loadIconTransparent(QString(TEAMHUB_ICONS_DIR) + "python.png");

    model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());
    if (!pythonIcon.isNull())
        model->setIconProvider(new TeamHubIconProvider(pythonIcon));

    tree = new QTreeView(this);
    tree->setModel(model);
    tree->setRootIndex(QModelIndex());
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
    m_projectLoaded = true;
    model->setRootPath(path);
    tree->setRootIndex(model->index(path));
    placeholder->setVisible(false);
    stack->setVisible(true);
    stack->setCurrentIndex(0);
}

QString FileBrowser::rootPath() const
{
    return m_projectLoaded ? model->rootPath() : QString();
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
    const QIcon defaultFileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

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
        const bool isPython = fileName.endsWith(".py", Qt::CaseInsensitive);
        fileItem->setIcon(0, isPython && !pythonIcon.isNull() ? pythonIcon : defaultFileIcon);
        fileItem->setData(0, Qt::UserRole, relPath);
    }

    remoteTree->expandAll();
    stack->setCurrentIndex(1);
    searchBox->setEnabled(false);
}

void FileBrowser::revealFile(const QString &absolutePath)
{
    QModelIndex index = model->index(absolutePath);
    if (index.isValid()) {
        tree->setCurrentIndex(index);
        tree->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }
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