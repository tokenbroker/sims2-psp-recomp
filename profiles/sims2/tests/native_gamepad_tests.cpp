#include "ctrl/native_gamepad.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        using namespace sims2::ctrl;
        const auto require=[](bool ok,const char *why) { if(!ok) throw std::runtime_error(why); };
        auto pad=NativeGamepad::map((1u<<SDL_GAMEPAD_BUTTON_SOUTH)|(1u<<SDL_GAMEPAD_BUTTON_DPAD_UP)|(1u<<SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),0,0);
        require(pad.buttons==(kButtonCross|kButtonUp|kButtonR),"Physical button mapping");
        pad=NativeGamepad::map(0,3000,-3000); require(pad.lx==128 && pad.ly==128,"Stick deadzone");
        pad=NativeGamepad::map(0,-32768,0); require(pad.lx<=1 && pad.ly==128,"Left endpoint");
        pad=NativeGamepad::map(0,0,32767); require(pad.lx==128 && pad.ly==255,"Down endpoint");
        NativeGamepad input;
        SDL_VirtualJoystickDesc desc; SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes=SDL_GAMEPAD_AXIS_COUNT; desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1; desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        desc.name="Sims 2 virtual gamepad test";
        const auto id=SDL_AttachVirtualJoystick(&desc); require(id!=0,SDL_GetError());
        auto *joystick=SDL_OpenJoystick(id); require(joystick!=nullptr,SDL_GetError());
        SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true);
        SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,32767);
        pad=input.sample(true);
        require((pad.buttons&kButtonCross)!=0 && pad.lx==255,"Virtual hot-plug and input");
        InputConfig remap;remap.gamepad[Circle]=SDL_GAMEPAD_BUTTON_SOUTH;remap.gamepad[Cross]=SDL_GAMEPAD_BUTTON_EAST;
        input.set_input_options(remap);pad=input.sample(true);require((pad.buttons&kButtonCircle)&&!(pad.buttons&kButtonCross),"Controller rebind ignored");
        input.set_input_options(InputConfig{});
        input.set_options(40,true);SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHTX,32767);
        pad=input.sample(true);require((pad.buttons&kButtonR)!=0,"Right stick camera mapping");
        input.set_options(40,false);pad=input.sample(true);require((pad.buttons&kButtonR)==0,"Right stick camera setting ignored");
        pad=NativeGamepad::map(0,8000,0,40);require(pad.lx==128,"Custom deadzone ignored");
        pad=NativeGamepad::map(0,8000,0,5);require(pad.lx>128,"Small deadzone suppressed intended input");
        pad=input.sample(false); require(pad.buttons==0 && pad.lx==128,"Focus release");
        SDL_CloseJoystick(joystick); SDL_DetachVirtualJoystick(id);
        pad=input.sample(true); require(pad.buttons==0 && pad.lx==128,"Disconnect release");
        return 0;
    } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
