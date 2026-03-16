#pragma once

#include <QMainWindow>

#include "app/SceneConfig.hpp"

namespace renderer {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const SceneConfig& scene, QWidget* parent = nullptr);
};

}  // namespace renderer
