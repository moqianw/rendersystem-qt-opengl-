#version 450 core

struct Material {
    sampler2D diffuseMap;
    vec3 tint;
};

struct Light {
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float ambientStrength;
    float intensity;
    float range;
    float innerConeCos;
    float outerConeCos;
};

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 fragColor;

uniform Material uMaterial;
uniform vec3 uViewPos;
uniform int uLightCount;
uniform Light uLights[16];

vec3 computeLightContribution(Light light, vec3 albedo, vec3 normal, vec3 viewDirection) {
    vec3 lightDirection = vec3(0.0);
    float attenuation = 1.0;
    float spotFactor = 1.0;

    if (light.type == 0) {
        lightDirection = normalize(-light.direction);
    } else {
        vec3 offset = light.position - vWorldPosition;
        float distanceToLight = length(offset);
        if (distanceToLight <= 1e-4) {
            return vec3(0.0);
        }

        lightDirection = offset / distanceToLight;
        float normalizedDistance = clamp(distanceToLight / max(light.range, 0.001), 0.0, 1.0);
        attenuation = pow(1.0 - normalizedDistance, 2.0);

        if (light.type == 2) {
            vec3 spotDirection = normalize(light.direction);
            float spotCos = dot(normalize(vWorldPosition - light.position), spotDirection);
            float coneRange = max(light.innerConeCos - light.outerConeCos, 0.0001);
            spotFactor = clamp((spotCos - light.outerConeCos) / coneRange, 0.0, 1.0);
        }
    }

    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    vec3 reflectDirection = reflect(-lightDirection, normal);
    float specularFactor = pow(max(dot(viewDirection, reflectDirection), 0.0), 32.0);
    float influence = attenuation * spotFactor;

    float ambientInfluence = light.type == 0 ? 1.0 : influence;
    vec3 ambient = light.ambientStrength * albedo * light.color * ambientInfluence;
    vec3 diffuse = diffuseFactor * light.intensity * albedo * light.color * influence;
    vec3 specular = specularFactor * 0.35 * light.color * influence;
    return ambient + diffuse + specular;
}

void main() {
    vec3 albedo = texture(uMaterial.diffuseMap, vTexCoord).rgb * uMaterial.tint;
    vec3 normal = normalize(vNormal);
    vec3 viewDirection = normalize(uViewPos - vWorldPosition);
    vec3 lighting = vec3(0.0);
    for (int index = 0; index < uLightCount; ++index) {
        lighting += computeLightContribution(uLights[index], albedo, normal, viewDirection);
    }

    fragColor = vec4(lighting, 1.0);
}
