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
#include <QPointF>
#include <QQuaternion>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QTimer>
#include <QVector3D>

#include <memory>
#include <vector>

#include "app/ModelLoader.hpp"
#include "app/SceneConfig.hpp"

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace renderer {

class RenderWidget final : public QOpenGLWidget, protected QOpenGLFunctions_4_5_Core {
    Q_OBJECT

public:
    enum class SceneUpdateMode {
        TransformsOnly,
        ReloadResources
    };

    enum class TransformMode {
        Translate,
        Rotate,
        Scale
    };
    Q_ENUM(TransformMode)

    enum class CoordinateSpace {
        World,
        Local
    };
    Q_ENUM(CoordinateSpace)

    enum class GizmoHandle {
        None,
        X,
        Y,
        Z,
        XY,
        XZ,
        YZ
    };

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
    void setScene(const SceneConfig& scene, SceneUpdateMode updateMode, bool resetCamera = false);
    void setSelection(const QVector<int>& indices, int activeIndex);
    void setSelectedObjectIndex(int index);
    QVector<int> selectedObjectIndices() const;
    int selectedObjectIndex() const;
    void setTransformMode(TransformMode mode);
    TransformMode transformMode() const;
    void setCoordinateSpace(CoordinateSpace space);
    CoordinateSpace coordinateSpace() const;
    void setSnapEnabled(bool enabled);
    void setMoveSnapStep(float step);
    void setRotateSnapStep(float stepDegrees);
    void setScaleSnapStep(float step);
    void resetCamera();
    void focusOnSelectedObject();

signals:
    void selectionChanged(const QVector<int>& indices, int activeIndex);
    void cameraStateChanged(const QVector3D& position, const QVector3D& target, float distance);
    void objectTransformInteractionStarted(int index, renderer::RenderWidget::TransformMode mode);
    void objectTransformPreview(
        int index,
        const QVector3D& position,
        const QVector3D& rotationDegrees,
        const QVector3D& scale);
    void objectTransformInteractionFinished(
        int index,
        const QVector3D& position,
        const QVector3D& rotationDegrees,
        const QVector3D& scale,
        renderer::RenderWidget::TransformMode mode);
    void transformModeChanged(renderer::RenderWidget::TransformMode mode);
    void coordinateSpaceChanged(renderer::RenderWidget::CoordinateSpace space);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    struct Ray {
        QVector3D origin;
        QVector3D direction;
    };

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

    struct ModelPartRuntime {
        MeshHandle mesh;
        int materialSlot = -1;
        bool valid = false;
    };

    struct ModelRuntime {
        std::vector<std::unique_ptr<ModelPartRuntime>> parts;
        QVector3D boundsMin = QVector3D(-0.5f, -0.5f, -0.5f);
        QVector3D boundsMax = QVector3D(0.5f, 0.5f, 0.5f);
        bool valid = false;
    };

    enum class MouseMode {
        None,
        Look,
        Pan,
        Orbit,
        BoxSelect,
        Translate,
        Rotate,
        Scale
    };

    void onFrame();
    void updateCamera(float deltaSeconds);
    void updateCameraVectors();
    void updateOrbitDistance();
    void moveCamera(const QVector3D& delta);
    void panCamera(const QPoint& deltaPixels);
    void orbitCamera(const QPoint& deltaPixels);
    void applySelection(const QVector<int>& indices, int activeIndex, bool emitSignal);
    QVector<int> normalizedSelection(const QVector<int>& indices, int fallbackIndex = -1) const;
    void emitCameraState();
    void pickAt(const QPointF& position, Qt::KeyboardModifiers modifiers);
    void selectInRect(const QRect& rect, Qt::KeyboardModifiers modifiers);
    int pickObject(const QPointF& position) const;
    QVector<int> pickObjectsInRect(const QRect& rect) const;
    GizmoHandle pickGizmoHandle(const QPointF& position) const;
    GizmoHandle pickLinearGizmoHandle(const QPointF& position) const;
    GizmoHandle pickRotationAxis(const QPointF& position) const;
    Ray buildPickRay(const QPointF& position) const;
    bool intersectObject(const Ray& ray, int objectIndex, float* distance) const;
    bool solveAxisDragParameter(const Ray& ray, GizmoHandle axis, float* parameter) const;
    bool solvePlaneDragPoint(const Ray& ray, GizmoHandle plane, QVector3D* point) const;
    QVector3D baseAxisDirection(GizmoHandle axis) const;
    QVector3D gizmoAxisDirection(GizmoHandle axis) const;
    QVector3D gizmoPlaneNormal(GizmoHandle plane) const;
    float gizmoScale() const;
    float linearSnapStep() const;
    QVector3D snapPositionForHandle(const QVector3D& position, GizmoHandle handle, float step) const;
    QVector3D snapScaleForAxis(const QVector3D& scale, GizmoHandle axis, float step) const;
    float snapScalar(float value, float step) const;
    float rotationDragDegrees(const QPoint& mousePosition) const;
    void emitTransformPreview();
    void initializeShaders();
    void initializeTextures();
    void initializeGeometry();
    void cleanupOpenGL();
    void destroySceneResources();
    void destroyMesh(MeshHandle& mesh);
    void resetCameraFromScene();
    void syncMouseCapture();
    void initializeModelMesh(const QString& sourcePath);
    void uploadMesh(
        MeshHandle& mesh,
        const std::vector<Vertex>& vertices,
        const std::vector<quint32>& indices,
        GLenum primitiveType);
    static std::vector<Vertex> createCubeVertices();
    static std::vector<quint32> createCubeIndices();
    static std::vector<Vertex> createAxesVertices(float length);
    static std::vector<Vertex> createTranslatePlaneVertices(float inner, float outer);
    static std::vector<Vertex> createScaleGizmoVertices(float length, float handleSize);
    static std::vector<Vertex> createRotationRingVertices(float radius, int segments);
    static std::vector<Vertex> createSelectionBracketVertices(float lengthRatio);
    static std::vector<Vertex> createGridVertices(float halfExtent, float step);
    static std::vector<quint32> createSequentialIndices(int vertexCount);
    MaterialRuntime& requireMaterial(const QString& materialId);
    QString resolvePath(const QString& relativePath) const;
    QMatrix4x4 buildViewMatrix() const;
    QMatrix4x4 buildProjectionMatrix() const;
    QMatrix4x4 buildObjectModelMatrix(int index) const;
    QMatrix4x4 buildParentWorldMatrix(int index) const;
    QMatrix4x4 buildGizmoModelMatrix() const;
    QMatrix4x4 buildSelectionModelMatrix(int index, float inflate) const;
    QQuaternion buildWorldRotation(int index) const;
    QVector3D objectLocalBoundsMin(int index) const;
    QVector3D objectLocalBoundsMax(int index) const;
    QVector3D objectCenter(int index) const;
    float objectRadius(int index) const;
    QVector3D selectionCenter() const;
    float selectionRadius() const;
    QRect normalizedSelectionRect() const;
    bool projectObjectScreenRect(int index, QRectF* screenRect) const;
    bool projectToScreen(const QVector3D& worldPoint, QPointF* screenPoint) const;

    SceneConfig scene_;
    QTimer frameTimer_;
    QElapsedTimer frameClock_;
    QSet<int> pressedKeys_;
    MouseMode mouseMode_ = MouseMode::None;
    TransformMode transformMode_ = TransformMode::Translate;
    CoordinateSpace coordinateSpace_ = CoordinateSpace::World;
    GizmoHandle activeGizmoHandle_ = GizmoHandle::None;
    QPoint lastMousePosition_;
    QPoint leftPressPosition_;
    QPoint gizmoDragStartMousePosition_;
    QPoint boxSelectionStart_;
    QPoint boxSelectionCurrent_;
    bool pendingPick_ = false;
    bool transformInteractionActive_ = false;
    QVector3D gizmoDragStartPosition_ = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D gizmoDragStartRotationDegrees_ = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D gizmoDragStartScale_ = QVector3D(1.0f, 1.0f, 1.0f);
    QVector3D gizmoDragStartWorldPoint_ = QVector3D(0.0f, 0.0f, 0.0f);
    float gizmoDragStartParameter_ = 0.0f;
    QVector3D cameraPosition_;
    QVector3D cameraTarget_ = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D cameraFront_ = QVector3D(0.0f, 0.0f, -1.0f);
    QVector3D cameraUp_ = QVector3D(0.0f, 1.0f, 0.0f);
    QVector3D cameraRight_ = QVector3D(1.0f, 0.0f, 0.0f);
    QVector3D worldUp_ = QVector3D(0.0f, 1.0f, 0.0f);
    float orbitDistance_ = 10.0f;
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
    QHash<QString, std::shared_ptr<ModelRuntime>> modelMeshes_;
    MeshHandle cubeMesh_;
    MeshHandle axesMesh_;
    MeshHandle gridMesh_;
    MeshHandle translateAxisGizmoMesh_;
    MeshHandle translatePlaneGizmoMesh_;
    MeshHandle scaleGizmoMesh_;
    MeshHandle rotationGizmoMesh_;
    MeshHandle selectionMesh_;
    QVector<int> selectedObjectIndices_;
    int selectedObjectIndex_ = -1;
    bool snapEnabled_ = false;
    float moveSnapStep_ = 1.0f;
    float rotateSnapStepDegrees_ = 15.0f;
    float scaleSnapStep_ = 0.1f;
};

}  // namespace renderer
