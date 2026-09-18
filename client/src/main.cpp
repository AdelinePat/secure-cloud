#include <QApplication>
#include <QMainWindow>
#include <sodium.h>

int main(int argc, char *argv[])
{
     if (sodium_init() < 0)
    {
        return 1;
    }
    
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("SecureChat");
    window.resize(800, 600);
    window.show();

    return app.exec();
}