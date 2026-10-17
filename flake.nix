{
  description = "Metal Gear Solid Portable Ops and Portable Ops+ as native programs, by static recompilation";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    # The reverse-engineering workspace: decrypted-module tools, function
    # boundaries, NID data and the PPSSPP boot comparison. Local until it is
    # published.
    poplusplus.url = "git+file:///path/to/local-checkout";
  };

  outputs =
    { nixpkgs, poplusplus, ... }:
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
          POPLUSPLUS = "${poplusplus}";
        };
      });

      formatter = forEachSystem (pkgs: pkgs.nixfmt);
    };
}
