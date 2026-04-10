#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    editor = new CodeEditor(this);
    setCentralWidget(editor);
    resize(1200, 800);
}

MainWindow::~MainWindow()
{
    delete ui;
}