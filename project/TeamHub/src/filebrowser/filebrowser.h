#ifndef FILEBROWSER_H
#define FILEBROWSER_H
#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>

class FileBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit FileBrowser(QWidget* parent = nullptr);
    void setRootPath(const QString& path);
    QString rootPath() const;
    QString findFile(const QString& filename);

signals:
    void fileDoubleClicked(const QString& filePath);

private slots:
    void onItemDoubleClicked(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);
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

    QTreeView*         tree;
    QFileSystemModel*  model;
    QLineEdit*         searchBox;

    QString clipboardPath;
    bool    isCut = false;
};

#endif // FILEBROWSER_H
