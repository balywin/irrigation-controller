Import('env')
import subprocess, os, sys, shutil

project_dir = env.subst('$PROJECT_DIR')
ui_dir = os.path.join(project_dir, 'ui')
ui_dist_dir = os.path.join(project_dir, 'ui', 'dist')
output_file = os.path.join(project_dir, 'include', 'embedded_files.h')
script      = os.path.join(project_dir, 'tools', 'embed_static.js')

tool_env = os.environ.copy()
tool_path = os.pathsep.join(['/opt/homebrew/bin', '/usr/local/bin', tool_env.get('PATH', '')])
tool_env['PATH'] = tool_path
npm_cmd = shutil.which('npm', path=tool_path)
node_cmd = shutil.which('node', path=tool_path)

if not npm_cmd:
    print('ERROR: npm not found. Install Node.js/npm or add npm to PATH.', file=sys.stderr)
    env.Exit(1)
if not node_cmd:
    print('ERROR: node not found. Install Node.js or add node to PATH.', file=sys.stderr)
    env.Exit(1)

print('Building Svelte UI...')
build = subprocess.run(
    [npm_cmd, 'run', 'build'],
    capture_output=True, text=True, cwd=ui_dir, env=tool_env
)
if build.returncode != 0:
    print('ERROR: npm run build failed:\n', build.stdout, build.stderr, file=sys.stderr)
    env.Exit(1)
else:
    print(build.stdout.strip())

if not os.path.isdir(ui_dist_dir):
    print('ERROR: UI build did not create ui/dist', file=sys.stderr)
    env.Exit(1)

print('Embedding UI assets into firmware...')
result = subprocess.run(
    [node_cmd, script, '--input', ui_dist_dir, '--output', output_file, '--prefix', '/', '--gzip'],
    capture_output=True, text=True, cwd=project_dir, env=tool_env
)

if result.returncode != 0:
    print('ERROR: embed_ui failed:\n', result.stderr, file=sys.stderr)
    env.Exit(1)
else:
    print(result.stdout.strip())
