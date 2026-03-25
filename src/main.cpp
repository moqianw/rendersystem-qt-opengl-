#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QSurfaceFormat>

#include <exception>

#include "app/MainWindow.hpp"
#include "app/SceneConfig.hpp"

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);

    try {
        const QString scenePath =
            QDir(QCoreApplication::applicationDirPath()).filePath("assets/config/scene.json");
        const renderer::SceneConfig scene = renderer::SceneConfig::loadFromFile(scenePath);

        renderer::MainWindow window(scene, scenePath);
        window.show();
        return application.exec();
    } catch (const std::exception& exception) {
        QMessageBox::critical(nullptr, "Startup Error", QString::fromUtf8(exception.what()));
        return -1;
    }
}
