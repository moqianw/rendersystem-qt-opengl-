#version 450 core

struct Material {
    sampler2D diffuseMap;
    vec3 tint;
};

struct PointLight {
    vec3 position;
    vec3 color;
    float ambientStrength;
    float intensity;
};

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 fragColor;

uniform Material uMaterial;
uniform PointLight uLight;
uniform vec3 uViewPos;

void main() {
    vec3 albedo = texture(uMaterial.diffuseMap, vTexCoord).rgb * uMaterial.tint;
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(uLight.position - vWorldPosition);
    vec3 viewDirection = normalize(uViewPos - vWorldPosition);
    vec3 reflectDirection = reflect(-lightDirection, normal);

    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float specularFactor = pow(max(dot(viewDirection, reflectDirection), 0.0), 32.0);

    vec3 ambient = uLight.ambientStrength * albedo * uLight.color;
    vec3 diffuse = diffuseFactor * uLight.intensity * albedo * uLight.color;
    vec3 specular = specularFactor * 0.35 * uLight.color;

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
