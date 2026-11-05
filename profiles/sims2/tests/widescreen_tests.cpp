#include "gpu/native_renderer.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
int main(){
    using namespace sims2::gpu;
    const auto require=[](bool ok,const char *why){if(!ok)throw std::runtime_error(why);};
    try{
        psprecomp::GuestMemory memory;NativeRenderer renderer(true);
        try{renderer.set_display_aspect(std::numeric_limits<float>::quiet_NaN());return 1;}catch(const std::exception&){}
        renderer.set_display_aspect(21.0f/9);
        DrawCall call{};call.through=true;call.has_vertex_color=true;call.primitive=PrimitiveType::Sprites;
        call.target={0x04000000,512,3,0x04180000,512};
        call.viewport.x_scale=240;call.viewport.y_scale=-136;call.viewport.x_offset=240;call.viewport.y_offset=136;
        call.vertices.resize(2);call.vertices[0].position={100,20,0,1};call.vertices[1].position={120,40,0,1};call.vertices[1].color=0xffffffff;
        renderer.draw(memory,call);renderer.flush(memory);
        const auto pixel=[&](unsigned x,unsigned y){return memory.load32(0x04000000+(y*512+x)*4);};
        require(pixel(140,30)==0xffffffff && pixel(110,30)==0,"HUD was not centered at its original proportions");
#if defined(SIMS2_VULKAN)
        require(renderer.gpu_presentation(memory,0x04000000,512,3)!=nullptr,"Corrected native-scale image not available for display");
#else
        require(renderer.presentation_view(memory,0x04000000,512,3)!=nullptr,"Corrected native-scale image not available for display");
#endif
        call.clear_mode=true;call.clear_flags=3;call.vertices[0].position={0,0,0,1};call.vertices[1].position={480,272,0,1};call.vertices[1].color=0xff000000;
        renderer.draw(memory,call);renderer.flush(memory);
        require(pixel(10,30)==0xff000000 && pixel(470,30)==0xff000000,"Clear did not cover the extended view");
        call.clear_mode=false;call.vertices[0].position={100,20,0,1};call.vertices[1].position={120,40,0,1};call.vertices[1].color=0xffffffff;
        call.viewport.scissor_x1=110;call.viewport.scissor_x2=114;
        renderer.draw(memory,call);renderer.flush(memory);
        require(pixel(143,30)==0xffffffff && pixel(138,30)==0xff000000,"HUD clipping did not follow its corrected position");
        call.viewport.scissor_x1=0;call.viewport.scissor_x2=479;
        call.through=false;call.primitive=PrimitiveType::Triangles;call.vertices.resize(3);
        call.world[0]=call.world[5]=call.world[10]=call.world[15]=1;call.view=call.world;
        call.projection[0]=call.projection[5]=1;call.projection[10]=-1;call.projection[11]=-1;
        call.viewport.z_scale=1;call.viewport.z_offset=1;
        call.vertices[0].position={1.05f,-.2f,-1,1};call.vertices[1].position={1.25f,-.2f,-1,1};call.vertices[2].position={1.05f,.2f,-1,1};
        for(auto &vertex:call.vertices)vertex.color=0xff0000ff;
        renderer.draw(memory,call);renderer.flush(memory);
        require(pixel(440,136)==0xff0000ff,"Expanded camera did not reveal geometry outside the original view");
        const auto projected=call;
        // Copying an already corrected frame must preserve its framing.
        call.through=true;call.primitive=PrimitiveType::Sprites;call.vertices.resize(2);call.target.color_address=0x04088000;
        call.texture.enabled=true;call.texture.address=0x04000000;call.texture.width=512;call.texture.height=256;call.texture.buffer_width=512;
        call.texture.format=TextureFormat::Rgba8888;call.texture.function=3;call.texture.alpha_from_texture=true;
        call.vertices[0].position={0,0,0,1};call.vertices[1].position={480,256,0,1};
        call.vertices[0].texcoord={0,0};call.vertices[1].texcoord={480,256};call.vertices[1].color=0xffffffff;
        renderer.draw(memory,call);renderer.flush(memory);
        require(memory.load32(0x04088000+(136*512+440)*4)==0xff0000ff,"Framebuffer copy applied camera correction twice");
        renderer.set_display_aspect(480.0f/272);
        call=projected;call.through=true;call.clear_mode=true;call.clear_flags=3;call.primitive=PrimitiveType::Sprites;call.vertices.resize(2);
        call.vertices[0].position={0,0,0,1};call.vertices[1].position={480,272,0,1};call.vertices[1].color=0xff000000;
        renderer.draw(memory,call);call=projected;renderer.draw(memory,call);renderer.flush(memory);
        require(pixel(440,136)==0xff000000,"Restoring native camera left expanded geometry visible");
        call.through=true;call.primitive=PrimitiveType::Sprites;call.vertices.resize(2);
        call.vertices[0].position={100,20,0,1};call.vertices[1].position={120,40,0,1};call.vertices[1].color=0xffffffff;
        renderer.draw(memory,call);renderer.flush(memory);
        require(pixel(110,30)==0xffffffff && pixel(140,30)==0xff000000,"Restoring native camera did not restore HUD placement");
        {
            psprecomp::GuestMemory narrow_memory;NativeRenderer narrow(true);narrow.set_display_aspect(4.0f/3);
            call.target.color_address=0x04000000;call.texture.enabled=false;call.vertices[0].position={100,20,0,1};call.vertices[1].position={120,40,0,1};
            narrow.draw(narrow_memory,call);narrow.flush(narrow_memory);
            require(narrow_memory.load32(0x04000000+(55*512+110)*4)==0xffffffff,"4:3 HUD did not fit vertically");
            require(narrow_memory.load32(0x04000000+(30*512+110)*4)==0,"4:3 HUD retained stretched proportions");
        }
        std::cout<<"Ultrawide camera, HUD layout/clipping, clears and framebuffer copies passed\n";return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
