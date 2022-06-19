#version 310 es

precision mediump float;

uniform sampler2D cameraTexture;

layout (location = 0) in vec2 texCoords;
layout (location = 1) out vec4 BrightColor;



void main()
{
    // check whether fragment output is higher than threshold, if so output as brightness color
    vec3 color = texture(cameraTexture, texCoords).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    if(brightness > 1.0)
    BrightColor = vec4(color, 1.0);
    else
    BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
}