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