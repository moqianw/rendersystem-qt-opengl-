#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_4_5_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QVector3D>

#include <memory>
#include <vector>

#include "app/SceneConfig.hpp"

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;

namespace renderer {

class RenderWidget final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
public:
    struct Vertex {
        float px;
        float py;
        float pz;
        float nx;
        float ny;
        float nz;
        float u;
        float v;
        float cr;
        float cg;
        float cb;
    };

    explicit RenderWidget(const SceneConfig& scene, QWidget* parent = nullptr);
    ~RenderWidget() override;

    QSize sizeHint() const override;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    struct MeshHandle {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ebo{QOpenGLBuffer::IndexBuffer};
        int indexCount = 0;
        GLenum primitiveType = GL_TRIANGLES;
    };

    struct MaterialRuntime {
        QVector3D tint = QVector3D(1.0f, 1.0f, 1.0f);
        std::shared_ptr<QOpenGLTexture> texture;
    };

    void onFrame();
    void updateCamera(float deltaSeconds);
    void updateCameraVectors();
    void initializeShaders();
    void initializeTextures();
    void initializeGeometry();
    void uploadMesh(
        MeshHandle& mesh,
        const std::vector<Vertex>& vertices,
        const std::vector<quint32>& indices,
        GLenum primitiveType);
    static std::vector<Vertex> createCubeVertices();
    static std::vector<quint32> createCubeIndices();
    static std::vector<Vertex> createAxesVertices(float length);
    static std::vector<quint32> createAxesIndices();
    MaterialRuntime& requireMaterial(const QString& materialId);
    QString resolvePath(const QString& relativePath) const;
    QMatrix4x4 buildViewMatrix() const;
    QMatrix4x4 buildProjectionMatrix() const;
    QMatrix4x4 buildModelMatrix(const RenderObjectConfig& object) const;

    SceneConfig scene_;
    QTimer frameTimer_;
    QElapsedTimer frameClock_;
    QSet<int> pressedKeys_;
    bool mouseLookActive_ = false;
    bool firstMouseSample_ = true;
    QPoint lastMousePosition_;
    QVector3D cameraPosition_;
    QVector3D cameraFront_ = QVector3D(0.0f, 0.0f, -1.0f);
    QVector3D cameraUp_ = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D cameraRight_ = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D worldUp_ = QVector3D(0.0f, 1.0f, 0.0f);
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float moveSpeed_ = 0.0f;
    float lookSpeed_ = 0.0f;
    float fov_ = 45.0f;
    float nearClip_ = 0.1f;
    float farClip_ = 200.0f;

    QOpenGLShaderProgram sceneProgram_;
    QOpenGLShaderProgram debugProgram_;
    QHash<QString, MaterialRuntime> materials_;
    MeshHandle cubeMesh_;
    MeshHandle axesMesh_;
};

}  // namespace renderer
