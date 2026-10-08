"""Run instrumented CTest binaries and export LLVM line/branch coverage."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--ctest', default='ctest')
    parser.add_argument('--llvm-profdata', default='llvm-profdata')
    parser.add_argument('--llvm-cov', default='llvm-cov')
    parser.add_argument('--minimum-line', type=float, default=0)
    parser.add_argument('--minimum-branch', type=float, default=0)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    output = (args.output or build / 'coverage').resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Each invocation has fresh profiles; an earlier build cannot inflate results.
    profiles = Path(tempfile.mkdtemp(prefix='profiles-', dir=output))
    environment = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / '%m-%p.profraw'))
    inventory = json.loads(subprocess.check_output(
        [args.ctest, '--test-dir', str(build), '--show-only=json-v1'], text=True))
    binaries = sorted({str(Path(test['command'][0]).resolve()) for test in inventory['tests']
                       if test.get('command') and Path(test['command'][0]).is_file()
                       and Path(test['command'][0]).stem.startswith('axisly')})
    if not binaries:
        raise RuntimeError('No Axisly test executables found')
    with (output / 'tests.log').open('w', encoding='utf-8') as log:
        subprocess.run([args.ctest, '--test-dir', str(build), '--output-on-failure',
                        '--no-tests=error', '--parallel', '4'], env=environment,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    raw = sorted(profiles.glob('*.profraw'))
    if not raw:
        raise RuntimeError('No LLVM profiles produced; configure AXISLY_ENABLE_COVERAGE=ON')
    inputs = output / 'profiles.txt'
    inputs.write_text(''.join(str(path) + '\n' for path in raw), encoding='utf-8')
    merged = output / 'merged.profdata'
    subprocess.run([args.llvm_profdata, 'merge', '-sparse', '-f', str(inputs), '-o', str(merged)], check=True)
    objects = [binaries[0], *[f'-object={binary}' for binary in binaries[1:]],
               f'-instr-profile={merged}']
    diagnostics = output / 'diagnostics.log'
    with (output / 'coverage.json').open('w', encoding='utf-8') as export, diagnostics.open('w', encoding='utf-8') as log:
        subprocess.run([args.llvm_cov, 'export', *objects], stdout=export, stderr=log, check=True)
    messages = diagnostics.read_text(encoding='utf-8')
    data = json.loads((output / 'coverage.json').read_text(encoding='utf-8'))
    totals = {kind: {'count': 0, 'covered': 0} for kind in ('lines', 'branches')}
    files = []
    for unit in data['data']:
        for entry in unit['files']:
            try:
                relative = Path(entry['filename']).resolve().relative_to(root).as_posix()
            except ValueError:
                continue
            if not relative.startswith(('src/', 'include/')):
                continue
            counts = {kind: entry['summary'][kind] for kind in totals}
            files.append({'file': relative, **counts})
            for kind in totals:
                for field in totals[kind]:
                    totals[kind][field] += counts[kind][field]
    if not files or any(value['count'] == 0 for value in totals.values()):
        raise RuntimeError('Coverage export omitted library lines or branches')
    for value in totals.values():
        value['percent'] = 100 * value['covered'] / value['count']
    summary = {'totals': totals, 'diagnostics': messages.splitlines(),
               'files': sorted(files, key=lambda row: row['branches']['percent'])}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    with diagnostics.open('a', encoding='utf-8') as log:
        subprocess.run([args.llvm_cov, 'show', *objects, '-format=html', '-show-branches=count',
                        f'-output-dir={output / "html"}', str(root / 'src'), str(root / 'include')], stderr=log, check=True)
    print(json.dumps(totals, indent=2))
    if messages:
        print(messages.strip())
        print(f'LLVM diagnostics retained in {diagnostics}')
    if totals['lines']['percent'] < args.minimum_line or totals['branches']['percent'] < args.minimum_branch:
        raise RuntimeError('Coverage is below the configured minimum')


if __name__ == '__main__':
    main()
