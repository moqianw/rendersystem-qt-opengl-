#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace renderer::rendermath {

inline glm::vec3 toGlm(const QVector3D& value) {
    return glm::vec3(value.x(), value.y(), value.z());
}

inline QVector3D toQt(const glm::vec3& value) {
    return QVector3D(value.x, value.y, value.z);
}

inline glm::mat4 toGlm(const QMatrix4x4& value) {
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
        const QVector4D columnVector = value.column(column);
        matrix[column][0] = columnVector.x();
        matrix[column][1] = columnVector.y();
        matrix[column][2] = columnVector.z();
        matrix[column][3] = columnVector.w();
    }
    return matrix;
}

inline QMatrix4x4 toQt(const glm::mat4& value) {
    QMatrix4x4 matrix;
    matrix.setColumn(0, QVector4D(value[0][0], value[0][1], value[0][2], value[0][3]));
    matrix.setColumn(1, QVector4D(value[1][0], value[1][1], value[1][2], value[1][3]));
    matrix.setColumn(2, QVector4D(value[2][0], value[2][1], value[2][2], value[2][3]));
    matrix.setColumn(3, QVector4D(value[3][0], value[3][1], value[3][2], value[3][3]));
    return matrix;
}

}  // namespace renderer::rendermath
