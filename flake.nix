{
  description = "Prologres - A Prolog PostgreSQL library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    devenv = {
      url = "github:cachix/devenv";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, devenv }@inputs:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
        {
          devShells.default = devenv.lib.mkShell {
            inherit inputs pkgs;
            modules = [
              {
                packages = with pkgs; [
                  swi-prolog
                  postgresql_18
                  clang-tools
                ];

                languages.c = {
                  enable = true;
                };

                enterShell = ''
                  # Generate .clangd configuration for LSP
                  cat > .clangd << EOF
                  CompileFlags:
                    Add:
                      - -I${pkgs.swi-prolog}/lib/swipl/include
                      - -I${pkgs.postgresql_18.dev}/include
                      - -I${pkgs.postgresql_18.dev}/include/server
                  EOF
                '';

                services.postgres = {
                  enable = true;
                  package = pkgs.postgresql_18;

                  initdbArgs = [
                    "--locale=C"
                    "--encoding=UTF8"
                  ];

                  extensions = ext: [
                    ext.periods
                  ];

                  initialDatabases = [
                    {
                      name = "prolog_store";
                    }
                  ];

                  port = 5432;
                  listen_addresses = "127.0.0.1";

                  settings = { };

                  initialScript = ''
                      CREATE USER admin WITH PASSWORD 'admin' SUPERUSER;
                      ALTER DATABASE prolog_store OWNER TO admin;
                    '';
                };
              }
            ];
          };

          packages.devenv-up = self.devShells.${system}.default.config.procfileScript;

        packages.default = pkgs.stdenv.mkDerivation {
          name = "prologres";
          src = ./.;

          buildInputs = with pkgs; [
            swi-prolog
          ];
          
          buildPhase = ''
            mkdir -p $out/lib
            cp $src/lib/prolog.c $out/lib/prolog.c
            swipl-ld -shared -o $out/lib/prologlib.so $out/lib/prolog.c -lpq -I${pkgs.postgresql_18.dev.outPath}/include -L${pkgs.postgresql_18.lib.outPath}/lib
          '';

          installPhase = ''
            mkdir -p $out/src
            cp $src/src/main.pro $out/src/main.pro
          '';
        };
        
        apps.default = {
          type = "app";
          program = "${pkgs.writeShellScript "prologres-run" ''
            ${pkgs.swi-prolog}/bin/swipl -p foreign=${self.packages.${system}.default}/lib/. ${self.packages.${system}.default}/src/main.pro "$@"
          ''}";
        };
      }
    );
}
