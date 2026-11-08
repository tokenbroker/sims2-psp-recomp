# Source provenance

The recompiler/framework is based on [jessicanataliagta/PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp)
at commit `f6e7d415c7f447b934cc3865a31eb725f353d659`, with local scratchpad,
VFPU/color, code-generation and runtime changes used by the Sims 2 PSP profile.
Its MIT license and copyright notice are retained in the root LICENSE.

The game host includes independently implemented PSP HLE and a Direct3D 11
renderer, plus attributed MIT work from Bigmission Recomp and Yakumo. ImGui and
tiny-AES-c sources/notices are retained under the profile's third_party folder.
SDL3 and FFmpeg development files are fetched separately from pinned public
sources and are not committed here. See THIRD_PARTY.md for sources and notices.

The abilliontokens Sims repositories supplied F1 feature/layout references.
Their Xbox game-specific patches, generated code and HUD implementations are
not incorporated. The God of War PSP project was an architectural reference;
its GPL implementation source is not incorporated.

This public repository starts from a reviewed source-only export. It intentionally
has no imported local development history, game executable, generated AOT corpus,
reverse-engineering output, save data, logs or playable binaries. Public build
instructions regenerate game-derived files locally from a supported user ISO.

Game-title identifiers, executable hashes, PSP addresses and public API/NID values
are compatibility metadata. They are not account credentials or redistributed
game assets. Third-party authorship and copyright notices are preserved.

The portable Linux/Windows Vulkan host uses SDL GPU and the matching upstream
ImGui SDL3/SDL GPU backends. Its PSP vertex/lighting/texture behavior follows the
existing MIT host implementation. GLSL shaders are independent host code. Linux
uses distribution FreeType/fontconfig for substitute fonts. No Xbox GPU source
was copied from the reference ports.

## Prebuilt Windows release artifacts

From v0.3.0, Windows release ZIPs intentionally include compiled native game
code built from the supported local ISO, alongside the host runtime and attributed
dependencies. Generated sources and original game files remain excluded from
the repository and release payload. Users provide their own matching ISO for
validation, executable preparation and asset access. This does not change the
source-tree publication guard or license third-party game content under MIT.
