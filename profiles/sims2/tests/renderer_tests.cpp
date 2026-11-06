#include "gpu/native_renderer.hpp"
#include "ge/ge.hpp"
#include <algorithm>
#include <iostream>
#if defined(SIMS2_VULKAN)
#include <SDL3/SDL.h>
#endif
#if !defined(SIMS2_VULKAN)
#include <d3d11.h>
#include <wrl/client.h>
#endif
#include <stdexcept>
int main(int argc,char **argv) {
    const unsigned scale=argc>1?unsigned(std::stoul(argv[1])):1;
    try {
        using namespace sims2::gpu;
        const auto require = [](bool ok, const char *why) { if (!ok) throw std::runtime_error(why); };
        psprecomp::GuestMemory memory;
        NativeRenderer renderer(true); renderer.set_render_scale(scale);
        DrawCall call{}; call.through = true; call.has_vertex_color = true;
        call.primitive = PrimitiveType::Sprites; call.target = {0x04000000u, 512u, 3u, 0x04180000u, 512u};
        call.vertices.resize(2); call.vertices[0].position = {10,20,100,1}; call.vertices[1].position = {30,40,100,1};
        call.vertices[1].color = 0xFF0000FFu;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(0x04000000u + (25u * 512u + 15u) * 4u) == 0xFF0000FFu, "Sprite pixels do not match");
        require(memory.load32(0x04000000u + (25u * 512u + 35u) * 4u) == 0u, "Sprite overwrote outside its rectangle");
        call.alpha_test = {true, 6u, 200u, 255u}; call.vertices[1].color = 0x4000FF00u;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(0x04000000u + (25u * 512u + 15u) * 4u) == 0xFF0000FFu, "Alpha rejection failed");
        call.alpha_test.enabled = false;
        call.texture.enabled = true; call.texture.address = 0x09000000u; call.texture.width = call.texture.height = 2;
        call.texture.buffer_width = 2; call.texture.format = TextureFormat::Rgba8888; call.texture.function = 3; call.texture.alpha_from_texture = true;
        for (unsigned i = 0; i < 4; ++i) memory.store32(0x09000000u + i * 4u, 0xFF00FF00u);
        call.vertices[0].texcoord = {0,0}; call.vertices[1].texcoord = {2,2};
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(0x04000000u + (25u * 512u + 15u) * 4u) == 0xFF00FF00u, "Texture sampling failed");
        call.texture.enabled = false; call.vertices[1].color = 0xFF0000FFu;
        call.blend.enabled = true; call.blend.source_factor = 0; call.blend.destination_factor = 10; call.blend.fixed_destination = 0;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(0x04000000u + (25u * 512u + 15u) * 4u) == 0xFF000000u, "Source blend did not use destination color");
        call.blend.enabled = false;
        // Shadows and light cones must blend using their alpha, rather than
        // covering the destination with an opaque black polygon.
        call.vertices[1].color=0xffffffffu;
        renderer.draw(memory,call);
        call.vertices[1].color=0x40000000u;
        call.blend.enabled=true;call.blend.source_factor=2;call.blend.destination_factor=3;
        renderer.draw(memory,call);renderer.flush(memory);
        auto shadow=memory.load32(0x04000000u+(25u*512u+15u)*4u);
        require((shadow&0x00ffffffu)==0x00bfbfbfu,"Translucent shadow became opaque");
#if defined(SIMS2_VULKAN)
        for(float presentation_scale : (scale==1?std::vector<float>{1.f,1.25f}:std::vector<float>{float(scale)})){
            if(renderer.render_scale()!=presentation_scale){
                renderer.set_render_scale(presentation_scale);
                call.blend.enabled=false;call.vertices[1].color=0xffffffffu;renderer.draw(memory,call);
                call.blend.enabled=true;call.vertices[1].color=0x40000000u;renderer.draw(memory,call);renderer.flush(memory);
            }
            auto *device=static_cast<SDL_GPUDevice*>(renderer.gpu_device());
            auto *image=static_cast<SDL_GPUTexture*>(renderer.gpu_presentation(memory,0x04000000u,512,3));
            require(image!=nullptr,"Missing shadow presentation image");
            const auto size=renderer.gpu_dimensions();
            SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=size[0]*size[1]*4;
            auto *readback=SDL_CreateGPUTransferBuffer(device,&info);require(readback!=nullptr,"Create presentation readback");
            auto *commands=SDL_AcquireGPUCommandBuffer(device);auto *copy=SDL_BeginGPUCopyPass(commands);
            SDL_GPUTextureRegion source{};source.texture=image;source.w=size[0];source.h=size[1];source.d=1;
            SDL_GPUTextureTransferInfo destination{};destination.transfer_buffer=readback;destination.pixels_per_row=size[0];destination.rows_per_layer=size[1];
            SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
            auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            require(fence && SDL_WaitForGPUFences(device,true,&fence,1),"Wait presentation readback");
            SDL_ReleaseGPUFence(device,fence);
            auto *pixels=static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(device,readback,false));
            require(pixels!=nullptr,"Map presentation readback");const auto presented=pixels[unsigned(25*presentation_scale)*size[0]+unsigned(15*presentation_scale)];
            if(presentation_scale==1)for(unsigned i=0;i<512u*272u;++i)
                require((pixels[i]&0x00ffffffu)==(memory.load32(0x04000000u+i*4u)&0x00ffffffu),
                        "Opaque display copy changed RGB or image orientation");
            SDL_UnmapGPUTransferBuffer(device,readback);SDL_ReleaseGPUTransferBuffer(device,readback);
            require(presented==0xffbfbfbfu,"Game framebuffer alpha leaked into presentation opacity");
            require(memory.load32(0x04000000u+(25u*512u+15u)*4u)==shadow,
                    "Opaque presentation modified PSP framebuffer alpha");
        }
        renderer.set_render_scale(scale);
#endif
        call.vertices[1].color=0xffffffffu;
        call.blend.source_factor=4;call.blend.destination_factor=10;call.blend.fixed_destination=0;
        renderer.draw(memory,call);renderer.flush(memory);
        require((memory.load32(0x04000000u+(25u*512u+15u)*4u)&0x00ffffffu)==0x00404040u,
                "Display copy changed game destination-alpha blending");
        call.blend.enabled=false;
        call.primitive = PrimitiveType::Triangles; call.through = false; call.vertices.resize(3);
        for (unsigned i = 0; i < 4; ++i) call.world[i * 5] = call.view[i * 5] = call.projection[i * 5] = 1;
        call.viewport.x_scale = 256; call.viewport.y_scale = -136; call.viewport.z_scale = 32767.5f;
        call.viewport.x_offset = 256; call.viewport.y_offset = 136; call.viewport.z_offset = 32767.5f;
        call.vertices[0].position = {-0.5f,-0.5f,0,1}; call.vertices[1].position = {0.5f,-0.5f,0,1}; call.vertices[2].position = {0,0.5f,0,1};
        for (auto &vertex : call.vertices) vertex.color = 0xFFFF0000u;
        call.depth.test_enabled = true; call.depth.function = 6;
        renderer.draw(memory, call); renderer.flush(memory);
        const auto center = 0x04000000u + (136u * 512u + 256u) * 4u;
        require(memory.load32(center) == 0xFFFF0000u, "Projected triangle did not rasterize");
        for (auto &vertex : call.vertices) { vertex.color = 0xFF0000FFu; vertex.position[2] = -0.5f; }
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(center) == 0xFFFF0000u, "Depth test failed to reject farther triangle");
        call.depth.test_enabled = false; call.lighting_enabled = true; call.material_color = 0xFFFFFFFFu;
        call.lighting.ambient_color = 0x000000FFu; call.lighting.ambient_alpha = 255;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(center) == 0xFF0000FFu, "Material/global ambient lighting failed");
        // Selected Sims menu buttons choose another atlas row with a texture
        // matrix. Verify rendered pixels change, including projective division.
        call.lighting_enabled = false; call.texture.enabled = true;
        call.texture.address = 0x09001000u; call.texture.width = call.texture.height = 2;
        call.texture.buffer_width = 2; call.texture.mapping_mode = 1; call.texture.projection_source = 1;
        for (unsigned i = 0; i < 4; ++i)
            memory.store32(call.texture.address + i * 4, i < 2 ? 0xFF0000FFu : 0xFF00FF00u);
        call.texture_matrix = {};
        call.texture_matrix[12] = 0.25f; call.texture_matrix[13] = 0.75f; call.texture_matrix[14] = 1;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(center) == 0xFF00FF00u, "Texture matrix did not select highlighted atlas row");
        call.texture_matrix[14] = 2;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(center) == 0xFF0000FFu, "Texture projection did not divide by Q");
        call.texture.enabled = false; call.lighting_enabled = true;
        call.texture.mapping_mode = 0;
        // Sample GPU output before an explicit flush: feedback must become
        // visible to the texture decoder and then to another render target.
        renderer.draw(memory, call);
        call.through = true; call.lighting_enabled = false; call.primitive = PrimitiveType::Sprites; call.vertices.resize(2);
        call.vertices[0].position = {240,120,0,1}; call.vertices[1].position = {272,152,0,1};
        call.vertices[0].texcoord = {240,120}; call.vertices[1].texcoord = {272,152};
        call.texture.enabled = true; call.texture.address = 0x04000000u; call.texture.width = 512; call.texture.height = 256; call.texture.buffer_width = 512;
        call.target.color_address = 0x04088000u;
        renderer.draw(memory, call); renderer.flush(memory);
        require(memory.load32(0x04088000u + (136u * 512u + 256u) * 4u) == 0xFF0000FFu, "Render-to-texture feedback lost GPU pixels");
        // Unrelated GE copies must not stall the GPU. Aliased framebuffer
        // sources and partial destination writes still need coherent pixels.
        NativeRenderer transfers(true); transfers.set_render_scale(scale);
        call.texture.enabled = false; call.target.color_address = 0x04100000u;
        call.vertices[0].position = {10,20,0,1}; call.vertices[1].position = {30,40,0,1};
        call.vertices[1].color = 0xFF0000FFu;
        transfers.draw(memory, call);
        BlockTransfer transfer{0x09000000u, 512, 0, 0, 0x09100000u, 512, 0, 0, 1, 1, 4};
        transfers.prepare_transfer(memory, transfer);
        require(transfers.framebuffer_readbacks() == 0, "Unrelated transfer flushed a framebuffer");
        transfer.source = 0x44100000u; transfer.source_x = 15; transfer.source_y = 25;
        transfers.prepare_transfer(memory, transfer);
        require(transfers.framebuffer_readbacks() == 1, "Aliased framebuffer source was not resolved");
        require(sims2::ge::Ge::copy_transfer(memory, transfer), "Framebuffer source transfer failed");
        require(memory.load32(0x09100000u) == 0xFF0000FFu, "Transfer read stale framebuffer pixels");
        transfers.draw(memory, call);
        memory.store32(0x09000000u, 0xFF00FF00u);
        transfer = {0x09000000u, 512, 0, 0, 0x44100000u, 512, 15, 25, 1, 1, 4};
        transfers.prepare_transfer(memory, transfer);
        require(sims2::ge::Ge::copy_transfer(memory, transfer), "Framebuffer destination transfer failed");
        call.vertices[0].position = {40,20,0,1}; call.vertices[1].position = {50,40,0,1};
        transfers.draw(memory, call); transfers.flush(memory);
        require(memory.load32(0x04100000u + (25u * 512u + 15u) * 4u) == 0xFF00FF00u, "GPU discarded transferred destination pixels");
        require(memory.load32(0x04100000u + (25u * 512u + 16u) * 4u) == 0xFF0000FFu, "Partial transfer lost neighboring GPU pixels");
        // Queue more than one streaming-buffer allocation without readbacks.
        // Every row must retain its own vertices across append and wraparound.
        NativeRenderer streaming(true); streaming.set_render_scale(scale);
        DrawCall rows{};
        rows.through = true; rows.primitive = PrimitiveType::Sprites;
        rows.target = call.target;
        rows.viewport = call.viewport;
        rows.has_vertex_color = true;
        rows.vertices.resize(512);
        for (unsigned y = 0; y < 64; ++y) {
            for (unsigned x = 0; x < 256; ++x) {
                rows.vertices[x * 2].position = {float(x), float(y), 0, 1};
                rows.vertices[x * 2 + 1].position = {float(x + 1), float(y + 1), 0, 1};
                rows.vertices[x * 2 + 1].color = 0xFF000000u | (y + 1u);
            }
            streaming.draw(memory, rows);
        }
        streaming.flush(memory);
        for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 256; ++x)
            require(memory.load32(rows.target.color_address + (y * 512u + x) * 4u) == (0xFF000000u | (y + 1u)),
                    "Streaming vertex upload overwrote an earlier draw");
#if defined(SIMS2_VULKAN)
        if(scale==1){
            // More than 32 MiB of texture uploads in one render sequence forces
            // staging rollover. Each earlier draw must retain its own texture
            // data after the same guest texture address is overwritten.
            NativeRenderer arena(true);
            DrawCall textured{};textured.through=true;textured.primitive=PrimitiveType::Sprites;
            textured.target=rows.target;textured.viewport=rows.viewport;
            textured.texture.enabled=true;textured.texture.address=0x09000000u;
            textured.texture.width=textured.texture.height=textured.texture.buffer_width=512;
            textured.texture.format=TextureFormat::Rgba8888;textured.texture.function=3;
            textured.texture.alpha_from_texture=true;
            textured.vertices.resize(2);
            textured.vertices[0].texcoord={0,0};textured.vertices[1].texcoord={1,1};
            std::vector<std::uint32_t> pixels(512u*512u);
            for(unsigned y=0;y<40;++y){
                const auto color=0xff000000u|(y+1u);
                std::fill(pixels.begin(),pixels.end(),color);
                memory.copy_in(textured.texture.address,{reinterpret_cast<const std::uint8_t*>(pixels.data()),pixels.size()*4u});
                textured.vertices[0].position={300.f,float(y),0,1};
                textured.vertices[1].position={301.f,float(y+1),0,1};
                arena.draw(memory,textured);
            }
            arena.flush(memory);
            for(unsigned y=0;y<40;++y)
                require(memory.load32(textured.target.color_address+(y*512u+300u)*4u)==(0xff000000u|(y+1u)),
                        "Upload arena rollover corrupted an earlier texture");
        }
#endif
        if(scale>1){
            psprecomp::GuestMemory clean;
            NativeRenderer quality(true);quality.set_render_scale(scale);
            DrawCall triangle{};triangle.through=true;triangle.has_vertex_color=true;
            triangle.primitive=PrimitiveType::Triangles;triangle.target={0x04000000u,512,3,0x04180000u,512};
            triangle.vertices.resize(3);
            triangle.vertices[0].position={10.375f,10.375f,0,1};triangle.vertices[1].position={30.375f,10.375f,0,1};triangle.vertices[2].position={10.375f,30.375f,0,1};
            for(auto &v:triangle.vertices)v.color=0xff0000ff;
            quality.draw(clean,triangle);quality.flush(clean);
#if defined(SIMS2_VULKAN)
            require(quality.gpu_presentation(clean,0x04000000,512,3)!=nullptr,"Scaled framebuffer missing");
            const auto size=quality.gpu_dimensions();require(size[0]==512*scale&&size[1]==272*scale,"Internal resolution did not scale");
            clean.store32(0x04000000,0xff00ff00);
            require(!quality.gpu_presentation(clean,0x04000000,512,3),"CPU modified framebuffer used stale image");
#else
            auto *view=quality.presentation_view(clean,0x04000000,512,3);
            require(view!=nullptr,"Scaled framebuffer missing from presentation");
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;resource.As(&texture);
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            require(desc.Width==512*scale && desc.Height==272*scale,"Internal resolution did not scale");
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            require(SUCCEEDED(quality.device()->CreateTexture2D(&desc,nullptr,&staging)),"Cannot read scaled pixels");
            quality.context()->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(quality.context()->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Cannot map scaled pixels");
            unsigned red{};
            for(unsigned y=10*scale;y<11*scale;++y){
                const auto *row=reinterpret_cast<const std::uint32_t *>(static_cast<const char *>(mapped.pData)+y*mapped.RowPitch);
                for(unsigned x=10*scale;x<11*scale;++x)red+=row[x]==0xff0000ff;
            }
            quality.context()->Unmap(staging.Get(),0);
            require(red>0 && red<scale*scale,"Scaled geometry has no detail within native pixels");
            clean.store32(0x04000000,0xff00ff00);
            require(!quality.presentation_view(clean,0x04000000,512,3),"CPU-modified framebuffer incorrectly used stale GPU image");
#endif
            quality.set_render_scale(1);require(quality.render_scale()==1,"Switching back to native failed");
        }
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}

