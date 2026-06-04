#ifndef FILEBROWSER_H
#define FILEBROWSER_H
#include <QFileSystemModel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QWidget>

class FileBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit FileBrowser(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    QString rootPath() const;
    QString findFile(const QString &filename);

    void setRemoteFiles(const QStringList &relPaths);

    void clearRemoteMode();

signals:
    void fileDoubleClicked(const QString &filePath);

private slots:
    void onItemDoubleClicked(const QModelIndex &index);
    void onRemoteItemDoubleClicked(QTreeWidgetItem *item, int column);
    void showContextMenu(const QPoint &pos);
    void deleteSelected();
    void copySelected();
    void cutSelected();
    void pasteToSelected();
    void newFile();
    void newFolder();
    void renameSelected();

private:
    void setupFileBrowser();
    void setupFilter();

    QStackedWidget *stack;
    QTreeView *tree;
    QFileSystemModel *model;
    QTreeWidget *remoteTree;
    QLineEdit *searchBox;

    QString clipboardPath;
    bool isCut = false;
};

#endif // FILEBROWSER_H
