#include "app/RenderWidget.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QPolygonF>
#include <QQuaternion>
#include <QWheelEvent>
#include <QVector2D>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "app/RenderMeshUtils.hpp"
#include "app/RenderSceneCompiler.hpp"
#include "app/RenderViewportHelpers.hpp"
#include "app/SceneGraph.hpp"

namespace {

constexpr float kPitchLimit = 89.0f;
constexpr float kPanSensitivity = 0.0025f;
constexpr float kZoomSpeed = 0.12f;
constexpr float kMinOrbitDistance = 1.0f;
constexpr float kGizmoViewScale = 0.16f;
constexpr float kLinearGizmoPickThresholdPixels = 12.0f;
constexpr float kRotationGizmoPickThresholdPixels = 16.0f;
constexpr float kRotateSensitivityDegreesPerPixel = 0.45f;
constexpr float kScaleSensitivity = 0.35f;
constexpr float kMinScaleComponent = 0.05f;
constexpr float kTau = 6.28318530718f;
constexpr int kPickDragThreshold = 6;
constexpr int kRotationRingSegments = 48;
constexpr int kScaleAxisIndexCount = 6;
constexpr int kRotationAxisIndexCount = kRotationRingSegments * 2;
constexpr int kTranslateAxisIndexCount = 2;
constexpr int kTranslatePlaneIndexCount = 8;
constexpr float kTranslatePlaneInner = 0.24f;
constexpr float kTranslatePlaneOuter = 0.46f;
constexpr float kPlaneGizmoPickPadding = 8.0f;
constexpr int kMinimumGridRingCount = 64;

renderer::RenderVertex makeVertex(
    const QVector3D& position,
    const QVector3D& normal,
    float u,
    float v,
    const QVector3D& color = QVector3D(1.0f, 1.0f, 1.0f)) {
    return renderer::RenderVertex{
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

QVector3D normalizedLightDirection(const renderer::LightConfig& light) {
    QVector3D direction = light.direction;
    if (direction.lengthSquared() <= 1e-6f) {
        direction = QVector3D(-0.45f, -1.0f, -0.3f);
    }
    return direction.normalized();
}

QQuaternion lightOrientationFromDirection(const QVector3D& direction) {
    return QQuaternion::rotationTo(QVector3D(0.0f, 0.0f, -1.0f), direction.normalized()).normalized();
}

float sanitizedLightRange(const renderer::LightConfig& light) {
    return qMax(0.1f, light.range);
}

float sanitizedInnerConeDegrees(const renderer::LightConfig& light) {
    return qBound(0.1f, light.innerConeDegrees, 89.0f);
}

float sanitizedOuterConeDegrees(const renderer::LightConfig& light) {
    return qBound(sanitizedInnerConeDegrees(light), light.outerConeDegrees, 89.5f);
}

float vectorComponent(const QVector3D& value, int axis) {
    switch (axis) {
    case 0:
        return value.x();
    case 1:
        return value.y();
    default:
        return value.z();
    }
}

void setVectorComponent(QVector3D* value, int axis, float component) {
    switch (axis) {
    case 0:
        value->setX(component);
        return;
    case 1:
        value->setY(component);
        return;
    default:
        value->setZ(component);
        return;
    }
}

int axisToIndex(renderer::RenderWidget::GizmoHandle axis) {
    switch (axis) {
    case renderer::RenderWidget::GizmoHandle::X:
        return 0;
    case renderer::RenderWidget::GizmoHandle::Y:
        return 1;
    case renderer::RenderWidget::GizmoHandle::Z:
        return 2;
    case renderer::RenderWidget::GizmoHandle::None:
    case renderer::RenderWidget::GizmoHandle::XY:
    case renderer::RenderWidget::GizmoHandle::XZ:
    case renderer::RenderWidget::GizmoHandle::YZ:
        break;
    }

    return -1;
}

bool isPlaneHandle(renderer::RenderWidget::GizmoHandle handle) {
    return handle == renderer::RenderWidget::GizmoHandle::XY ||
        handle == renderer::RenderWidget::GizmoHandle::XZ ||
        handle == renderer::RenderWidget::GizmoHandle::YZ;
}

int planeHandleToIndex(renderer::RenderWidget::GizmoHandle handle) {
    switch (handle) {
    case renderer::RenderWidget::GizmoHandle::XY:
        return 0;
    case renderer::RenderWidget::GizmoHandle::XZ:
        return 1;
    case renderer::RenderWidget::GizmoHandle::YZ:
        return 2;
    default:
        return -1;
    }
}

bool isCameraMoveKey(int key) {
    switch (key) {
    case Qt::Key_W:
    case Qt::Key_A:
    case Qt::Key_S:
    case Qt::Key_D:
    case Qt::Key_Q:
    case Qt::Key_E:
    case Qt::Key_Shift:
        return true;
    default:
        return false;
    }
}

bool intersectUnitCube(
    const QVector3D& origin,
    const QVector3D& direction,
    float* hitDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float originValue = vectorComponent(origin, axis);
        const float directionValue = vectorComponent(direction, axis);

        if (qAbs(directionValue) < 1e-6f) {
            if (originValue < -0.5f || originValue > 0.5f) {
                return false;
            }
            continue;
        }

        float axisTMin = (-0.5f - originValue) / directionValue;
        float axisTMax = (0.5f - originValue) / directionValue;
        if (axisTMin > axisTMax) {
            std::swap(axisTMin, axisTMax);
        }

        tMin = qMax(tMin, axisTMin);
        tMax = qMin(tMax, axisTMax);
        if (tMin > tMax) {
            return false;
        }
    }

    *hitDistance = tMin >= 0.0f ? tMin : tMax;
    return *hitDistance >= 0.0f;
}

float distanceToSegment(const QPointF& point, const QPointF& start, const QPointF& end) {
    const QVector2D segment(end - start);
    const float segmentLengthSquared = segment.lengthSquared();
    if (segmentLengthSquared <= 1e-6f) {
        return QVector2D(point - start).length();
    }

    const QVector2D startToPoint(point - start);
    const float projection =
        qBound(0.0f, QVector2D::dotProduct(startToPoint, segment) / segmentLengthSquared, 1.0f);
    const QPointF closest = start + ((end - start) * projection);
    return QVector2D(point - closest).length();
}

int gizmoAxisStartIndex(
    renderer::RenderWidget::TransformMode mode,
    renderer::RenderWidget::GizmoHandle axis) {
    const int axisIndex = axisToIndex(axis);
    if (axisIndex < 0) {
        return 0;
    }

    switch (mode) {
    case renderer::RenderWidget::TransformMode::Translate:
        return axisIndex * kTranslateAxisIndexCount;
    case renderer::RenderWidget::TransformMode::Rotate:
        return axisIndex * kRotationAxisIndexCount;
    case renderer::RenderWidget::TransformMode::Scale:
        return axisIndex * kScaleAxisIndexCount;
    }

    return 0;
}

int gizmoAxisIndexCount(renderer::RenderWidget::TransformMode mode) {
    switch (mode) {
    case renderer::RenderWidget::TransformMode::Translate:
        return kTranslateAxisIndexCount;
    case renderer::RenderWidget::TransformMode::Rotate:
        return kRotationAxisIndexCount;
    case renderer::RenderWidget::TransformMode::Scale:
        return kScaleAxisIndexCount;
    }

    return 2;
}

int translatePlaneStartIndex(renderer::RenderWidget::GizmoHandle handle) {
    const int planeIndex = planeHandleToIndex(handle);
    return planeIndex < 0 ? 0 : planeIndex * kTranslatePlaneIndexCount;
}

}  // namespace

namespace renderer {

RenderWidget::RenderWidget(const SceneConfig& scene, QWidget* parent)
    : QOpenGLWidget(parent),
      scene_(scene) {
    scenegraph::ensureObjectIds(&scene_);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    snapEnabled_ = scene_.debug.snapEnabled;
    moveSnapStep_ = qMax(0.001f, scene_.debug.gridStep);
    rotateSnapStepDegrees_ = qMax(0.1f, scene_.debug.rotateSnapDegrees);
    scaleSnapStep_ = qMax(0.001f, scene_.debug.scaleSnapStep);
    resetCameraFromScene();
    syncMouseCapture();

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
    destroySceneResources();
    sceneRenderer_.cleanup();
    debugProgram_.removeAllShaders();
    doneCurrent();
}

QSize RenderWidget::sizeHint() const {
    return scene_.window.size;
}

void RenderWidget::setScene(const SceneConfig& scene, SceneUpdateMode updateMode, bool resetCamera) {
    scene_ = scene;
    scenegraph::ensureObjectIds(&scene_);
    snapEnabled_ = scene_.debug.snapEnabled;
    moveSnapStep_ = qMax(0.001f, scene_.debug.gridStep);
    rotateSnapStepDegrees_ = qMax(0.1f, scene_.debug.rotateSnapDegrees);
    scaleSnapStep_ = qMax(0.001f, scene_.debug.scaleSnapStep);
    selectedObjectIndices_ = normalizedSelection(selectedObjectIndices_, selectedObjectIndex_);
    if (!selectedObjectIndices_.contains(selectedObjectIndex_)) {
        selectedObjectIndex_ = selectedObjectIndices_.isEmpty() ? -1 : selectedObjectIndices_.constLast();
    }
    if (selectedLightIndex_ < 0 || selectedLightIndex_ >= scene_.lights.size()) {
        selectedLightIndex_ = -1;
    }

    if (resetCamera) {
        resetCameraFromScene();
    } else {
        updateOrbitDistance();
        emitCameraState();
    }

    syncMouseCapture();

    if (context()) {
        makeCurrent();
        if (updateMode == SceneUpdateMode::ReloadResources) {
            destroySceneResources();
            initializeGeometry();
        }
        rebuildRenderScene(updateMode == SceneUpdateMode::ReloadResources);
        doneCurrent();
    } else {
        compiledScene_ = RenderSceneCompiler::compile(scene_, resourceManager_, [this](const QString& path) {
            return resolvePath(path);
        });
    }

    update();
}

void RenderWidget::setSelection(const QVector<int>& indices, int activeIndex) {
    applySelection(indices, activeIndex, false);
}

void RenderWidget::setSelectedObjectIndex(int index) {
    setSelection(index >= 0 ? QVector<int>{index} : QVector<int>{}, index);
}

void RenderWidget::setSelectedLightIndex(int index) {
    selectedObjectIndices_.clear();
    selectedObjectIndex_ = -1;
    selectedLightIndex_ = index >= 0 && index < scene_.lights.size() ? index : -1;
    activeGizmoHandle_ = GizmoHandle::None;
    transformInteractionActive_ = false;
    if (selectedLightIndex_ >= 0) {
        cameraTarget_ = activeSelectionCenter();
        updateOrbitDistance();
        emitCameraState();
    }
    update();
}

QVector<int> RenderWidget::selectedObjectIndices() const {
    return selectedObjectIndices_;
}

int RenderWidget::selectedObjectIndex() const {
    return selectedObjectIndex_;
}

int RenderWidget::selectedLightIndex() const {
    return selectedLightIndex_;
}

void RenderWidget::setTransformMode(TransformMode mode) {
    if (transformMode_ == mode) {
        return;
    }

    transformMode_ = mode;
    syncMouseCapture();
    emit transformModeChanged(transformMode_);
    update();
}

RenderWidget::TransformMode RenderWidget::transformMode() const {
    return transformMode_;
}

void RenderWidget::setCoordinateSpace(CoordinateSpace space) {
    if (coordinateSpace_ == space) {
        return;
    }

    coordinateSpace_ = space;
    syncMouseCapture();
    emit coordinateSpaceChanged(coordinateSpace_);
    update();
}

RenderWidget::CoordinateSpace RenderWidget::coordinateSpace() const {
    return coordinateSpace_;
}

void RenderWidget::setSnapEnabled(bool enabled) {
    snapEnabled_ = enabled;
}

void RenderWidget::setMoveSnapStep(float step) {
    moveSnapStep_ = qMax(0.001f, step);
}

void RenderWidget::setRotateSnapStep(float stepDegrees) {
    rotateSnapStepDegrees_ = qMax(0.1f, stepDegrees);
}

void RenderWidget::setScaleSnapStep(float step) {
    scaleSnapStep_ = qMax(0.001f, step);
}

void RenderWidget::resetCamera() {
    resetCameraFromScene();
    syncMouseCapture();
    update();
}

void RenderWidget::focusOnSelectedObject() {
    if (!hasActiveSelection()) {
        return;
    }

    cameraTarget_ = activeSelectionCenter();
    const float halfFovRadians = qDegreesToRadians(fov_) * 0.5f;
    const float radius = qMax(0.75f, activeSelectionRadius());
    orbitDistance_ = qMax(kMinOrbitDistance, (radius / std::tan(halfFovRadians)) * 1.8f);
    cameraPosition_ = cameraTarget_ - cameraFront_ * orbitDistance_;
    emitCameraState();
    update();
}

void RenderWidget::initializeGL() {
    initializeOpenGLFunctions();
    connect(
        context(),
        &QOpenGLContext::aboutToBeDestroyed,
        this,
        &RenderWidget::cleanupOpenGL,
        Qt::UniqueConnection);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initializeShaders();
    destroySceneResources();
    initializeGeometry();
    rebuildRenderScene(true);
}

void RenderWidget::paintGL() {
    const QVector4D clearColor = scene_.window.clearColor;
    glClearColor(clearColor.x(), clearColor.y(), clearColor.z(), clearColor.w());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const QMatrix4x4 view = buildViewMatrix();
    const QMatrix4x4 projection = buildProjectionMatrix();
    sceneRenderer_.render(compiledScene_, &cubeMesh_, view, projection, cameraPosition_);
    renderDebugPass(view, projection);

    if (mouseMode_ == MouseMode::BoxSelect) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRect rect = normalizedSelectionRect();
        painter.fillRect(rect, QColor(70, 140, 220, 45));
        painter.setPen(QPen(QColor(70, 140, 220), 1.5));
        painter.drawRect(rect);
    }
}

void RenderWidget::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
}

void RenderWidget::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_F) {
        focusOnSelectedObject();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Home) {
        resetCamera();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        syncMouseCapture();
        update();
        event->accept();
        return;
    }

    if (mouseMode_ == MouseMode::Look && isCameraMoveKey(event->key())) {
        pressedKeys_.insert(event->key());
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_W) {
        setTransformMode(TransformMode::Translate);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_E) {
        setTransformMode(TransformMode::Rotate);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_R) {
        setTransformMode(TransformMode::Scale);
        event->accept();
        return;
    }

    QOpenGLWidget::keyPressEvent(event);
}

void RenderWidget::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat() && isCameraMoveKey(event->key())) {
        pressedKeys_.remove(event->key());
    }
    QOpenGLWidget::keyReleaseEvent(event);
}

void RenderWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    lastMousePosition_ = event->position().toPoint();

    if (event->button() == Qt::RightButton) {
        mouseMode_ = MouseMode::Look;
        pendingPick_ = false;
        transformInteractionActive_ = false;
        activeGizmoHandle_ = GizmoHandle::None;
        setCursor(scene_.window.captureMouse ? Qt::BlankCursor : Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        mouseMode_ = MouseMode::Pan;
        pendingPick_ = false;
        transformInteractionActive_ = false;
        activeGizmoHandle_ = GizmoHandle::None;
        setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier)) {
        mouseMode_ = MouseMode::Orbit;
        pendingPick_ = false;
        transformInteractionActive_ = false;
        activeGizmoHandle_ = GizmoHandle::None;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const GizmoHandle pickedHandle = pickGizmoHandle(event->position());
        if (pickedHandle != GizmoHandle::None && canRenderActiveGizmo()) {
            bool readyToTransform = false;

            gizmoDragStartMousePosition_ = event->position().toPoint();
            activeGizmoHandle_ = pickedHandle;

            if (activeTransformTarget() == TransformTarget::Object) {
                const RenderObjectConfig& object = scene_.objects.at(selectedObjectIndex_);
                gizmoDragStartPosition_ = object.position;
                gizmoDragStartRotationDegrees_ = object.rotationDegrees;
                gizmoDragStartScale_ = object.scale;
            } else if (activeTransformTarget() == TransformTarget::Light) {
                const LightConfig& light = scene_.lights.at(selectedLightIndex_);
                gizmoDragStartPosition_ = light.position;
                gizmoDragStartLightDirection_ = normalizedLightDirection(light);
            }

            if (transformMode_ == TransformMode::Translate && isPlaneHandle(pickedHandle)) {
                const Ray ray = buildPickRay(event->position());
                readyToTransform = solvePlaneDragPoint(ray, pickedHandle, &gizmoDragStartWorldPoint_);
            } else if (transformMode_ == TransformMode::Translate || transformMode_ == TransformMode::Scale) {
                const Ray ray = buildPickRay(event->position());
                readyToTransform = solveAxisDragParameter(ray, pickedHandle, &gizmoDragStartParameter_);
            } else if (transformMode_ == TransformMode::Rotate) {
                readyToTransform = true;
            }

            if (readyToTransform) {
                transformInteractionActive_ = true;
                pendingPick_ = false;
                mouseMode_ =
                    transformMode_ == TransformMode::Translate ? MouseMode::Translate
                    : transformMode_ == TransformMode::Rotate   ? MouseMode::Rotate
                                                                : MouseMode::Scale;
                setCursor(Qt::SizeAllCursor);
                if (activeTransformTarget() == TransformTarget::Object) {
                    emit objectTransformInteractionStarted(selectedObjectIndex_, transformMode_);
                } else if (activeTransformTarget() == TransformTarget::Light) {
                    emit lightTransformInteractionStarted(selectedLightIndex_, transformMode_);
                }
                event->accept();
                return;
            }
        }

        leftPressPosition_ = lastMousePosition_;
        boxSelectionStart_ = leftPressPosition_;
        boxSelectionCurrent_ = leftPressPosition_;
        pendingPick_ = true;
        transformInteractionActive_ = false;
        activeGizmoHandle_ = GizmoHandle::None;
        event->accept();
        return;
    }

    QOpenGLWidget::mousePressEvent(event);
}

void RenderWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && mouseMode_ == MouseMode::Look) {
        syncMouseCapture();
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton && mouseMode_ == MouseMode::Pan) {
        syncMouseCapture();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && mouseMode_ == MouseMode::Orbit) {
        syncMouseCapture();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton &&
        (mouseMode_ == MouseMode::Translate || mouseMode_ == MouseMode::Rotate || mouseMode_ == MouseMode::Scale)) {
        if (transformInteractionActive_ &&
            selectedObjectIndex_ >= 0 &&
            selectedObjectIndex_ < scene_.objects.size()) {
            const RenderObjectConfig& object = scene_.objects.at(selectedObjectIndex_);
            emit objectTransformInteractionFinished(
                selectedObjectIndex_,
                object.position,
                object.rotationDegrees,
                object.scale,
                transformMode_);
        } else if (transformInteractionActive_ &&
            selectedLightIndex_ >= 0 &&
            selectedLightIndex_ < scene_.lights.size()) {
            const LightConfig& light = scene_.lights.at(selectedLightIndex_);
            emit lightTransformInteractionFinished(
                selectedLightIndex_,
                light.position,
                normalizedLightDirection(light),
                transformMode_);
        }
        syncMouseCapture();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && mouseMode_ == MouseMode::BoxSelect) {
        selectInRect(normalizedSelectionRect(), event->modifiers());
        syncMouseCapture();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && pendingPick_) {
        const QPoint releasePosition = event->position().toPoint();
        if ((releasePosition - leftPressPosition_).manhattanLength() <= kPickDragThreshold) {
            pickAt(event->position(), event->modifiers());
        }
        pendingPick_ = false;
        event->accept();
        return;
    }

    QOpenGLWidget::mouseReleaseEvent(event);
}

void RenderWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - lastMousePosition_;
    lastMousePosition_ = position;

    if (pendingPick_ && (position - leftPressPosition_).manhattanLength() > kPickDragThreshold) {
        pendingPick_ = false;
        mouseMode_ = MouseMode::BoxSelect;
        boxSelectionCurrent_ = position;
        update();
    }

    if (delta.isNull()) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    if (mouseMode_ == MouseMode::Look) {
        yaw_ += static_cast<float>(delta.x()) * lookSpeed_;
        pitch_ -= static_cast<float>(delta.y()) * lookSpeed_;
        pitch_ = qBound(-kPitchLimit, pitch_, kPitchLimit);
        updateCameraVectors();
        cameraTarget_ = cameraPosition_ + cameraFront_ * orbitDistance_;
        emitCameraState();
        update();
    } else if (mouseMode_ == MouseMode::Pan) {
        panCamera(delta);
        update();
    } else if (mouseMode_ == MouseMode::Orbit) {
        orbitCamera(delta);
        update();
    } else if (mouseMode_ == MouseMode::BoxSelect) {
        boxSelectionCurrent_ = position;
        update();
    } else if (activeTransformTarget() == TransformTarget::Object &&
        selectedObjectIndex_ >= 0 &&
        selectedObjectIndex_ < scene_.objects.size()) {
        RenderObjectConfig& object = scene_.objects[selectedObjectIndex_];
        const bool useSnap = snapEnabled_ || event->modifiers().testFlag(Qt::ShiftModifier);

        if (mouseMode_ == MouseMode::Translate) {
            const Ray ray = buildPickRay(event->position());
            QVector3D nextPosition = gizmoDragStartPosition_;
            bool updated = false;
            if (isPlaneHandle(activeGizmoHandle_)) {
                QVector3D worldPoint;
                if (solvePlaneDragPoint(ray, activeGizmoHandle_, &worldPoint)) {
                    const QMatrix4x4 inverseParent = buildParentWorldMatrix(selectedObjectIndex_).inverted();
                    const QVector3D localDelta = (inverseParent * QVector4D(worldPoint - gizmoDragStartWorldPoint_, 0.0f)).toVector3D();
                    nextPosition = gizmoDragStartPosition_ + localDelta;
                    updated = true;
                }
            } else {
                float axisParameter = 0.0f;
                if (solveAxisDragParameter(ray, activeGizmoHandle_, &axisParameter)) {
                    const QVector3D axisDirection = gizmoAxisDirection(activeGizmoHandle_);
                    const QVector3D worldDelta = axisDirection * (axisParameter - gizmoDragStartParameter_);
                    const QMatrix4x4 inverseParent = buildParentWorldMatrix(selectedObjectIndex_).inverted();
                    const QVector3D localDelta = (inverseParent * QVector4D(worldDelta, 0.0f)).toVector3D();
                    nextPosition = gizmoDragStartPosition_ + localDelta;
                    updated = true;
                }
            }

            if (updated) {
                if (useSnap) {
                    nextPosition = snapPositionForHandle(nextPosition, activeGizmoHandle_, linearSnapStep());
                }

                object.position = nextPosition;
                cameraTarget_ = selectionCenter();
                updateOrbitDistance();
                emitCameraState();
                emitTransformPreview();
                update();
            }
        } else if (mouseMode_ == MouseMode::Scale) {
            const Ray ray = buildPickRay(event->position());
            float axisParameter = 0.0f;
            if (solveAxisDragParameter(ray, activeGizmoHandle_, &axisParameter)) {
                QVector3D nextScale = gizmoDragStartScale_;
                const int axisIndex = axisToIndex(activeGizmoHandle_);
                if (axisIndex >= 0) {
                    const float startValue = vectorComponent(gizmoDragStartScale_, axisIndex);
                    const float nextValue = qMax(
                        kMinScaleComponent,
                        startValue + ((axisParameter - gizmoDragStartParameter_) * kScaleSensitivity));
                    setVectorComponent(&nextScale, axisIndex, nextValue);
                }
                if (useSnap) {
                    nextScale = snapScaleForAxis(nextScale, activeGizmoHandle_, scaleSnapStep_);
                }

                object.scale = nextScale;
                emitTransformPreview();
                update();
            }
        } else if (mouseMode_ == MouseMode::Rotate) {
            float nextAngle = rotationDragDegrees(position);
            if (useSnap) {
                nextAngle = snapScalar(nextAngle, rotateSnapStepDegrees_);
            }

            const QQuaternion startRotation =
                QQuaternion::fromEulerAngles(gizmoDragStartRotationDegrees_);
            const QQuaternion deltaRotation =
                QQuaternion::fromAxisAndAngle(baseAxisDirection(activeGizmoHandle_), nextAngle);
            const QQuaternion composed =
                coordinateSpace_ == CoordinateSpace::Local
                    ? (startRotation * deltaRotation)
                    : (deltaRotation * startRotation);

            object.rotationDegrees = composed.normalized().toEulerAngles();
            emitTransformPreview();
            update();
        }
    } else if (activeTransformTarget() == TransformTarget::Light &&
        selectedLightIndex_ >= 0 &&
        selectedLightIndex_ < scene_.lights.size()) {
        LightConfig& light = scene_.lights[selectedLightIndex_];
        const bool useSnap = snapEnabled_ || event->modifiers().testFlag(Qt::ShiftModifier);

        if (mouseMode_ == MouseMode::Translate) {
            const Ray ray = buildPickRay(event->position());
            QVector3D nextPosition = gizmoDragStartPosition_;
            bool updated = false;
            if (isPlaneHandle(activeGizmoHandle_)) {
                QVector3D worldPoint;
                if (solvePlaneDragPoint(ray, activeGizmoHandle_, &worldPoint)) {
                    nextPosition = gizmoDragStartPosition_ + (worldPoint - gizmoDragStartWorldPoint_);
                    updated = true;
                }
            } else {
                float axisParameter = 0.0f;
                if (solveAxisDragParameter(ray, activeGizmoHandle_, &axisParameter)) {
                    const QVector3D worldDelta = gizmoAxisDirection(activeGizmoHandle_) * (axisParameter - gizmoDragStartParameter_);
                    nextPosition = gizmoDragStartPosition_ + worldDelta;
                    updated = true;
                }
            }

            if (updated) {
                if (useSnap) {
                    nextPosition = snapPositionForHandle(nextPosition, activeGizmoHandle_, linearSnapStep());
                }

                light.position = nextPosition;
                cameraTarget_ = activeSelectionCenter();
                updateOrbitDistance();
                emitCameraState();
                emitLightTransformPreview();
                update();
            }
        } else if (mouseMode_ == MouseMode::Rotate && light.type != LightType::Point) {
            float nextAngle = rotationDragDegrees(position);
            if (useSnap) {
                nextAngle = snapScalar(nextAngle, rotateSnapStepDegrees_);
            }

            const QQuaternion startRotation = lightOrientationFromDirection(gizmoDragStartLightDirection_);
            const QQuaternion deltaRotation =
                QQuaternion::fromAxisAndAngle(baseAxisDirection(activeGizmoHandle_), nextAngle);
            const QQuaternion composed =
                coordinateSpace_ == CoordinateSpace::Local
                    ? (startRotation * deltaRotation)
                    : (deltaRotation * startRotation);
            light.direction =
                composed.normalized().rotatedVector(QVector3D(0.0f, 0.0f, -1.0f)).normalized();
            emitLightTransformPreview();
            update();
        }
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void RenderWidget::wheelEvent(QWheelEvent* event) {
    const float wheelSteps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    if (qFuzzyIsNull(wheelSteps)) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    const float zoomAmount = qMax(0.5f, orbitDistance_ * kZoomSpeed) * wheelSteps;
    if (hasActiveSelection()) {
        orbitDistance_ = qMax(kMinOrbitDistance, orbitDistance_ - zoomAmount);
        cameraPosition_ = cameraTarget_ - cameraFront_ * orbitDistance_;
        emitCameraState();
    } else {
        moveCamera(cameraFront_ * zoomAmount);
    }

    update();
    event->accept();
}

void RenderWidget::focusOutEvent(QFocusEvent* event) {
    pressedKeys_.clear();
    pendingPick_ = false;
    syncMouseCapture();
    QOpenGLWidget::focusOutEvent(event);
}

void RenderWidget::onFrame() {
    const float deltaSeconds =
        qMax(0.001f, static_cast<float>(frameClock_.restart()) / 1000.0f);
    updateCamera(deltaSeconds);
    update();
}

void RenderWidget::updateCamera(float deltaSeconds) {
    if (mouseMode_ != MouseMode::Look) {
        return;
    }

    QVector3D movement;
    if (pressedKeys_.contains(Qt::Key_W)) {
        movement += cameraFront_;
    }
    if (pressedKeys_.contains(Qt::Key_S)) {
        movement -= cameraFront_;
    }
    if (pressedKeys_.contains(Qt::Key_A)) {
        movement -= cameraRight_;
    }
    if (pressedKeys_.contains(Qt::Key_D)) {
        movement += cameraRight_;
    }
    if (pressedKeys_.contains(Qt::Key_Q)) {
        movement -= worldUp_;
    }
    if (pressedKeys_.contains(Qt::Key_E)) {
        movement += worldUp_;
    }

    if (movement.lengthSquared() <= 0.0f) {
        return;
    }

    float speed = moveSpeed_ * deltaSeconds;
    if (pressedKeys_.contains(Qt::Key_Shift)) {
        speed *= 3.0f;
    }

    moveCamera(movement.normalized() * speed);
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

void RenderWidget::updateOrbitDistance() {
    orbitDistance_ = qMax(kMinOrbitDistance, (cameraTarget_ - cameraPosition_).length());
}

void RenderWidget::moveCamera(const QVector3D& delta) {
    cameraPosition_ += delta;
    cameraTarget_ += delta;
    updateOrbitDistance();
    emitCameraState();
}

void RenderWidget::panCamera(const QPoint& deltaPixels) {
    const float panScale = qMax(1.0f, orbitDistance_) * kPanSensitivity;
    const QVector3D delta =
        (-cameraRight_ * static_cast<float>(deltaPixels.x()) +
            cameraUp_ * static_cast<float>(deltaPixels.y())) *
        panScale;
    moveCamera(delta);
}

void RenderWidget::orbitCamera(const QPoint& deltaPixels) {
    yaw_ += static_cast<float>(deltaPixels.x()) * lookSpeed_;
    pitch_ -= static_cast<float>(deltaPixels.y()) * lookSpeed_;
    pitch_ = qBound(-kPitchLimit, pitch_, kPitchLimit);
    updateCameraVectors();
    cameraPosition_ = cameraTarget_ - cameraFront_ * orbitDistance_;
    emitCameraState();
}

void RenderWidget::emitCameraState() {
    emit cameraStateChanged(cameraPosition_, cameraTarget_, orbitDistance_);
}

void RenderWidget::applySelection(const QVector<int>& indices, int activeIndex, bool emitSignal) {
    selectedObjectIndices_ = normalizedSelection(indices, activeIndex);
    selectedLightIndex_ = -1;
    if (selectedObjectIndices_.isEmpty()) {
        selectedObjectIndex_ = -1;
    } else if (activeIndex >= 0 && activeIndex < scene_.objects.size() && selectedObjectIndices_.contains(activeIndex)) {
        selectedObjectIndex_ = activeIndex;
    } else {
        selectedObjectIndex_ = selectedObjectIndices_.constLast();
    }

    activeGizmoHandle_ = GizmoHandle::None;
    transformInteractionActive_ = false;
    if (selectedObjectIndex_ >= 0) {
        cameraTarget_ = activeSelectionCenter();
        updateOrbitDistance();
        emitCameraState();
    }

    update();
    if (emitSignal) {
        emit selectionChanged(selectedObjectIndices_, selectedObjectIndex_);
    }
}

QVector<int> RenderWidget::normalizedSelection(const QVector<int>& indices, int fallbackIndex) const {
    QVector<int> normalized;
    QSet<int> seen;
    for (int index : indices) {
        if (index < 0 || index >= scene_.objects.size() || seen.contains(index)) {
            continue;
        }
        seen.insert(index);
        normalized.append(index);
    }
    if (normalized.isEmpty() && fallbackIndex >= 0 && fallbackIndex < scene_.objects.size()) {
        normalized.append(fallbackIndex);
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}

RenderWidget::TransformTarget RenderWidget::activeTransformTarget() const {
    if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < scene_.objects.size()) {
        return TransformTarget::Object;
    }
    if (selectedLightIndex_ >= 0 && selectedLightIndex_ < scene_.lights.size()) {
        return TransformTarget::Light;
    }
    return TransformTarget::None;
}

bool RenderWidget::hasActiveSelection() const {
    return activeTransformTarget() != TransformTarget::None;
}

bool RenderWidget::canRenderActiveGizmo() const {
    if (activeTransformTarget() == TransformTarget::Object) {
        return true;
    }
    if (activeTransformTarget() != TransformTarget::Light) {
        return false;
    }
    if (transformMode_ == TransformMode::Scale) {
        return false;
    }

    const LightConfig& light = scene_.lights.at(selectedLightIndex_);
    return transformMode_ != TransformMode::Rotate || light.type != LightType::Point;
}

QVector3D RenderWidget::activeSelectionCenter() const {
    switch (activeTransformTarget()) {
    case TransformTarget::Object:
        return selectionCenter();
    case TransformTarget::Light:
        return lightCenter(selectedLightIndex_);
    case TransformTarget::None:
        break;
    }

    return cameraPosition_ + cameraFront_ * orbitDistance_;
}

float RenderWidget::activeSelectionRadius() const {
    switch (activeTransformTarget()) {
    case TransformTarget::Object:
        return selectionRadius();
    case TransformTarget::Light:
        return lightRadius(selectedLightIndex_);
    case TransformTarget::None:
        break;
    }

    return 1.0f;
}

QVector3D RenderWidget::activeGizmoOrigin() const {
    return activeSelectionCenter();
}

QQuaternion RenderWidget::activeGizmoOrientation() const {
    if (coordinateSpace_ != CoordinateSpace::Local) {
        return QQuaternion();
    }

    if (activeTransformTarget() == TransformTarget::Object) {
        return buildWorldRotation(selectedObjectIndex_);
    }
    if (activeTransformTarget() == TransformTarget::Light) {
        return buildLightRotation(selectedLightIndex_);
    }

    return QQuaternion();
}

void RenderWidget::pickAt(const QPointF& position, Qt::KeyboardModifiers modifiers) {
    float objectDistance = std::numeric_limits<float>::max();
    float lightDistance = std::numeric_limits<float>::max();
    const int pickedObject = pickObject(position, &objectDistance);
    const int pickedLight = pickLight(position, &lightDistance);
    const bool preferLight =
        pickedLight >= 0 && (pickedObject < 0 || lightDistance <= objectDistance);

    if (preferLight) {
        applySelection({}, -1, true);
        emit lightSelectionChanged(pickedLight);
        return;
    }

    QVector<int> nextSelection = selectedObjectIndices_;
    int activeIndex = selectedObjectIndex_;

    if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier)) {
        if (pickedObject >= 0) {
            if (nextSelection.contains(pickedObject)) {
                nextSelection.removeAll(pickedObject);
            } else {
                nextSelection.append(pickedObject);
            }
            activeIndex = nextSelection.isEmpty() ? -1 : pickedObject;
        }
    } else {
        nextSelection = pickedObject >= 0 ? QVector<int>{pickedObject} : QVector<int>{};
        activeIndex = pickedObject;
    }

    applySelection(nextSelection, activeIndex, true);
}

void RenderWidget::selectInRect(const QRect& rect, Qt::KeyboardModifiers modifiers) {
    QVector<int> nextSelection = pickObjectsInRect(rect);
    int activeIndex = nextSelection.isEmpty() ? -1 : nextSelection.constLast();
    if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier)) {
        QSet<int> merged;
        for (int index : selectedObjectIndices_) {
            merged.insert(index);
        }
        for (int index : nextSelection) {
            merged.insert(index);
        }
        nextSelection = QVector<int>(merged.begin(), merged.end());
        std::sort(nextSelection.begin(), nextSelection.end());
        activeIndex = nextSelection.isEmpty() ? -1 : activeIndex;
    }

    applySelection(nextSelection, activeIndex, true);
}

int RenderWidget::pickObject(const QPointF& position, float* hitDistance) const {
    const Ray ray = buildPickRay(position);
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();

    for (int index = 0; index < scene_.objects.size(); ++index) {
        const RenderObjectConfig& object = scene_.objects.at(index);
        if (!object.visible) {
            continue;
        }

        float hitDistance = 0.0f;
        if (intersectObject(ray, index, &hitDistance) && hitDistance < bestDistance) {
            bestDistance = hitDistance;
            bestIndex = index;
        }
    }

    if (hitDistance) {
        *hitDistance = bestDistance;
    }
    return bestIndex;
}

int RenderWidget::pickLight(const QPointF& position, float* hitDistance) const {
    if (!scene_.debug.drawLightGizmo) {
        if (hitDistance) {
            *hitDistance = std::numeric_limits<float>::max();
        }
        return -1;
    }

    const Ray ray = buildPickRay(position);
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();

    for (int index = 0; index < scene_.lights.size(); ++index) {
        float candidateDistance = 0.0f;
        if (intersectLight(ray, index, &candidateDistance) && candidateDistance < bestDistance) {
            bestDistance = candidateDistance;
            bestIndex = index;
        }
    }

    if (hitDistance) {
        *hitDistance = bestDistance;
    }
    return bestIndex;
}

QVector<int> RenderWidget::pickObjectsInRect(const QRect& rect) const {
    QVector<int> hits;
    if (!rect.isValid()) {
        return hits;
    }

    const QRectF selectionRect = rect.normalized();
    for (int index = 0; index < scene_.objects.size(); ++index) {
        const RenderObjectConfig& object = scene_.objects.at(index);
        if (!object.visible) {
            continue;
        }

        QRectF projectedRect;
        if (projectObjectScreenRect(index, &projectedRect) && selectionRect.intersects(projectedRect)) {
            hits.append(index);
            continue;
        }

        QPointF centerPoint;
        if (projectToScreen(objectCenter(index), &centerPoint) && selectionRect.contains(centerPoint)) {
            hits.append(index);
        }
    }

    return hits;
}

RenderWidget::GizmoHandle RenderWidget::pickGizmoHandle(const QPointF& position) const {
    if (!canRenderActiveGizmo()) {
        return GizmoHandle::None;
    }

    if (transformMode_ == TransformMode::Rotate) {
        return pickRotationAxis(position);
    }

    return pickLinearGizmoHandle(position);
}

RenderWidget::GizmoHandle RenderWidget::pickLinearGizmoHandle(const QPointF& position) const {
    const QVector3D origin = activeGizmoOrigin();
    const float scale = gizmoScale();
    GizmoHandle bestAxis = GizmoHandle::None;
    float bestDistance = kLinearGizmoPickThresholdPixels;

    for (const GizmoHandle axis : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
        QPointF screenStart;
        QPointF screenEnd;
        if (!projectToScreen(origin, &screenStart)) {
            continue;
        }
        if (!projectToScreen(origin + (gizmoAxisDirection(axis) * scale), &screenEnd)) {
            continue;
        }

        const float distance = distanceToSegment(position, screenStart, screenEnd);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAxis = axis;
        }
    }

    if (transformMode_ == TransformMode::Translate) {
        const auto testPlane = [&](GizmoHandle planeHandle, const QVector3D& axisA, const QVector3D& axisB) {
            QPolygonF polygon;
            const QVector3D p0 = origin + ((axisA * kTranslatePlaneInner) + (axisB * kTranslatePlaneInner)) * scale;
            const QVector3D p1 = origin + ((axisA * kTranslatePlaneOuter) + (axisB * kTranslatePlaneInner)) * scale;
            const QVector3D p2 = origin + ((axisA * kTranslatePlaneOuter) + (axisB * kTranslatePlaneOuter)) * scale;
            const QVector3D p3 = origin + ((axisA * kTranslatePlaneInner) + (axisB * kTranslatePlaneOuter)) * scale;
            QPointF s0;
            QPointF s1;
            QPointF s2;
            QPointF s3;
            if (!projectToScreen(p0, &s0) || !projectToScreen(p1, &s1) || !projectToScreen(p2, &s2) || !projectToScreen(p3, &s3)) {
                return;
            }

            polygon << s0 << s1 << s2 << s3;
            if (polygon.containsPoint(position, Qt::OddEvenFill)) {
                bestAxis = planeHandle;
                bestDistance = -1.0f;
            } else if (bestDistance >= 0.0f) {
                for (int edgeIndex = 0; edgeIndex < polygon.size(); ++edgeIndex) {
                    const QPointF edgeStart = polygon.at(edgeIndex);
                    const QPointF edgeEnd = polygon.at((edgeIndex + 1) % polygon.size());
                    const float edgeDistance = distanceToSegment(position, edgeStart, edgeEnd);
                    if (edgeDistance < qMin(bestDistance, kPlaneGizmoPickPadding)) {
                        bestAxis = planeHandle;
                        bestDistance = edgeDistance;
                    }
                }
            }
        };

        testPlane(GizmoHandle::XY, gizmoAxisDirection(GizmoHandle::X), gizmoAxisDirection(GizmoHandle::Y));
        if (bestDistance >= 0.0f) {
            testPlane(GizmoHandle::XZ, gizmoAxisDirection(GizmoHandle::X), gizmoAxisDirection(GizmoHandle::Z));
        }
        if (bestDistance >= 0.0f) {
            testPlane(GizmoHandle::YZ, gizmoAxisDirection(GizmoHandle::Y), gizmoAxisDirection(GizmoHandle::Z));
        }
    }

    return bestAxis;
}

RenderWidget::GizmoHandle RenderWidget::pickRotationAxis(const QPointF& position) const {
    const QVector3D origin = activeGizmoOrigin();
    const float radius = gizmoScale();
    GizmoHandle bestAxis = GizmoHandle::None;
    float bestDistance = kRotationGizmoPickThresholdPixels;

    for (const GizmoHandle axis : {GizmoHandle::X, GizmoHandle::Y, GizmoHandle::Z}) {
        const QVector3D axisDirection = gizmoAxisDirection(axis).normalized();
        QVector3D ringX = QVector3D::crossProduct(axisDirection, worldUp_);
        if (ringX.lengthSquared() < 1e-4f) {
            ringX = QVector3D::crossProduct(axisDirection, cameraRight_);
        }
        ringX.normalize();
        QVector3D ringY = QVector3D::crossProduct(axisDirection, ringX).normalized();

        for (int segment = 0; segment < kRotationRingSegments; ++segment) {
            const float angle0 =
                (static_cast<float>(segment) / static_cast<float>(kRotationRingSegments)) * kTau;
            const float angle1 =
                (static_cast<float>(segment + 1) / static_cast<float>(kRotationRingSegments)) * kTau;

            const QVector3D worldPoint0 =
                origin + ((ringX * std::cos(angle0)) + (ringY * std::sin(angle0))) * radius;
            const QVector3D worldPoint1 =
                origin + ((ringX * std::cos(angle1)) + (ringY * std::sin(angle1))) * radius;

            QPointF screen0;
            QPointF screen1;
            if (!projectToScreen(worldPoint0, &screen0) || !projectToScreen(worldPoint1, &screen1)) {
                continue;
            }

            const float distance = distanceToSegment(position, screen0, screen1);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestAxis = axis;
            }
        }
    }

    return bestAxis;
}

RenderWidget::Ray RenderWidget::buildPickRay(const QPointF& position) const {
    const float viewportWidth = static_cast<float>(qMax(width(), 1));
    const float viewportHeight = static_cast<float>(qMax(height(), 1));
    const float x = (2.0f * static_cast<float>(position.x()) / viewportWidth) - 1.0f;
    const float y = 1.0f - (2.0f * static_cast<float>(position.y()) / viewportHeight);

    bool invertible = false;
    const QMatrix4x4 inverseViewProjection =
        (buildProjectionMatrix() * buildViewMatrix()).inverted(&invertible);
    require(invertible, QStringLiteral("failed to invert view projection matrix"));

    QVector4D nearPoint = inverseViewProjection * QVector4D(x, y, -1.0f, 1.0f);
    QVector4D farPoint = inverseViewProjection * QVector4D(x, y, 1.0f, 1.0f);
    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    return Ray{
        nearPoint.toVector3D(),
        (farPoint - nearPoint).toVector3D().normalized(),
    };
}

bool RenderWidget::intersectObject(
    const Ray& ray,
    int objectIndex,
    float* distance) const {
    if (objectIndex < 0 || objectIndex >= scene_.objects.size()) {
        return false;
    }

    bool invertible = false;
    const QMatrix4x4 model = buildObjectModelMatrix(objectIndex);
    const QMatrix4x4 inverseModel = model.inverted(&invertible);
    if (!invertible) {
        return false;
    }

    const QVector3D localOrigin = (inverseModel * QVector4D(ray.origin, 1.0f)).toVector3D();
    const QVector3D localDirection = (inverseModel * QVector4D(ray.direction, 0.0f)).toVector3D();
    if (localDirection.lengthSquared() < 1e-8f) {
        return false;
    }

    float localDistance = 0.0f;
    const QVector3D boundsMin = objectLocalBoundsMin(objectIndex);
    const QVector3D boundsMax = objectLocalBoundsMax(objectIndex);
    QVector3D shiftedOrigin = localOrigin - ((boundsMin + boundsMax) * 0.5f);
    QVector3D scaledOrigin = shiftedOrigin;
    QVector3D scaledDirection = localDirection;
    const QVector3D halfExtents = (boundsMax - boundsMin) * 0.5f;
    if (halfExtents.x() <= 1e-6f || halfExtents.y() <= 1e-6f || halfExtents.z() <= 1e-6f) {
        return false;
    }
    scaledOrigin.setX(scaledOrigin.x() / halfExtents.x());
    scaledOrigin.setY(scaledOrigin.y() / halfExtents.y());
    scaledOrigin.setZ(scaledOrigin.z() / halfExtents.z());
    scaledDirection.setX(scaledDirection.x() / halfExtents.x());
    scaledDirection.setY(scaledDirection.y() / halfExtents.y());
    scaledDirection.setZ(scaledDirection.z() / halfExtents.z());
    if (!intersectUnitCube(scaledOrigin, scaledDirection, &localDistance)) {
        return false;
    }

    const QVector3D localHitPoint = scaledOrigin + (scaledDirection * localDistance);
    const QVector3D rescaledLocalHitPoint(
        localHitPoint.x() * halfExtents.x(),
        localHitPoint.y() * halfExtents.y(),
        localHitPoint.z() * halfExtents.z());
    const QVector3D finalLocalHitPoint = rescaledLocalHitPoint + ((boundsMin + boundsMax) * 0.5f);
    const QVector3D worldHitPoint = (model * QVector4D(finalLocalHitPoint, 1.0f)).toVector3D();
    *distance = (worldHitPoint - ray.origin).length();
    return true;
}

bool RenderWidget::intersectLight(const Ray& ray, int lightIndex, float* distance) const {
    if (lightIndex < 0 || lightIndex >= scene_.lights.size()) {
        return false;
    }

    const viewport::LightMarkerStyle markerStyle =
        viewport::buildLightMarkerStyle(scene_.lights.at(lightIndex), false);
    bool invertible = false;
    const QMatrix4x4 model = markerStyle.modelMatrix;
    const QMatrix4x4 inverseModel = model.inverted(&invertible);
    if (!invertible) {
        return false;
    }

    const QVector3D localOrigin = (inverseModel * QVector4D(ray.origin, 1.0f)).toVector3D();
    const QVector3D localDirection = (inverseModel * QVector4D(ray.direction, 0.0f)).toVector3D();
    if (localDirection.lengthSquared() < 1e-8f) {
        return false;
    }

    float localDistance = 0.0f;
    if (!intersectUnitCube(localOrigin, localDirection, &localDistance)) {
        return false;
    }

    const QVector3D localHitPoint = localOrigin + (localDirection * localDistance);
    const QVector3D worldHitPoint = (model * QVector4D(localHitPoint, 1.0f)).toVector3D();
    *distance = (worldHitPoint - ray.origin).length();
    return true;
}

bool RenderWidget::solveAxisDragParameter(const Ray& ray, GizmoHandle axis, float* parameter) const {
    if (axis == GizmoHandle::None || isPlaneHandle(axis) || activeTransformTarget() == TransformTarget::None) {
        return false;
    }

    const QVector3D axisOrigin = activeGizmoOrigin();
    const QVector3D axisDirection = gizmoAxisDirection(axis);
    const QVector3D separation = ray.origin - axisOrigin;

    const float a = QVector3D::dotProduct(ray.direction, ray.direction);
    const float b = QVector3D::dotProduct(ray.direction, axisDirection);
    const float c = QVector3D::dotProduct(axisDirection, axisDirection);
    const float d = QVector3D::dotProduct(ray.direction, separation);
    const float e = QVector3D::dotProduct(axisDirection, separation);
    const float denominator = (a * c) - (b * b);

    if (qAbs(denominator) < 1e-5f) {
        return false;
    }

    *parameter = ((a * e) - (b * d)) / denominator;
    return true;
}

bool RenderWidget::solvePlaneDragPoint(const Ray& ray, GizmoHandle plane, QVector3D* point) const {
    if (!point || !isPlaneHandle(plane) || activeTransformTarget() == TransformTarget::None) {
        return false;
    }

    const QVector3D planeOrigin = activeGizmoOrigin();
    const QVector3D planeNormal = gizmoPlaneNormal(plane);
    const float denominator = QVector3D::dotProduct(ray.direction, planeNormal);
    if (qAbs(denominator) < 1e-5f) {
        return false;
    }

    const float t = QVector3D::dotProduct(planeOrigin - ray.origin, planeNormal) / denominator;
    if (t < 0.0f) {
        return false;
    }

    *point = ray.origin + (ray.direction * t);
    return true;
}

QVector3D RenderWidget::baseAxisDirection(GizmoHandle axis) const {
    switch (axis) {
    case GizmoHandle::X:
        return QVector3D(1.0f, 0.0f, 0.0f);
    case GizmoHandle::Y:
        return QVector3D(0.0f, 1.0f, 0.0f);
    case GizmoHandle::Z:
        return QVector3D(0.0f, 0.0f, 1.0f);
    case GizmoHandle::None:
    case GizmoHandle::XY:
    case GizmoHandle::XZ:
    case GizmoHandle::YZ:
        break;
    }

    return QVector3D(1.0f, 0.0f, 0.0f);
}

QVector3D RenderWidget::gizmoAxisDirection(GizmoHandle axis) const {
    const QVector3D baseAxis = baseAxisDirection(axis);
    if (coordinateSpace_ == CoordinateSpace::Local && activeTransformTarget() != TransformTarget::None) {
        return activeGizmoOrientation().rotatedVector(baseAxis).normalized();
    }

    return baseAxis;
}

QVector3D RenderWidget::gizmoPlaneNormal(GizmoHandle plane) const {
    switch (plane) {
    case GizmoHandle::XY:
        return QVector3D::crossProduct(gizmoAxisDirection(GizmoHandle::X), gizmoAxisDirection(GizmoHandle::Y)).normalized();
    case GizmoHandle::XZ:
        return QVector3D::crossProduct(gizmoAxisDirection(GizmoHandle::X), gizmoAxisDirection(GizmoHandle::Z)).normalized();
    case GizmoHandle::YZ:
        return QVector3D::crossProduct(gizmoAxisDirection(GizmoHandle::Y), gizmoAxisDirection(GizmoHandle::Z)).normalized();
    default:
        return QVector3D(0.0f, 1.0f, 0.0f);
    }
}

float RenderWidget::gizmoScale() const {
    const float distanceToTarget = (cameraPosition_ - activeGizmoOrigin()).length();
    return qMax(0.85f, distanceToTarget * kGizmoViewScale);
}

float RenderWidget::linearSnapStep() const {
    return qMax(0.001f, moveSnapStep_);
}

QVector3D RenderWidget::snapPositionForHandle(const QVector3D& position, GizmoHandle handle, float step) const {
    QVector3D snapped = position;
    const auto snapAxis = [&](GizmoHandle axis) {
        const int axisIndex = axisToIndex(axis);
        if (axisIndex >= 0) {
            setVectorComponent(&snapped, axisIndex, snapScalar(vectorComponent(snapped, axisIndex), step));
        }
    };

    if (isPlaneHandle(handle)) {
        if (handle == GizmoHandle::XY || handle == GizmoHandle::XZ) {
            snapAxis(GizmoHandle::X);
        }
        if (handle == GizmoHandle::XY || handle == GizmoHandle::YZ) {
            snapAxis(GizmoHandle::Y);
        }
        if (handle == GizmoHandle::XZ || handle == GizmoHandle::YZ) {
            snapAxis(GizmoHandle::Z);
        }
        return snapped;
    }

    snapAxis(handle);
    return snapped;
}

QVector3D RenderWidget::snapScaleForAxis(const QVector3D& scale, GizmoHandle axis, float step) const {
    const int axisIndex = axisToIndex(axis);
    if (axisIndex < 0) {
        return scale;
    }

    QVector3D snapped = scale;
    const float component = qMax(
        kMinScaleComponent,
        snapScalar(vectorComponent(scale, axisIndex), qMax(0.001f, step)));
    setVectorComponent(&snapped, axisIndex, component);
    return snapped;
}

float RenderWidget::snapScalar(float value, float step) const {
    const float safeStep = qMax(0.001f, step);
    return std::round(value / safeStep) * safeStep;
}

float RenderWidget::rotationDragDegrees(const QPoint& mousePosition) const {
    QPointF screenOrigin;
    QPointF screenAxisPoint;
    QVector3D axisDirection = baseAxisDirection(activeGizmoHandle_);
    if (coordinateSpace_ == CoordinateSpace::Local && activeTransformTarget() != TransformTarget::None) {
        axisDirection = activeGizmoOrientation().rotatedVector(axisDirection).normalized();
    }

    if (!projectToScreen(activeGizmoOrigin(), &screenOrigin) ||
        !projectToScreen(activeGizmoOrigin() + (axisDirection * gizmoScale()), &screenAxisPoint)) {
        return static_cast<float>(mousePosition.x() - gizmoDragStartMousePosition_.x()) * kRotateSensitivityDegreesPerPixel;
    }

    QVector2D axisScreen = QVector2D(screenAxisPoint - screenOrigin);
    if (axisScreen.lengthSquared() <= 1e-6f) {
        return static_cast<float>(mousePosition.x() - gizmoDragStartMousePosition_.x()) * kRotateSensitivityDegreesPerPixel;
    }

    axisScreen.normalize();
    const QVector2D tangent(-axisScreen.y(), axisScreen.x());
    const QVector2D dragDelta(mousePosition - gizmoDragStartMousePosition_);
    return QVector2D::dotProduct(dragDelta, tangent) * kRotateSensitivityDegreesPerPixel;
}

void RenderWidget::emitTransformPreview() {
    if (selectedObjectIndex_ < 0 || selectedObjectIndex_ >= scene_.objects.size()) {
        return;
    }

    const RenderObjectConfig& object = scene_.objects.at(selectedObjectIndex_);
    emit objectTransformPreview(
        selectedObjectIndex_,
        object.position,
        object.rotationDegrees,
        object.scale);
}

void RenderWidget::emitLightTransformPreview() {
    if (selectedLightIndex_ < 0 || selectedLightIndex_ >= scene_.lights.size()) {
        return;
    }

    const LightConfig& light = scene_.lights.at(selectedLightIndex_);
    emit lightTransformPreview(selectedLightIndex_, light.position, normalizedLightDirection(light));
}

void RenderWidget::renderGridPass() {
    debugProgram_.setUniformValue("uModel", buildGridModelMatrix());
    debugProgram_.setUniformValue("uColorTint", QVector3D(1.0f, 1.0f, 1.0f));
    gridMesh_.vao.bind();
    glDrawElements(gridMesh_.primitiveType, gridMesh_.indexCount, GL_UNSIGNED_INT, nullptr);
    gridMesh_.vao.release();
}

void RenderWidget::renderLightMarkers() {
    for (int lightIndex = 0; lightIndex < scene_.lights.size(); ++lightIndex) {
        const viewport::LightMarkerStyle markerStyle =
            viewport::buildLightMarkerStyle(scene_.lights.at(lightIndex), lightIndex == selectedLightIndex_);
        debugProgram_.setUniformValue("uModel", markerStyle.modelMatrix);
        debugProgram_.setUniformValue("uColorTint", markerStyle.tint);

        cubeMesh_.vao.bind();
        glDrawElements(cubeMesh_.primitiveType, cubeMesh_.indexCount, GL_UNSIGNED_INT, nullptr);
        cubeMesh_.vao.release();
    }
}

void RenderWidget::renderObjectSelectionPass() {
    for (int index : selectedObjectIndices_) {
        if (index < 0 || index >= scene_.objects.size() || !scene_.objects.at(index).visible) {
            continue;
        }

        debugProgram_.setUniformValue(
            "uModel",
            buildSelectionModelMatrix(index, index == selectedObjectIndex_ ? 1.06f : 1.03f));
        debugProgram_.setUniformValue(
            "uColorTint",
            index == selectedObjectIndex_
                ? QVector3D(1.15f, 0.82f, 0.22f)
                : QVector3D(0.42f, 0.75f, 1.15f));
        selectionMesh_.vao.bind();
        glDrawElements(selectionMesh_.primitiveType, selectionMesh_.indexCount, GL_UNSIGNED_INT, nullptr);
        selectionMesh_.vao.release();
    }
}

void RenderWidget::renderActiveGizmoPass() {
    if (!canRenderActiveGizmo()) {
        return;
    }

    debugProgram_.setUniformValue("uModel", buildGizmoModelMatrix());
    if (transformMode_ == TransformMode::Translate) {
        debugProgram_.setUniformValue("uColorTint", QVector3D(1.0f, 1.0f, 1.0f));
        translateAxisGizmoMesh_.vao.bind();
        glDrawElements(
            translateAxisGizmoMesh_.primitiveType,
            translateAxisGizmoMesh_.indexCount,
            GL_UNSIGNED_INT,
            nullptr);
        translateAxisGizmoMesh_.vao.release();

        translatePlaneGizmoMesh_.vao.bind();
        glDrawElements(
            translatePlaneGizmoMesh_.primitiveType,
            translatePlaneGizmoMesh_.indexCount,
            GL_UNSIGNED_INT,
            nullptr);
        translatePlaneGizmoMesh_.vao.release();

        if (activeGizmoHandle_ != GizmoHandle::None) {
            const QVector3D highlightTint =
                activeGizmoHandle_ == GizmoHandle::X || activeGizmoHandle_ == GizmoHandle::XY || activeGizmoHandle_ == GizmoHandle::XZ
                    ? QVector3D(2.2f, 0.8f, 0.8f)
                : activeGizmoHandle_ == GizmoHandle::Y || activeGizmoHandle_ == GizmoHandle::YZ
                    ? QVector3D(0.8f, 2.2f, 0.8f)
                    : QVector3D(0.8f, 1.2f, 2.2f);
            debugProgram_.setUniformValue("uColorTint", highlightTint);
            if (isPlaneHandle(activeGizmoHandle_)) {
                translatePlaneGizmoMesh_.vao.bind();
                glDrawElements(
                    translatePlaneGizmoMesh_.primitiveType,
                    kTranslatePlaneIndexCount,
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<size_t>(translatePlaneStartIndex(activeGizmoHandle_)) * sizeof(quint32)));
                translatePlaneGizmoMesh_.vao.release();
            } else {
                translateAxisGizmoMesh_.vao.bind();
                glDrawElements(
                    translateAxisGizmoMesh_.primitiveType,
                    gizmoAxisIndexCount(transformMode_),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<size_t>(gizmoAxisStartIndex(transformMode_, activeGizmoHandle_)) * sizeof(quint32)));
                translateAxisGizmoMesh_.vao.release();
            }
        }
        return;
    }

    MeshHandle* gizmoMesh = transformMode_ == TransformMode::Rotate ? &rotationGizmoMesh_ : &scaleGizmoMesh_;
    debugProgram_.setUniformValue("uColorTint", QVector3D(1.0f, 1.0f, 1.0f));
    gizmoMesh->vao.bind();
    glDrawElements(gizmoMesh->primitiveType, gizmoMesh->indexCount, GL_UNSIGNED_INT, nullptr);

    if (activeGizmoHandle_ != GizmoHandle::None) {
        const QVector3D highlightTint =
            activeGizmoHandle_ == GizmoHandle::X ? QVector3D(2.2f, 0.8f, 0.8f)
            : activeGizmoHandle_ == GizmoHandle::Y ? QVector3D(0.8f, 2.2f, 0.8f)
                                                   : QVector3D(0.8f, 1.2f, 2.2f);
        debugProgram_.setUniformValue("uColorTint", highlightTint);
        glDrawElements(
            gizmoMesh->primitiveType,
            gizmoAxisIndexCount(transformMode_),
            GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<size_t>(gizmoAxisStartIndex(transformMode_, activeGizmoHandle_)) * sizeof(quint32)));
    }

    gizmoMesh->vao.release();
}

void RenderWidget::renderDebugPass(const QMatrix4x4& view, const QMatrix4x4& projection) {
    const viewport::DebugRenderPlan renderPlan = viewport::buildDebugRenderPlan(
        scene_.debug,
        !selectedObjectIndices_.isEmpty(),
        selectedLightIndex_ >= 0,
        canRenderActiveGizmo());
    if (!renderPlan.hasAnyPass()) {
        return;
    }

    debugProgram_.bind();
    debugProgram_.setUniformValue("uView", view);
    debugProgram_.setUniformValue("uProjection", projection);

    if (renderPlan.drawGrid) {
        renderGridPass();
    }

    if (renderPlan.drawAxes) {
        QMatrix4x4 axesModel;
        debugProgram_.setUniformValue("uModel", axesModel);
        debugProgram_.setUniformValue("uColorTint", QVector3D(1.0f, 1.0f, 1.0f));
        axesMesh_.vao.bind();
        glDrawElements(axesMesh_.primitiveType, axesMesh_.indexCount, GL_UNSIGNED_INT, nullptr);
        axesMesh_.vao.release();
    }

    if (renderPlan.drawLightMarkers) {
        renderLightMarkers();
    }

    if (renderPlan.requiresOverlayPass()) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        if (renderPlan.drawObjectSelection) {
            renderObjectSelectionPass();
        }
        if (renderPlan.drawGizmo) {
            renderActiveGizmoPass();
        }
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }

    debugProgram_.release();
}

void RenderWidget::initializeShaders() {
    sceneRenderer_.cleanup();
    sceneRenderer_.initialize([this](const QString& path) {
        return resolvePath(path);
    });

    debugProgram_.removeAllShaders();
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

void RenderWidget::cleanupOpenGL() {
    if (!context()) {
        return;
    }

    makeCurrent();
    destroySceneResources();
    sceneRenderer_.cleanup();
    debugProgram_.removeAllShaders();
    doneCurrent();
}

void RenderWidget::initializeGeometry() {
    rendermesh::uploadMesh(&cubeMesh_, createCubeVertices(), createCubeIndices(), GL_TRIANGLES);

    const std::vector<RenderVertex> axesVertices = createAxesVertices(scene_.debug.axesLength);
    rendermesh::uploadMesh(
        &axesMesh_,
        axesVertices,
        createSequentialIndices(static_cast<int>(axesVertices.size())),
        GL_LINES);

    const int gridRingCount = qMax(
        kMinimumGridRingCount,
        static_cast<int>(std::ceil(scene_.debug.gridHalfExtent / qMax(0.1f, scene_.debug.gridStep))));
    const std::vector<RenderVertex> gridVertices = createGridVertices(static_cast<float>(gridRingCount), 1.0f);
    rendermesh::uploadMesh(
        &gridMesh_,
        gridVertices,
        createSequentialIndices(static_cast<int>(gridVertices.size())),
        GL_LINES);

    const std::vector<RenderVertex> translateVertices = createAxesVertices(1.0f);
    rendermesh::uploadMesh(
        &translateAxisGizmoMesh_,
        translateVertices,
        createSequentialIndices(static_cast<int>(translateVertices.size())),
        GL_LINES);

    const std::vector<RenderVertex> translatePlaneVertices =
        createTranslatePlaneVertices(kTranslatePlaneInner, kTranslatePlaneOuter);
    rendermesh::uploadMesh(
        &translatePlaneGizmoMesh_,
        translatePlaneVertices,
        createSequentialIndices(static_cast<int>(translatePlaneVertices.size())),
        GL_LINES);

    const std::vector<RenderVertex> scaleVertices = createScaleGizmoVertices(1.0f, 0.08f);
    rendermesh::uploadMesh(
        &scaleGizmoMesh_,
        scaleVertices,
        createSequentialIndices(static_cast<int>(scaleVertices.size())),
        GL_LINES);

    const std::vector<RenderVertex> rotationVertices =
        createRotationRingVertices(1.0f, kRotationRingSegments);
    rendermesh::uploadMesh(
        &rotationGizmoMesh_,
        rotationVertices,
        createSequentialIndices(static_cast<int>(rotationVertices.size())),
        GL_LINES);

    const std::vector<RenderVertex> selectionVertices = createSelectionBracketVertices(0.28f);
    rendermesh::uploadMesh(
        &selectionMesh_,
        selectionVertices,
        createSequentialIndices(static_cast<int>(selectionVertices.size())),
        GL_LINES);
}

void RenderWidget::rebuildRenderScene(bool reloadResources) {
    resourceManager_.sync(scene_, reloadResources, [this](const QString& path) {
        return resolvePath(path);
    });
    compiledScene_ = RenderSceneCompiler::compile(scene_, resourceManager_, [this](const QString& path) {
        return resolvePath(path);
    });
}

void RenderWidget::destroySceneResources() {
    compiledScene_.clear();
    resourceManager_.clear();
    rendermesh::destroyMesh(&cubeMesh_);
    rendermesh::destroyMesh(&axesMesh_);
    rendermesh::destroyMesh(&gridMesh_);
    rendermesh::destroyMesh(&translateAxisGizmoMesh_);
    rendermesh::destroyMesh(&translatePlaneGizmoMesh_);
    rendermesh::destroyMesh(&scaleGizmoMesh_);
    rendermesh::destroyMesh(&rotationGizmoMesh_);
    rendermesh::destroyMesh(&selectionMesh_);
}

void RenderWidget::resetCameraFromScene() {
    cameraPosition_ = scene_.camera.position;
    yaw_ = scene_.camera.yaw;
    pitch_ = scene_.camera.pitch;
    moveSpeed_ = scene_.camera.moveSpeed;
    lookSpeed_ = scene_.camera.lookSpeed;
    fov_ = scene_.camera.fov;
    nearClip_ = scene_.camera.nearClip;
    farClip_ = scene_.camera.farClip;
    updateCameraVectors();
    cameraTarget_ = cameraPosition_ + cameraFront_ * 10.0f;
    updateOrbitDistance();
    emitCameraState();
}

void RenderWidget::syncMouseCapture() {
    mouseMode_ = MouseMode::None;
    activeGizmoHandle_ = GizmoHandle::None;
    pendingPick_ = false;
    transformInteractionActive_ = false;
    pressedKeys_.clear();
    unsetCursor();
}

std::vector<RenderVertex> RenderWidget::createCubeVertices() {
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

std::vector<RenderVertex> RenderWidget::createAxesVertices(float length) {
    return {
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, length, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({0.0f, 0.0f, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
    };
}

std::vector<RenderVertex> RenderWidget::createTranslatePlaneVertices(float inner, float outer) {
    return {
        makeVertex({inner, inner, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({outer, inner, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({outer, inner, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({outer, outer, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({outer, outer, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({inner, outer, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({inner, outer, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),
        makeVertex({inner, inner, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.75f, 0.22f}),

        makeVertex({inner, 0.0f, inner}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({outer, 0.0f, inner}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({outer, 0.0f, inner}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({outer, 0.0f, outer}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({outer, 0.0f, outer}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({inner, 0.0f, outer}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({inner, 0.0f, outer}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),
        makeVertex({inner, 0.0f, inner}, {}, 0.0f, 0.0f, {1.0f, 0.6f, 0.35f}),

        makeVertex({0.0f, inner, inner}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, outer, inner}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, outer, inner}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, outer, outer}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, outer, outer}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, inner, outer}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, inner, outer}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
        makeVertex({0.0f, inner, inner}, {}, 0.0f, 0.0f, {0.35f, 0.95f, 0.8f}),
    };
}

std::vector<RenderVertex> RenderWidget::createScaleGizmoVertices(float length, float handleSize) {
    return {
        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, -handleSize, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, handleSize, 0.0f}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, 0.0f, -handleSize}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),
        makeVertex({length, 0.0f, handleSize}, {}, 0.0f, 0.0f, {1.0f, 0.2f, 0.2f}),

        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, length, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({-handleSize, length, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({handleSize, length, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, length, -handleSize}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),
        makeVertex({0.0f, length, handleSize}, {}, 0.0f, 0.0f, {0.2f, 1.0f, 0.2f}),

        makeVertex({0.0f, 0.0f, 0.0f}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({0.0f, 0.0f, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({-handleSize, 0.0f, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({handleSize, 0.0f, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({0.0f, -handleSize, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
        makeVertex({0.0f, handleSize, length}, {}, 0.0f, 0.0f, {0.2f, 0.4f, 1.0f}),
    };
}

std::vector<RenderVertex> RenderWidget::createRotationRingVertices(float radius, int segments) {
    std::vector<RenderVertex> vertices;
    vertices.reserve(static_cast<size_t>(segments * 2 * 3));

    const auto appendRing = [&vertices, radius, segments](
                                const QVector3D& axisX,
                                const QVector3D& axisY,
                                const QVector3D& color) {
        for (int segment = 0; segment < segments; ++segment) {
            const float angle0 =
                (static_cast<float>(segment) / static_cast<float>(segments)) * kTau;
            const float angle1 =
                (static_cast<float>(segment + 1) / static_cast<float>(segments)) * kTau;

            const QVector3D position0 =
                ((axisX * std::cos(angle0)) + (axisY * std::sin(angle0))) * radius;
            const QVector3D position1 =
                ((axisX * std::cos(angle1)) + (axisY * std::sin(angle1))) * radius;
            vertices.push_back(makeVertex(position0, {}, 0.0f, 0.0f, color));
            vertices.push_back(makeVertex(position1, {}, 0.0f, 0.0f, color));
        }
    };

    appendRing(QVector3D(0.0f, 1.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f), QVector3D(1.0f, 0.2f, 0.2f));
    appendRing(QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f), QVector3D(0.2f, 1.0f, 0.2f));
    appendRing(QVector3D(1.0f, 0.0f, 0.0f), QVector3D(0.0f, 1.0f, 0.0f), QVector3D(0.2f, 0.4f, 1.0f));

    return vertices;
}

std::vector<RenderVertex> RenderWidget::createSelectionBracketVertices(float lengthRatio) {
    const float minCorner = -0.5f;
    const float maxCorner = 0.5f;
    const float inset = qBound(0.08f, lengthRatio, 0.48f);

    std::vector<RenderVertex> vertices;
    vertices.reserve(48);

    const auto appendCorner = [&vertices, inset](float x, float y, float z, const QVector3D& color) {
        const float xInner = x > 0.0f ? x - inset : x + inset;
        const float yInner = y > 0.0f ? y - inset : y + inset;
        const float zInner = z > 0.0f ? z - inset : z + inset;

        vertices.push_back(makeVertex({x, y, z}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({xInner, y, z}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({x, y, z}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({x, yInner, z}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({x, y, z}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({x, y, zInner}, {}, 0.0f, 0.0f, color));
    };

    const QVector3D warm(1.0f, 0.72f, 0.18f);
    appendCorner(minCorner, minCorner, minCorner, warm);
    appendCorner(minCorner, minCorner, maxCorner, warm);
    appendCorner(minCorner, maxCorner, minCorner, warm);
    appendCorner(minCorner, maxCorner, maxCorner, warm);
    appendCorner(maxCorner, minCorner, minCorner, warm);
    appendCorner(maxCorner, minCorner, maxCorner, warm);
    appendCorner(maxCorner, maxCorner, minCorner, warm);
    appendCorner(maxCorner, maxCorner, maxCorner, warm);

    return vertices;
}

std::vector<RenderVertex> RenderWidget::createGridVertices(float halfExtent, float step) {
    const float safeHalfExtent = qMax(2.0f, halfExtent);
    const float safeStep = qMax(0.25f, step);
    const int lineCount = static_cast<int>(std::floor(safeHalfExtent / safeStep));
    const float y = 0.001f;

    std::vector<RenderVertex> vertices;
    vertices.reserve(static_cast<size_t>((lineCount * 2 + 1) * 4));

    for (int index = -lineCount; index <= lineCount; ++index) {
        const float offset = static_cast<float>(index) * safeStep;
        const bool axisLine = index == 0;
        const bool majorLine = axisLine || (std::abs(index) % 10 == 0);
        const QVector3D color = axisLine
            ? QVector3D(0.48f, 0.52f, 0.58f)
            : majorLine
                ? QVector3D(0.34f, 0.36f, 0.4f)
                : QVector3D(0.26f, 0.28f, 0.31f);

        vertices.push_back(makeVertex({-safeHalfExtent, y, offset}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({safeHalfExtent, y, offset}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({offset, y, -safeHalfExtent}, {}, 0.0f, 0.0f, color));
        vertices.push_back(makeVertex({offset, y, safeHalfExtent}, {}, 0.0f, 0.0f, color));
    }

    return vertices;
}

std::vector<quint32> RenderWidget::createSequentialIndices(int vertexCount) {
    std::vector<quint32> indices;
    indices.reserve(static_cast<size_t>(vertexCount));
    for (int index = 0; index < vertexCount; ++index) {
        indices.push_back(static_cast<quint32>(index));
    }
    return indices;
}

QString RenderWidget::resolvePath(const QString& relativePath) const {
    const QFileInfo fileInfo(relativePath);
    if (fileInfo.isAbsolute()) {
        return QDir::cleanPath(fileInfo.absoluteFilePath());
    }

    return QDir::cleanPath(
        QFileInfo(QDir(QCoreApplication::applicationDirPath()), relativePath).absoluteFilePath());
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

QMatrix4x4 RenderWidget::buildObjectModelMatrix(int index) const {
    if (index >= 0 && index < compiledScene_.objects.size()) {
        return compiledScene_.objects.at(index).worldTransform;
    }
    return scenegraph::buildWorldTransform(scene_, index);
}

QMatrix4x4 RenderWidget::buildParentWorldMatrix(int index) const {
    if (index < 0 || index >= scene_.objects.size()) {
        return QMatrix4x4();
    }

    const QString parentId = scene_.objects.at(index).parentId;
    if (parentId.isEmpty()) {
        return QMatrix4x4();
    }

    const QHash<QString, int> idLookup = scenegraph::buildIdLookup(scene_);
    if (!idLookup.contains(parentId)) {
        return QMatrix4x4();
    }

    return scenegraph::buildWorldTransform(scene_, idLookup.value(parentId));
}

QMatrix4x4 RenderWidget::buildGizmoModelMatrix() const {
    QMatrix4x4 model;
    model.translate(activeGizmoOrigin());
    if (coordinateSpace_ == CoordinateSpace::Local && activeTransformTarget() != TransformTarget::None) {
        model.rotate(activeGizmoOrientation());
    }
    model.scale(gizmoScale());
    return model;
}

QMatrix4x4 RenderWidget::buildGridModelMatrix() const {
    return viewport::buildGridModelMatrix(effectiveGridStep(), cameraTarget_);
}

QMatrix4x4 RenderWidget::buildSelectionModelMatrix(int index, float inflate) const {
    QMatrix4x4 model = buildObjectModelMatrix(index);
    const QVector3D boundsMin = objectLocalBoundsMin(index);
    const QVector3D boundsMax = objectLocalBoundsMax(index);
    const QVector3D center = (boundsMin + boundsMax) * 0.5f;
    const QVector3D size = (boundsMax - boundsMin) * inflate;
    model.translate(center);
    model.scale(size);
    return model;
}

QQuaternion RenderWidget::buildWorldRotation(int index) const {
    if (index < 0 || index >= scene_.objects.size()) {
        return QQuaternion();
    }

    QQuaternion rotation;
    QVector<int> chain{index};
    QHash<QString, int> idLookup = scenegraph::buildIdLookup(scene_);
    QString parentId = scene_.objects.at(index).parentId;
    QSet<QString> visitedIds;
    visitedIds.insert(scene_.objects.at(index).id);
    while (!parentId.isEmpty() && idLookup.contains(parentId) && !visitedIds.contains(parentId)) {
        visitedIds.insert(parentId);
        const int parentIndex = idLookup.value(parentId);
        chain.prepend(parentIndex);
        parentId = scene_.objects.at(parentIndex).parentId;
    }

    for (int chainIndex : chain) {
        rotation = rotation * QQuaternion::fromEulerAngles(scene_.objects.at(chainIndex).rotationDegrees);
    }

    return rotation.normalized();
}

QQuaternion RenderWidget::buildLightRotation(int index) const {
    if (index < 0 || index >= scene_.lights.size()) {
        return QQuaternion();
    }

    return lightOrientationFromDirection(normalizedLightDirection(scene_.lights.at(index)));
}

QVector3D RenderWidget::objectLocalBoundsMin(int index) const {
    if (index < 0 || index >= compiledScene_.objects.size()) {
        return QVector3D(-0.5f, -0.5f, -0.5f);
    }
    return compiledScene_.objects.at(index).localBoundsMin;
}

QVector3D RenderWidget::objectLocalBoundsMax(int index) const {
    if (index < 0 || index >= compiledScene_.objects.size()) {
        return QVector3D(0.5f, 0.5f, 0.5f);
    }
    return compiledScene_.objects.at(index).localBoundsMax;
}

QVector3D RenderWidget::objectCenter(int index) const {
    if (index < 0 || index >= scene_.objects.size()) {
        return cameraPosition_ + cameraFront_ * orbitDistance_;
    }

    const QVector3D boundsCenter = (objectLocalBoundsMin(index) + objectLocalBoundsMax(index)) * 0.5f;
    return (buildObjectModelMatrix(index) * QVector4D(boundsCenter, 1.0f)).toVector3D();
}

float RenderWidget::objectRadius(int index) const {
    if (index < 0 || index >= scene_.objects.size()) {
        return 1.0f;
    }

    const QVector3D halfExtent = (objectLocalBoundsMax(index) - objectLocalBoundsMin(index)) * 0.5f;
    const QVector3D scale(
        buildObjectModelMatrix(index).column(0).toVector3D().length(),
        buildObjectModelMatrix(index).column(1).toVector3D().length(),
        buildObjectModelMatrix(index).column(2).toVector3D().length());
    return QVector3D(halfExtent.x() * scale.x(), halfExtent.y() * scale.y(), halfExtent.z() * scale.z()).length();
}

QVector3D RenderWidget::lightCenter(int index) const {
    if (index < 0 || index >= scene_.lights.size()) {
        return cameraPosition_ + cameraFront_ * orbitDistance_;
    }

    return scene_.lights.at(index).position;
}

float RenderWidget::lightRadius(int index) const {
    if (index < 0 || index >= scene_.lights.size()) {
        return 1.0f;
    }

    return viewport::buildLightMarkerStyle(scene_.lights.at(index), false).focusRadius;
}

QVector3D RenderWidget::selectionCenter() const {
    if (selectedObjectIndices_.isEmpty()) {
        return cameraPosition_ + cameraFront_ * orbitDistance_;
    }

    QVector3D center;
    for (int index : selectedObjectIndices_) {
        center += objectCenter(index);
    }
    return center / static_cast<float>(selectedObjectIndices_.size());
}

float RenderWidget::selectionRadius() const {
    if (selectedObjectIndices_.isEmpty()) {
        return 1.0f;
    }

    const QVector3D center = selectionCenter();
    float radius = 0.0f;
    for (int index : selectedObjectIndices_) {
        radius = qMax(radius, (objectCenter(index) - center).length() + objectRadius(index));
    }
    return radius;
}

float RenderWidget::effectiveGridStep() const {
    return viewport::effectiveGridStep(scene_.debug.gridStep, cameraPosition_, cameraTarget_);
}

QRect RenderWidget::normalizedSelectionRect() const {
    return QRect(boxSelectionStart_, boxSelectionCurrent_).normalized();
}

bool RenderWidget::projectObjectScreenRect(int index, QRectF* screenRect) const {
    if (!screenRect || index < 0 || index >= scene_.objects.size()) {
        return false;
    }

    const QVector3D boundsMin = objectLocalBoundsMin(index);
    const QVector3D boundsMax = objectLocalBoundsMax(index);
    const QMatrix4x4 model = buildObjectModelMatrix(index);
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    bool anyPoint = false;

    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                const QVector3D localPoint(
                    x == 0 ? boundsMin.x() : boundsMax.x(),
                    y == 0 ? boundsMin.y() : boundsMax.y(),
                    z == 0 ? boundsMin.z() : boundsMax.z());
                QPointF projected;
                if (!projectToScreen((model * QVector4D(localPoint, 1.0f)).toVector3D(), &projected)) {
                    continue;
                }
                anyPoint = true;
                minX = qMin(minX, static_cast<float>(projected.x()));
                minY = qMin(minY, static_cast<float>(projected.y()));
                maxX = qMax(maxX, static_cast<float>(projected.x()));
                maxY = qMax(maxY, static_cast<float>(projected.y()));
            }
        }
    }

    if (!anyPoint) {
        return false;
    }

    *screenRect = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
    return true;
}

bool RenderWidget::projectToScreen(const QVector3D& worldPoint, QPointF* screenPoint) const {
    const QVector4D clipPoint = buildProjectionMatrix() * buildViewMatrix() * QVector4D(worldPoint, 1.0f);
    if (clipPoint.w() <= 0.0f) {
        return false;
    }

    const QVector3D ndcPoint = clipPoint.toVector3DAffine();
    *screenPoint = QPointF(
        (ndcPoint.x() + 1.0f) * 0.5f * static_cast<float>(width()),
        (1.0f - ndcPoint.y()) * 0.5f * static_cast<float>(height()));
    return true;
}

}  // namespace renderer
