mod cmake '~/_just/cmake'
mod dotfiles '~/_just/dotfiles'
mod repo '~/_just/repo'

import '~/_just/dirs/mod.just'

cfg := env("cfg", "RelWithDebInfo")
_source_dir := _ROOT_DIR
_root_dir := _ROOT_DIR
_build_dir := _root_dir / "_out/_cmake"
_install_dir := _root_dir / "_out"
_envrc_dir := _root_dir
_clangd_dir := _root_dir
_compile_commands_dir := _build_dir
_vscode_dir := _root_dir / ".vscode"
_emacs_desktop_dir := _root_dir / ".emacs.d"
_emacs_dape_dir := _root_dir

_compiler := if os() == "macos" { "clang++" } else { "g++" }

_default: build

# Some important aliases

[doc("Build the project (Release)")]
[metadata("task", "pm=$gcc")]
br: (build "Release")

[doc("Build the project (RelWithDebInfo)")]
[metadata("task", "pm=$gcc")]
brd: (build "RelWithDebInfo")

[doc("Build the project (Debug)")]
[metadata("task", "pm=$gcc")]
bd: (build "Debug")

# Repo recipes

alias new_workspace := repo::new_workspace
alias forget_workspace := repo::forget_workspace

# Build recipes

[doc("Generate configure files")]
prepare: generate_envrc generate_clangd generate_vscode_tasks_json generate_vscode_launch_json generate_emacs_desktop generate_emacs_dape
    direnv allow

[private]
_cmake_options := replace_regex('
    -DBUILD_VIEWER=ON
    -DNFD_PORTAL=ON
    -DVIEWER_ENABLE_GLTF=ON
    -DUSE_DRACO=ON
    -DUSE_KTX=OFF
    -DUSE_WEBP=ON
', '\s*\n\s*', ' ')

[doc("Configure")]
configure: (cmake::force_configure _source_dir _build_dir _install_dir _cmake_options)

[doc("Reconfigure")]
reconfigure: (cmake::configure _source_dir _build_dir _install_dir _cmake_options)

[doc("Build")]
[metadata("task", "pm=$gcc")]
build cfg=cfg: reconfigure (cmake::install cfg _build_dir)
    mkdir -p "{{ _install_dir }}/bin"
    cp -u "{{ _build_dir }}/{{ cfg }}/anariViewer" "{{ _install_dir }}/bin" 2>/dev/null || true

[doc("Rebuild")]
[metadata("task", "pm=$gcc")]
rebuild cfg=cfg: clean (build cfg)

# Run recipes

[doc("Run a command")]
[private]
[script("bash", "-euo", "pipefail")]
run COMMAND *ARGS:
    exec {{ COMMAND }} {{ ARGS }}

[doc("Run anariViewer")]
[metadata("launch", "debug")]
view *ARGS: (run "anariViewer" ARGS)

# Auxiliary recipes

[doc("Generate .envrc file")]
generate_envrc: (dotfiles::generate_envrc _envrc_dir _install_dir)

[doc("Generate .clangd config file")]
generate_clangd: (dotfiles::generate_clangd _clangd_dir _compile_commands_dir _compiler)

[doc("Generate .vscode/tasks.json")]
generate_vscode_tasks_json: (dotfiles::generate_vscode_tasks_json _vscode_dir)

[doc("Generate .vscode/launch.json")]
generate_vscode_launch_json: (dotfiles::generate_vscode_launch_json _vscode_dir)

[doc("Generate .emacs.desktop file")]
generate_emacs_desktop: (dotfiles::generate_emacs_desktop _emacs_desktop_dir)

[doc("Generate .dape.el file")]
generate_emacs_dape: (dotfiles::generate_emacs_dape _emacs_dape_dir)

[doc("Remove the installation dir")]
clean:
    rm -fr "{{ _install_dir }}"
