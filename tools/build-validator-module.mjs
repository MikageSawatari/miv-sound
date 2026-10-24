import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({ options: {
  source: { type: 'string' }, out: { type: 'string' }, support: { type: 'string' },
} });
if (!values.source || !values.out || !values.support) throw new Error('Expected --source, --out, --support');
let source = (await readFile(values.source, 'utf8')).replace(/\r\n/g, '\n');
function replaceOnce(before, after) {
  if (source.split(before).length !== 2) throw new Error('The pinned SDK validator module code has changed');
  source = source.replace(before, after);
}
replaceOnce('#include "module.h"',
  `#include "${path.resolve(values.support).replaceAll('\\', '/')}"\n#include "module.h"`);
replaceOnce('\t~Win32Module () override\n\t{\n\t\tfactory = PluginFactory (nullptr);',
  '\t~Win32Module () override\n\t{\n\t\teffetune::vst::testing::drainTrackedWebViews(\n\t\t    effetune::vst::testing::moduleWebViewCallbackCounter((HMODULE)mModule));\n\t\tfactory = PluginFactory (nullptr);');
await mkdir(path.dirname(values.out), { recursive: true });
await writeFile(values.out, '// Generated test-only SDK adapter.\n' + source);
