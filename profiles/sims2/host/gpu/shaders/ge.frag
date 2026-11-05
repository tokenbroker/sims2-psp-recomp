#version 450
layout(location=0) in vec3 uv;
layout(location=1) in vec4 shade;
layout(location=2) in vec4 secondary;
layout(location=0) out vec4 output_color;
layout(set=2,binding=0) uniform sampler2D image;
layout(set=3,binding=0,std140) uniform Params {
 uint textured; uint function_; uint texture_alpha; uint alpha_enabled;
 uint alpha_function; uint alpha_reference; uint alpha_mask; uint double_color;
 vec4 environment; vec4 fog;
};
void main(){
 vec4 c=shade;
 if(textured!=0){
  vec2 st=uv.z>0?uv.xy/uv.z:1.0-0.5/vec2(textureSize(image,0));
  vec4 t=texture(image,st);float a=texture_alpha!=0?c.a*t.a:c.a;
  if(function_==0)c=vec4(c.rgb*t.rgb,a);
  else if(function_==1)c=vec4(texture_alpha!=0?mix(c.rgb,t.rgb,t.a):t.rgb,c.a);
  else if(function_==2)c=vec4(mix(c.rgb,environment.rgb,t.rgb),a);
  else if(function_==3)c=vec4(t.rgb,texture_alpha!=0?t.a:c.a);
  else c=vec4(c.rgb+t.rgb,a);
  if(double_color!=0)c.rgb*=2;
 }
 c.rgb=clamp(c.rgb+secondary.rgb,0,1);
 c.rgb=mix(fog.rgb,c.rgb,clamp(secondary.w,0,1));
 if(alpha_enabled!=0){
  uint a=uint(round(clamp(c.a,0,1)*255))&alpha_mask,r=alpha_reference&alpha_mask;
  bool ok=alpha_function==1||(alpha_function==2&&a==r)||(alpha_function==3&&a!=r)||
   (alpha_function==4&&a<r)||(alpha_function==5&&a<=r)||(alpha_function==6&&a>r)||(alpha_function==7&&a>=r);
  if(!ok)discard;
 }
 output_color=clamp(c,0,1);
}
