Import('env')
import subprocess, os, sys

project_dir = env.subst('$PROJECT_DIR')
samples_dir = os.path.join(project_dir, 'data', 'config', 'samples')
output_file = os.path.join(project_dir, 'include', 'embedded_configs.h')
script     = os.path.join(project_dir, 'tools', 'embed_static.js')

print('Embedding sample configs into firmware...')
result = subprocess.run(
    ['node', script, '--input', samples_dir, '--output', output_file, '--prefix', '/config/'],
    capture_output=True, text=True, cwd=project_dir
)
if result.returncode != 0:
    print('ERROR: embed_configs failed:\n', result.stderr, file=sys.stderr)
    env.Exit(1)
else:
    print(result.stdout.strip())
