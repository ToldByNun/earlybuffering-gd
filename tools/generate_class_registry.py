"""Generate the authored C++ class catalog and managed-service dependency plan."""

import argparse
import json
import os
from pathlib import Path
import re


class CppClassScanner:
    @staticmethod
    def mask(source):
        pattern = r'R"([^ ()\\\t]{0,16})\(.*?\)\1"|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
        return re.sub(pattern, lambda match: ''.join('\n' if char == '\n' else ' ' for char in match[0]), source, flags=re.S)

    @staticmethod
    def closing_brace(masked, start):
        depth = 1
        for position in range(start + 1, len(masked)):
            depth += (masked[position] == '{') - (masked[position] == '}')
            if depth == 0:
                return position
        raise ValueError('Unclosed class definition')

    @staticmethod
    def scan(source, path):
        masked = CppClassScanner.mask(source)
        pattern = r'\bclass\s+(?:\$modify\(\s*(?P<hook>\w+)\s*,[^)]+\)|(?P<name>\w+(?:::\w+)*)(?:\s+final)?(?:\s*:\s*[^;{]+)?)\s*\{'
        records = []
        for match in re.finditer(pattern, masked):
            if re.search(r'\benum\s*$', masked[max(0, match.start() - 10):match.start()]):
                continue
            name = match['hook'] or match['name']
            brace = masked.index('{', match.start(), match.end())
            end = CppClassScanner.closing_brace(masked, brace)
            parent = next((item for item in reversed(records) if item['start'] < match.start() < item['end']), None)
            qualified = parent['qualified'] + '::' + name if parent else name
            getter = re.search(r'\bstatic\s+' + re.escape(name.split('::')[-1]) + r'\s*&\s*get\s*\(\s*\)', masked[brace:end])
            managed = bool(getter) and qualified != 'EbClassManager'
            records.append({
                'name': name.split('::')[-1], 'qualified': qualified,
                'file': path, 'line': source.count('\n', 0, match.start()) + 1,
                'kind': 'hook' if match['hook'] else ('service' if managed else 'class'),
                'managed': managed, 'start': match.start(), 'end': end,
                'top_level': parent is None and '::' not in name,
            })
        return records


class ClassRegistryGenerator:
    @staticmethod
    def collect(root):
        records = []
        for path in sorted((root / 'src').rglob('*')):
            if path.suffix not in ('.cpp', '.hpp'):
                continue
            relative = path.relative_to(root).as_posix()
            records.extend(CppClassScanner.scan(path.read_text(encoding='utf-8'), relative))
        return records

    @staticmethod
    def validate(records, policy):
        names = set()
        for record in records:
            name = record['qualified']
            if name in names:
                raise ValueError(f'Duplicate class definition: {name}')
            names.add(name)
            if not name.startswith('Eb'):
                raise ValueError(f'{record["file"]}:{record["line"]}: project class {name} needs the Eb prefix')
        services = {record['qualified'] for record in records if record['managed']}
        for name, dependencies in policy.items():
            if name not in services:
                raise ValueError(f'Dependency owner is not a managed service: {name}')
            if len(dependencies) != len(set(dependencies)):
                raise ValueError(f'Duplicate dependencies: {name}')
            for dependency in dependencies:
                if dependency not in services:
                    raise ValueError(f'Unknown dependency {name} -> {dependency}')
        visited, active = set(), set()

        def visit(name):
            if name in active:
                raise ValueError(f'Service dependency cycle at {name}')
            if name in visited:
                return
            active.add(name)
            for dependency in policy.get(name, []):
                visit(dependency)
            active.remove(name)
            visited.add(name)

        for name in services:
            visit(name)

    @staticmethod
    def write_changed(path, content):
        if path.exists() and path.read_text(encoding='utf-8') == content:
            return
        path.write_text(content, encoding='utf-8')

    @staticmethod
    def generate(root, output):
        records = ClassRegistryGenerator.collect(root)
        policy = json.loads((root / 'src/app/service_dependencies.json').read_text(encoding='utf-8'))
        ClassRegistryGenerator.validate(records, policy)
        records.sort(key=lambda record: (record['file'], record['qualified']))
        services = sorted((record for record in records if record['managed']), key=lambda record: record['qualified'])
        service_indices = {record['qualified']: index for index, record in enumerate(services)}
        output.mkdir(parents=True, exist_ok=True)
        include = lambda path: os.path.relpath(root / path, output).replace('\\', '/')
        header = ['#ifndef EB_CLASS_CATALOG_HPP', '#define EB_CLASS_CATALOG_HPP', '',
                  '// Generated by tools/generate_class_registry.py. Do not edit.',
                  '#include "' + include('src/app/class_registry.hpp') + '"', '#include <array>', '']
        header += [f'inline constexpr std::size_t EB_MANAGED_SERVICE_COUNT = {len(services)};',
                   'template <class Service> class EbServiceTraits;', '']
        for record in services:
            name = record['qualified']
            header += [f'class {name};', f'template <> class EbServiceTraits<{name}> {{', 'public:',
                       f'    static constexpr std::size_t index = {service_indices[name]};', 'private:', '};', '']
        header += [f'inline constexpr std::array<EbClassInfo, {len(records)}> EB_CLASS_INFOS = {{{{']
        for record in records:
            domain = Path(record['file']).parent.relative_to('src').as_posix().replace('/', '.')
            domain = 'app' if domain == '.' else domain
            identity = 'eb.' + domain + '.' + record['qualified'].replace('::', '.')
            fields = [json.dumps(identity), json.dumps(record['qualified']), json.dumps(record['file']), str(record['line']),
                      'true' if record['managed'] else 'false', 'true' if record['kind'] == 'hook' else 'false']
            header += ['    {' + ', '.join(fields) + '},']
        header += ['}};', '', '#endif', '']
        ClassRegistryGenerator.write_changed(output / 'eb_class_catalog.hpp', '\n'.join(header))
        source = ['// Generated service dependency construction order. Do not edit.',
                  '#include "' + include('src/app/class_manager.hpp') + '"']
        source += ['#include "' + include(record['file']) + '"' for record in services]
        source += ['', 'void EbClassManager::prepareService(std::size_t serviceIndex) {',
                   '    switch (serviceIndex) {']
        for record in services:
            name = record['qualified']
            dependencies = policy.get(name, [])
            if not dependencies:
                continue
            source += [f'    case EbServiceTraits<{name}>::index:']
            source += [f'        (void)EbClassManager::get().service<{dependency}>();' for dependency in dependencies]
            source += ['        return;']
        source += ['    default:', '        return;', '    }', '}', '']
        ClassRegistryGenerator.write_changed(output / 'eb_service_dependencies.cpp', '\n'.join(source))
        manifest = [{key: record[key] for key in ('qualified', 'file', 'line', 'kind', 'managed')} for record in records]
        ClassRegistryGenerator.write_changed(output / 'class_registry.json', json.dumps({'classes': manifest, 'services': policy}, indent=2) + '\n')
        print(f'Class registry: {len(records)} classes, {len(services)} managed services')

    @staticmethod
    def run():
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
        parser.add_argument('--output', type=Path, required=True)
        args = parser.parse_args()
        ClassRegistryGenerator.generate(args.root.resolve(), args.output.resolve())


if __name__ == '__main__':
    ClassRegistryGenerator.run()
