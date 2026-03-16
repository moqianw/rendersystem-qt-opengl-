#include "app/MainWindow.hpp"

#include "app/RenderWidget.hpp"

namespace renderer {

MainWindow::MainWindow(const SceneConfig& scene, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(scene.window.title);
    resize(scene.window.size);
    setCentralWidget(new RenderWidget(scene, this));
}

}  // namespace renderer
