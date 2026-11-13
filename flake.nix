{
  description = "Metal Gear Solid Portable Ops and Portable Ops+ as native programs, by static recompilation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forEachSystem = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forEachSystem (pkgs: {
        default = pkgs.mkShell {
          name = "portable-ops-native";
          packages = with pkgs; [
            cmake
            ninja
            ccache
            pkg-config
            python3
            git
            sdl3
            vulkan-headers
            vulkan-loader
            vulkan-validation-layers
            glslang
            ffmpeg
          ];
          # PortableKit builds FFmpeg itself by default; use the system one.
          PORTABLEKIT_FFMPEG = "system";
          # One edition's objects are about 2 GiB; ccache's 5 GiB default
          # evicted them between builds.
          CCACHE_MAXSIZE = "15G";
        };
      });

      formatter = forEachSystem (pkgs: pkgs.nixfmt);
    };
}
