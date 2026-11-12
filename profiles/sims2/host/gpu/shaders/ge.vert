#version 450
layout(location=0) in vec4 position;
layout(location=1) in vec3 texcoord;
layout(location=2) in vec4 color;
layout(location=3) in vec4 extra;
layout(location=0) out vec3 uv;
layout(location=1) out vec4 shade;
layout(location=2) out vec4 secondary;
void main(){gl_Position=position;gl_PointSize=1.0;uv=texcoord;shade=color;secondary=extra;}
