#ifndef FILEBROWSER_H
#define FILEBROWSER_H
#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLineEdit>

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

private:
    void setupFileBrowser();
    void setupFilter();

    QTreeView*         tree;
    QFileSystemModel*  model;
    QLineEdit*         searchBox;
};

#endif // FILEBROWSER_H
