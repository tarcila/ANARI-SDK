{
  description = ''
    ANARI-SDK development environment
  '';

  nixConfig = {
    extra-substituters = [
      "https://dldt.cachix.org/"
    ];
    extra-trusted-public-keys = [
      "dldt.cachix.org-1:lF3I8Yijsqk+5+ZjH3QCLYrPvKadXpL41fsdIpM5Rss="
    ];
  };

  inputs = {
    anari-nix.url = "github:dldt/anari-nix";
    nixpkgs.follows = "anari-nix/nixpkgs";
    flake-utils = {
      url = "github:numtide/flake-utils";
      inputs.systems.follows = "systems";
    };
    systems.url = "github:nix-systems/default";
    treefmt-nix = {
      url = "github:numtide/treefmt-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      anari-nix,
      flake-utils,
      systems,
      treefmt-nix,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        inherit (nixpkgs) lib;

        pkgs = import nixpkgs {
          inherit system;
          overlays = [ anari-nix.overlays.default ];
        };

        treefmtEval = treefmt-nix.lib.evalModule pkgs ./treefmt.nix;
      in
      {
        devShells.default = pkgs.mkShell {
          hardeningDisable = [ "all" ];

          packages = with pkgs; [
            cmakeCurses
            jq
          ];

          buildInputs = [
            pkgs.draco
            pkgs.glslang
            pkgs.libjpeg
            pkgs.snappy
            pkgs.boost
          ];

          nativeBuildInputs = with pkgs; [
            cmake
            gdb
            lldb
            ninja
            valgrind
            watchexec
            python3
          ];

          inputsFrom = [ pkgs.anari-sdk ];

          STOCKADE_SESSION = "anari-sdk";

          shellHook = ''
            if test -n "''${VSCODE_IPC_HOOK_CLI}"
            then
              export DISPLAY=:10
            fi
            export CMAKE_TLS_VERIFY=0
          '';
        };

        formatter = treefmtEval.config.build.wrapper;
      }
    );
}
