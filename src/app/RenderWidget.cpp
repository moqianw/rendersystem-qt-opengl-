#include "app/RenderWidget.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QVector2D>
#include <QtMath>

#include <cstddef>

namespace {

constexpr float kPitchLimit = 89.0f;
constexpr float kLightMarkerScale = 0.25f;

renderer::RenderWidget::Vertex makeVertex(
    const QVector3D& position,
    const QVector3D& normal,
    float u,
    float v,
    const QVector3D& color = QVector3D(1.0f, 1.0f, 1.0f)) {
    return renderer::RenderWidget::Vertex{
        position.x(),
        position.y(),
        position.z(),
        normal.x(),
        normal.y(),
        normal.z(),
        u,
        v,
        color.x(),
        color.y(),
        color.z(),
    };
}

[[noreturn]] void fail(const QString& message) {
    qFatal("%s", qPrintable(message));
}

void require(bool condition, const QString& message) {
    if (!condition) {
        fail(message);
    }
}

}  // namespace

namespace renderer {

RenderWidget::RenderWidget(const SceneConfig& scene, QWidget* parent)
    : QOpenGLWidget(parent),
      scene_(scene),
      cameraPosition_(scene.camera.position),
      yaw_(scene.camera.yaw),
      pitch_(scene.camera.pitch),
      moveSpeed_(scene.camera.moveSpeed),
      lookSpeed_(scene.camera.lookSpeed),
      fov_(scene.camera.fov),
      nearClip_(scene.camera.nearClip),
      farClip_(scene.camera.farClip) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    if (scene_.window.captureMouse) {
        mouseLookActive_ = true;
        setCursor(Qt::BlankCursor);
    }

    updateCameraVectors();

    frameClock_.start();
    frameTimer_.setInterval(16);
    connect(&frameTimer_, &QTimer::timeout, this, &RenderWidget::onFrame);
    frameTimer_.start();
}

RenderWidget::~RenderWidget() {
    if (!context()) {
        return;
    }

    makeCurrent();
    cubeMesh_.vao.destroy();
    cubeMesh_.vbo.destroy();
    cubeMesh_.ebo.destroy();
    axesMesh_.vao.destroy();
    axesMesh_.vbo.destroy();
    axesMesh_.ebo.destroy();
    materials_.clear();
    sceneProgram_.removeAllShaders();
    debugProgram_.removeAllShaders();
    doneCurrent();
}

QSize RenderWidget::sizeHint() const {
    return scene_.window.size;
}

void RenderWidget::initializeGL() {
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initializeShaders();
    initializeTextures();
    initializeGeometry();
}

void RenderWidget::paintGL() {
    const QVector4D clearColor = scene_.window.clearColor;
    glClearColor(clearColor.x(), clearColor.y(), clearColor.z(), clearColor.w());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const LightConfig light = scene_.lights.isEmpty() ? LightConfig{} : scene_.lights.constFirst();
    const QMatrix4x4 view = buildViewMatrix();
    const QMatrix4x4 projection = buildProjectionMatrix();

    sceneProgram_.bind();
    sceneProgram_.setUniformValue("uView", view);
    sceneProgram_.setUniformValue("uProjection", projection);
    sceneProgram_.setUniformValue("uViewPos", cameraPosition_);
    sceneProgram_.setUniformValue("uLight.position", light.position);
    sceneProgram_.setUniformValue("uLight.color", light.color);
    sceneProgram_.setUniformValue("uLight.ambientStrength", light.ambientStrength);
    sceneProgram_.setUniformValue("uLight.intensity", light.intensity);
    sceneProgram_.setUniformValue("uMaterial.diffuseMap", 0);

    cubeMesh_.vao.bind();
    for (const RenderObjectConfig& object : scene_.objects) {
        if (!object.visible || object.geometry != GeometryType::Cube) {
            continue;
        }

        MaterialRuntime& material = requireMaterial(object.materialId);
        sceneProgram_.setUniformValue("uModel", buildModelMatrix(object));
        sceneProgram_.setUniformValue("uMaterial.tint", material.tint);

        if (material.texture) {
            material.texture->bind(0);
        }
        glDrawElements(
            cubeMesh_.primitiveType,
            cubeMesh_.indexCount,
            GL_UNSIGNED_INT,
            nullptr);
        if (material.texture) {
            material.texture->release();
        }
    }
    cubeMesh_.vao.release();
    sceneProgram_.release();

    if (!scene_.debug.drawAxes && !scene_.debug.drawLightGizmo) {
        return;
    }

    debugProgram_.bind();
    debugProgram_.setUniformValue("uView", view);
    debugProgram_.setUniformValue("uProjection", projection);

    if (scene_.debug.drawAxes) {
        QMatrix4x4 axesModel;
        debugProgram_.setUniformValue("uModel", axesModel);
        debugProgram_.setUniformValue("uColorTint", QVector3D(1.0f, 1.0f, 1.0f));

        axesMesh_.vao.bind();
        glDrawElements(
            axesMesh_.primitiveType,
            axesMesh_.indexCount,
            GL_UNSIGNED_INT,
            nullptr);
        axesMesh_.vao.release();
    }

    if (scene_.debug.drawLightGizmo) {
        QMatrix4x4 lightModel;
        lightModel.translate(light.position);
        lightModel.scale(kLightMarkerScale);
        debugProgram_.setUniformValue("uModel", lightModel);
        debugProgram_.setUniformValue("uColorTint", light.color);

        cubeMesh_.vao.bind();
        glDrawElements(
            cubeMesh_.primitiveType,
            cubeMesh_.indexCount,
            GL_UNSIGNED_INT,
            nullptr);
        cubeMesh_.vao.release();
    }

    debugProgram_.release();
}

void RenderWidget::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
}

void RenderWidget::keyPressEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        pressedKeys_.insert(event->key());
    }
    QOpenGLWidget::keyPressEvent(event);
}

void RenderWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        pressedKeys_.remove(event->key());
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void RenderWidget::mousePressEvent(QMouseEvent* event) {
    if (scene_.window.captureMouse || event->button() == Qt::RightButton) {
        mouseLookActive_ = true;
        firstMouseSample_ = true;
        if (scene_.window.captureMouse) {
            setCursor(Qt::BlankCursor);
        }
    }
    setFocus();
    QOpenGLWidget::mousePressEvent(event);
}

void RenderWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!scene_.window.captureMouse && event->button() == Qt::RightButton) {
        mouseLookActive_ = false;
        unsetCursor();
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void RenderWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!mouseLookActive_) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint position = event->position().toPoint();
    if (firstMouseSample_) {
        firstMouseSample_ = false;
        lastMousePosition_ = position;
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = position - lastMousePosition_;
    lastMousePosition_ = position;

    yaw_ += static_cast<float>(delta.x()) * lookSpeed_;
    pitch_ -= static_cast<float>(delta.y()) * lookSpeed_;
    pitch_ = qBound(-kPitchLimit, pitch_, kPitchLimit);
    updateCameraVectors();

    QOpenGLWidget::mouseMoveEvent(event);
}

void RenderWidget::focusOutEvent(QFocusEvent* event) {
    pressedKeys_.clear();
    firstMouseSample_ = true;
    if (!scene_.window.captureMouse) {
        mouseLookActive_ = false;
        unsetCursor();
    }
    QOpenGLWidget::focusOutEvent(event);
}

void RenderWidget::onFrame() {
    const float deltaSeconds =
        qMax(0.001f, static_cast<float>(frameClock_.restart()) / 1000.0f);
    updateCamera(deltaSeconds);
    update();
}

void RenderWidget::updateCamera(float deltaSeconds) {
    const float speed = moveSpeed_ * deltaSeconds;

    if (pressedKeys_.contains(Qt::Key_W)) {
        cameraPosition_ += cameraFront_ * speed;
    }
    if (pressedKeys_.contains(Qt::Key_S)) {
        cameraPosition_ -= cameraFront_ * speed;
    }
    if (pressedKeys_.contains(Qt::Key_A)) {
        cameraPosition_ -= cameraRight_ * speed;
    }
    if (pressedKeys_.contains(Qt::Key_D)) {
        cameraPosition_ += cameraRight_ * speed;
    }
    if (pressedKeys_.contains(Qt::Key_Q)) {
        cameraPosition_ -= worldUp_ * speed;
    }
    if (pressedKeys_.contains(Qt::Key_E)) {
        cameraPosition_ += worldUp_ * speed;
    }
}

void RenderWidget::updateCameraVectors() {
    const float yawRadians = qDegreesToRadians(yaw_);
    const float pitchRadians = qDegreesToRadians(pitch_);

    QVector3D front;
    front.setX(std::cos(yawRadians) * std::cos(pitchRadians));
    front.setY(std::sin(pitchRadians));
    front.setZ(std::sin(yawRadians) * std::cos(pitchRadians));

    cameraFront_ = front.normalized();
    cameraRight_ = QVector3D::crossProduct(cameraFront_, worldUp_).normalized();
    cameraUp_ = QVector3D::crossProduct(cameraRight_, cameraFront_).normalized();
}

void RenderWidget::initializeShaders() {
    require(
        sceneProgram_.addShaderFromSourceFile(
            QOpenGLShader::Vertex,
            resolvePath("assets/shaders/scene.vert")),
        sceneProgram_.log());
    require(
        sceneProgram_.addShaderFromSourceFile(
            QOpenGLShader::Fragment,
            resolvePath("assets/shaders/scene.frag")),
        sceneProgram_.log());
    require(sceneProgram_.link(), sceneProgram_.log());

    require(
        debugProgram_.addShaderFromSourceFile(
            QOpenGLShader::Vertex,
            resolvePath("assets/shaders/debug.vert")),
        debugProgram_.log());
    require(
        debugProgram_.addShaderFromSourceFile(
            QOpenGLShader::Fragment,
            resolvePath("assets/shaders/debug.frag")),
        debugProgram_.log());
    require(debugProgram_.link(), debugProgram_.log());
}

void RenderWidget::initializeTextures() {
    QImage whiteImage(1, 1, QImage::Format_RGBA8888);
    whiteImage.fill(Qt::white);

    MaterialRuntime fallback;
    fallback.texture = std::make_shared<QOpenGLTexture>(whiteImage);
    fallback.texture->setWrapMode(QOpenGLTexture::Repeat);
    fallback.texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    fallback.texture->generateMipMaps();
    materials_.insert(QStringLiteral("__default__"), fallback);

    for (const MaterialConfig& material : scene_.materials) {
        MaterialRuntime runtime;
        runtime.tint = material.tint;

        QImage image;
        if (!material.texturePath.isEmpty()) {
            image = QImage(resolvePath(material.texturePath)).mirrored();
        }
        if (image.isNull()) {
            image = whiteImage;
        } else {
            image = image.convertToFormat(QImage::Format_RGBA8888);
        }

        runtime.texture = std::make_shared<QOpenGLTexture>(image);
        runtime.texture->setWrapMode(QOpenGLTexture::Repeat);
        runtime.texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
        runtime.texture->generateMipMaps();

        const QString id = material.id.isEmpty() ? QStringLiteral("__default__") : material.id;
        materials_.insert(id, runtime);
    }
}

void RenderWidget::initializeGeometry() {
    uploadMesh(cubeMesh_, createCubeVertices(), createCubeIndices(), GL_TRIANGLES);
    uploadMesh(
        axesMesh_,
        createAxesVertices(scene_.debug.axesLength),
        createAxesIndices(),
        GL_LINES);
}

void RenderWidget::uploadMesh(
    MeshHandle& mesh,
    const std::vector<Vertex>& vertices,
    const std::vector<quint32>& indices,
    GLenum primitiveType) {
    require(mesh.vao.create(), QStringLiteral("failed to create VAO"));
    require(mesh.vbo.create(), QStringLiteral("failed to create VBO"));
    require(mesh.ebo.create(), QStringLiteral("failed to create EBO"));

    mesh.vao.bind();
    mesh.vbo.bind();
    mesh.vbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(Vertex)));
    mesh.ebo.bind();
    mesh.ebo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(quint32)));

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, px)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, cr)));
    glEnableVertexAttribArray(3);

    mesh.vao.release();
    mesh.vbo.release();
    mesh.ebo.release();

    mesh.indexCount = static_cast<int>(indices.size());
    mesh.primitiveType = primitiveType;
}

std::vector<RenderWidget::Vertex> RenderWidget::createCubeVertices() {
    return {
        makeVertex({-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, 0.0f, 0.0f),
        makeVertex({0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, 1.0f, 0.0f),
        makeVertex({0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, 1.0f, 1.0f),
        makeVertex({-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, 0.0f, 1.0f),

        makeVertex({0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, 0.0f, 0.0f),
        makeVertex({-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, 1.0f, 0.0f),
        makeVertex({-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, 1.0f, 1.0f),
        makeVertex({0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, 0.0f, 1.0f),

        makeVertex({-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, 0.0f, 0.0f),
        makeVertex({-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, 1.0f, 0.0f),
        makeVertex({-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, 1.0f, 1.0f),
        makeVertex({-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, 0.0f, 1.0f),

        makeVertex({0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.0f),
        makeVertex({0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f),
        makeVertex({0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, 1.0f, 1.0f),
        makeVertex({0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, 0.0f, 1.0f),

        makeVertex({-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f),
        makeVertex({0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, 1.0f, 0.0f),
        makeVertex({0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, 1.0f, 1.0f),
        makeVertex({-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f),

        makeVertex({-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, 0.0f, 0.0f),
        makeVertex({0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, 1.0f, 0.0f),
        makeVertex({0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, 1.0f, 1.0f),
        makeVertex({-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, 0.0f, 1.0f),
    };
}

std::vector<quint32> RenderWidget::createCubeIndices() {
    return {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };
}

std::vector<RenderWidget::Vertex> RenderWidget::createAxesVertices(float length) {
    return {
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, length, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({0.0f, 0.0f, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
    };
}

std::vector<quint32> RenderWidget::createAxesIndices() {
    return {0, 1, 2, 3, 4, 5};
}

RenderWidget::MaterialRuntime& RenderWidget::requireMaterial(const QString& materialId) {
    auto it = materials_.find(materialId);
    if (it != materials_.end()) {
        return it.value();
    }

    return materials_[QStringLiteral("__default__")];
}

QString RenderWidget::resolvePath(const QString& relativePath) const {
    const QFileInfo fileInfo(relativePath);
    if (fileInfo.isAbsolute()) {
        return fileInfo.absoluteFilePath();
    }

    return QFileInfo(QDir(QCoreApplication::applicationDirPath()), relativePath).absoluteFilePath();
}

QMatrix4x4 RenderWidget::buildViewMatrix() const {
    QMatrix4x4 view;
    view.lookAt(cameraPosition_, cameraPosition_ + cameraFront_, cameraUp_);
    return view;
}

QMatrix4x4 RenderWidget::buildProjectionMatrix() const {
    const float safeHeight = static_cast<float>(qMax(height(), 1));
    const float aspect = static_cast<float>(width()) / safeHeight;

    QMatrix4x4 projection;
    projection.perspective(fov_, aspect, nearClip_, farClip_);
    return projection;
}

QMatrix4x4 RenderWidget::buildModelMatrix(const RenderObjectConfig& object) const {
    QMatrix4x4 model;
    model.translate(object.position);
    model.rotate(object.rotationDegrees.x(), 1.0f, 0.0f, 0.0f);
    model.rotate(object.rotationDegrees.y(), 0.0f, 1.0f, 0.0f);
    model.rotate(object.rotationDegrees.z(), 0.0f, 0.0f, 1.0f);
    model.scale(object.scale);
    return model;
}

}  // namespace renderer
